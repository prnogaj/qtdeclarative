// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant

#include "qqmljslintercodegen_p.h"
#include "qqmljslintertypepropagator_p.h"

#include <private/qqmljsbasicblocks_p.h>
#include <private/qqmljsfunctioninitializer_p.h>
#include <private/qqmljsimportvisitor_p.h>
#include <private/qqmljsshadowcheck_p.h>
#include <private/qqmljsstoragegeneralizer_p.h>
#include <private/qqmljsstorageinitializer_p.h>

#include <QFileInfo>

#include <functional>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

bool operator==(const IdMemberShadow &lhs, const IdMemberShadow &rhs)
{
    return lhs.name == rhs.name && lhs.idScope == rhs.idScope
            && lhs.memberOwnerScope == rhs.memberOwnerScope;
}
bool operator!=(const IdMemberShadow &lhs, const IdMemberShadow &rhs)
{
    return !(lhs == rhs);
}
size_t qHash(const IdMemberShadow &idShadowsMember, size_t seed)
{
    return qHashMulti(seed, idShadowsMember.name, idShadowsMember.idScope,
                      idShadowsMember.memberOwnerScope);
}
QQmlJSLinterCodegen::QQmlJSLinterCodegen(QQmlJSImporter *importer, const QString &fileName,
                                         const QStringList &qmldirFiles, QQmlJSLogger *logger,
                                         const QQmlJS::LinterContext &context)
    : QQmlJSAotCompiler(importer, fileName, qmldirFiles, logger), m_context(context)
{
    m_flags |= QQmlJSAotCompiler::IsLintCompiler;
}

void QQmlJSLinterCodegen::setDocument(const QmlIR::JSCodeGen *codegen,
                                      const QmlIR::Document *document)
{
    Q_UNUSED(codegen);
    m_document = document;
    m_unitGenerator = &document->jsGenerator;
    m_lintedOnTheirOwn.clear();
    m_lintedAsClosures.clear();
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>>
QQmlJSLinterCodegen::compileBinding(const QV4::Compiler::Context *context,
                                    const QmlIR::Binding &irBinding, QQmlJS::AST::Node *astNode)
{
    const QString name = m_document->stringAt(irBinding.propertyNameIndex);
    m_logger->setCompileErrorPrefix(
            u"Could not determine signature of binding for %1: "_s.arg(name));

    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);
    QQmlJSCompilePass::Function function = initializer.run(context, name, astNode, irBinding);

    m_logger->iterateCurrentFunctionMessages([this](const Message &error) {
        diagnose(error.message, error.type, error.loc);
    });

    m_logger->setCompileErrorPrefix(u"Could not compile binding for %1: "_s.arg(name));
    m_logger->setCompileSkipPrefix(u"Compilation of binding for %1 was skipped: "_s.arg(name));

    analyzeFunction(context, &function);
    if (const auto errors = finalizeBindingOrFunction())
        return *errors;

    return QQmlJSAotFunction {};
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>>
QQmlJSLinterCodegen::compileFunction(const QV4::Compiler::Context *context,
                                     const QString &name, QQmlJS::AST::Node *astNode)
{
    m_logger->setCompileErrorPrefix(u"Could not determine signature of function %1: "_s.arg(name));

    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);
    QQmlJSCompilePass::Function function = initializer.run(context, name, astNode);

    m_logger->iterateCurrentFunctionMessages([this](const Message &error) {
        diagnose(error.message, error.type, error.loc);
    });

    m_logger->setCompileErrorPrefix(u"Could not compile function %1: "_s.arg(name));
    m_logger->setCompileSkipPrefix(u"Compilation of function %1 was skipped: "_s.arg(name));
    analyzeFunction(context, &function);

    if (const auto errors = finalizeBindingOrFunction())
        return *errors;

    return QQmlJSAotFunction {};
}

void QQmlJSLinterCodegen::setPassManager(QQmlSA::PassManager *passManager)
{
    m_passManager = passManager;
    auto managerPriv = QQmlSA::PassManagerPrivate::get(passManager);
    managerPriv->m_typeResolver = typeResolver();
}

namespace {
struct LintClosures : QQmlJSCompilePass::ClosureSupport
{
    using Analyze = std::function<bool(
            int, const QQmlJSCompilePass::Function *, const QList<QQmlJSRegisterContent> &,
            const QQmlJSScope::ConstPtr &)>;

    bool analyzeClosure(
            int functionIndex, const QQmlJSCompilePass::Function *outer,
            const QList<QQmlJSRegisterContent> &argumentTypes,
            const QQmlJSScope::ConstPtr &returnType) override
    {
        return analyze(functionIndex, outer, argumentTypes, returnType);
    }

    Analyze analyze;
};
} // namespace

/*!
 * \internal
 * Sets up the closure with index \a functionIndex, created by \a outer, for type propagation.
 * \a argumentTypes are the types we know it is called with, and \a returnType is what its
 * caller expects it to return, if we know. Parameters we know nothing about are of type var,
 * and declared types take precedence. Returns the context of the closure, or nullptr if there
 * is none.
 */
const QV4::Compiler::Context *QQmlJSLinterCodegen::initializeClosure(
        int functionIndex, const QQmlJSCompilePass::Function *outer,
        const QList<QQmlJSRegisterContent> &argumentTypes,
        const QQmlJSScope::ConstPtr &returnType, QQmlJSCompilePass::Function *closure)
{
    const QV4::Compiler::Context *context = m_document->jsModule.functions.value(functionIndex);
    if (!context || context->isGenerator)
        return nullptr;

    const qsizetype formals = context->arguments.size();
    QList<QQmlJSRegisterContent> arguments = argumentTypes.first(qMin(formals, argumentTypes.size()));
    while (arguments.size() < formals)
        arguments.append(m_typeResolver.namedType(m_typeResolver.varType()));

    // As for any other function: it is typed if all its parameters are annotated, or if it has
    // none and declares what it returns.
    bool isFullyTyped = false;
    QQmlJSScope::ConstPtr declaredReturnType;
    if (QQmlJS::AST::Node *astNode = m_document->jsModule.contextMap.key(
                const_cast<QV4::Compiler::Context *>(context))) {
        if (QQmlJS::AST::FunctionExpression *ast = astNode->asFunctionDefinition()) {
            QQmlJS::AST::BoundNames declared;
            if (ast->formals)
                declared = ast->formals->formals();
            isFullyTyped = m_typeResolver.canCallJSFunctions()
                    && (!declared.isEmpty() || ast->typeAnnotation);
            for (qsizetype i = 0; i < declared.size() && i < formals; ++i) {
                if (!declared[i].typeAnnotation) {
                    isFullyTyped = false;
                    continue;
                }
                if (const QQmlJSScope::ConstPtr type
                            = m_typeResolver.typeFromAST(declared[i].typeAnnotation->type)) {
                    arguments[i] = m_typeResolver.namedType(type);
                } else {
                    isFullyTyped = false;
                }
            }
            if (ast->typeAnnotation)
                declaredReturnType = m_typeResolver.typeFromAST(ast->typeAnnotation->type);
        }
    }

    closure->closureSupport = outer->closureSupport;
    closure->identity = context;
    closure->isInlinedClosure = true;
    closure->contextChain = outer->contextChain;
    if (context->requiresExecutionContext) {
        // Its variables are captured in turn. It gets a context of its own on each call.
        closure->contextChain.prepend(context);
        closure->ownsContext = true;
        if (context->argumentsCanEscape)
            closure->firstArgumentLocal = context->locals.size();
    }
    closure->qmlScope = outer->qmlScope;
    closure->addressableScopes = outer->addressableScopes;
    closure->argumentTypes = arguments;
    // Without a declared return type, the type propagator infers one. See inferredReturnType.
    // What the caller makes of the result is no reason to complain about what is returned:
    // JavaScript coerces it.
    Q_UNUSED(returnType);
    if (declaredReturnType)
        closure->returnType = m_typeResolver.namedType(declaredReturnType);
    for (int i = QQmlJSCompilePass::FirstArgument + formals;
         i < context->registerCountInFunction; ++i) {
        closure->registerTypes.append(m_typeResolver.namedType(m_typeResolver.voidType()));
    }
    closure->code = context->code;
    closure->sourceLocations = context->sourceLocationTable.get();
    closure->isFullyTyped = isFullyTyped;
    return context;
}

void QQmlJSLinterCodegen::analyzeFunction(const QV4::Compiler::Context *context,
                                          QQmlJSCompilePass::Function *function)
{
    // See m_lintedAsClosures. There is nothing left to say about this one.
    if (m_lintedAsClosures.contains(context))
        return;
    m_lintedOnTheirOwn.insert(context);

    // The closures a function creates are analyzed with it: the callbacks of the methods of
    // lists and of then() with the types they are called with, when the type propagator sees
    // the call, and the others afterwards. They share the types of the variables they capture.
    LintClosures closures;
    closures.analysisOnly = true;

    // Whatever is propagated without reporting is propagated again, or was before. The passes
    // of plugins are only told about it once.
    const auto suspendPasses = [this](bool suspended) {
        const bool before = m_passManager
                && QQmlSA::PassManagerPrivate::get(m_passManager)->arePropertyPassesSuspended();
        if (m_passManager) {
            QQmlSA::PassManagerPrivate::get(m_passManager)
                    ->setPropertyPassesSuspended(suspended);
        }
        return before;
    };

    const auto propagate = [this](const QV4::Compiler::Context *functionContext,
                                  const QQmlJSCompilePass::Function *propagated) {
        bool dummy = false;
        QQmlJSCompilePass::BlocksAndAnnotations result =
                QQmlJSBasicBlocks(functionContext, m_unitGenerator, &m_typeResolver, m_logger)
                        .run(propagated, ValidateBasicBlocks, dummy);

        QQmlJSLinterTypePropagator lintTypePropgator(
                m_unitGenerator, &m_typeResolver, m_logger, m_context, result.basicBlocks,
                result.annotations, m_passManager);
        lintTypePropgator.setIdMemberShadows(&m_idMemberShadows);
        return lintTypePropgator.run(propagated);
    };

    int depth = 0;
    std::function<void(const QQmlJSCompilePass::Function *)> analyzeOtherClosures;
    closures.analyze = [&](int functionIndex, const QQmlJSCompilePass::Function *outer,
                           const QList<QQmlJSRegisterContent> &argumentTypes,
                           const QQmlJSScope::ConstPtr &returnType) {
        if (depth > 16)
            return false;

        QQmlJSCompilePass::Function closure;
        const QV4::Compiler::Context *closureContext = initializeClosure(
                functionIndex, outer, argumentTypes, returnType, &closure);
        if (!closureContext)
            return false;

        // The closure may analyze further closures. So, don't hold on to its entry.
        QQmlJSScope::ConstPtr inferredReturnType;
        const QQmlJSScope::ConstPtr knownReturnType = closure.returnType.isValid()
                ? closure.returnType.containedType()
                : QQmlJSScope::ConstPtr();
        if (!knownReturnType)
            closure.inferredReturnType = &inferredReturnType;

        // If the closure was linted on its own already, we only want the types.
        const bool wasDisabled = m_logger->isDisabled();
        const bool isLinted = m_lintedOnTheirOwn.contains(closureContext);
        const QSet<IdMemberShadow> knownShadows = m_idMemberShadows;
        const bool wereSuspended = suspendPasses(isLinted || wasDisabled);
        if (isLinted)
            m_logger->setIsDisabled(true);
        else if (!wasDisabled)
            m_lintedAsClosures.insert(closureContext);

        ++depth;
        propagate(closureContext, &closure);
        analyzeOtherClosures(&closure);
        --depth;
        m_logger->setIsDisabled(wasDisabled);
        suspendPasses(wereSuspended);
        if (isLinted && !wasDisabled)
            m_idMemberShadows = knownShadows;

        QQmlJSCompilePass::ClosureSupport::Closure &analyzed = closures.closures[functionIndex];
        analyzed.argumentTypes = closure.argumentTypes;
        analyzed.returnType = knownReturnType ? knownReturnType : inferredReturnType;
        return true;
    };

    // The closures the given function creates that were not analyzed where they are passed on.
    // We don't know who calls them, with what.
    analyzeOtherClosures = [&](const QQmlJSCompilePass::Function *creator) {
        QList<int> others;
        for (auto it = closures.loadedClosures.constBegin(),
                  end = closures.loadedClosures.constEnd(); it != end; ++it) {
            if (it.key().first == creator->identity && !closures.inlinedLoads.contains(it.key())
                    && !others.contains(it.value())) {
                others.append(it.value());
            }
        }
        // In the order they are written, so that what is only reported once is reported
        // where it comes first in the document.
        const auto position = [this](int functionIndex) {
            const QV4::Compiler::Context *closure
                    = m_document->jsModule.functions.value(functionIndex);
            return closure ? std::make_pair(closure->line, closure->column) : std::make_pair(0, 0);
        };
        std::sort(others.begin(), others.end(), [&](int a, int b) {
            const auto positionA = position(a);
            const auto positionB = position(b);
            return positionA != positionB ? positionA < positionB : a < b;
        });

        for (int functionIndex : std::as_const(others)) {
            QList<QQmlJSRegisterContent> arguments;
            const auto contextual = closures.contextualArgumentTypes.value(functionIndex);
            for (const QQmlJSScope::ConstPtr &type : contextual)
                arguments.append(m_typeResolver.namedType(type));
            closures.analyze(functionIndex, creator, arguments, QQmlJSScope::ConstPtr());
        }
    };

    // For comparing with what the linter did before it knew about closures
    static const bool withClosures = !qEnvironmentVariableIsSet("QMLLINT_NO_CLOSURE_ANALYSIS");
    if (withClosures) {
        function->closureSupport = &closures;
        function->identity = context;
        if (context->requiresExecutionContext) {
            function->contextChain = { context };
            function->ownsContext = true;
            if (context->argumentsCanEscape)
                function->firstArgumentLocal = context->locals.size();
        }
    }

    // The types of the captured variables depend on what the function and its closures store
    // in them. Propagate without reporting anything until they don't change anymore. Types
    // are only ever merged into more general ones, so this terminates.
    if (withClosures && !context->nestedContexts.isEmpty()) {
        // What is found in these rounds is not reported. So it must not count as known when
        // it is found again.
        const bool wasDisabled = m_logger->isDisabled();
        const QSet<IdMemberShadow> knownShadows = m_idMemberShadows;
        const bool wereSuspended = suspendPasses(true);
        m_logger->setIsDisabled(true);
        for (int round = 0; round < 16; ++round) {
            closures.localTypesChanged = false;
            closures.loadedClosures.clear();
            closures.inlinedLoads.clear();
            closures.inlinedCalls.clear();
            propagate(context, function);
            analyzeOtherClosures(function);
            if (!closures.localTypesChanged)
                break;
        }
        m_logger->setIsDisabled(wasDisabled);
        suspendPasses(wereSuspended);
        m_idMemberShadows = knownShadows;
        closures.loadedClosures.clear();
        closures.inlinedLoads.clear();
        closures.inlinedCalls.clear();
    }

    QQmlJSCompilePass::BlocksAndAnnotations blocksAndAnnotations = propagate(context, function);
    analyzeOtherClosures(function);

    if (m_logger->categorySeverity(qmlCompiler) == QQmlJS::WarningSeverity::Disable)
        return;

    if (!m_logger->currentFunctionHasCompileError()) {
        blocksAndAnnotations = QQmlJSShadowCheck(m_unitGenerator, &m_typeResolver, m_logger,
                                                 blocksAndAnnotations.basicBlocks,
                                                 blocksAndAnnotations.annotations)
                                       .run(function);
    }

    if (!m_logger->currentFunctionHasCompileError()) {
        blocksAndAnnotations = QQmlJSStorageInitializer(m_unitGenerator, &m_typeResolver, m_logger,
                                                        blocksAndAnnotations.basicBlocks,
                                                        blocksAndAnnotations.annotations)
                                       .run(function);
    }

    if (!m_logger->currentFunctionHasCompileError()) {
        blocksAndAnnotations = QQmlJSStorageGeneralizer(m_unitGenerator, &m_typeResolver, m_logger,
                                                        blocksAndAnnotations.basicBlocks,
                                                        blocksAndAnnotations.annotations)
                                       .run(function);
    }
}

QT_END_NAMESPACE
