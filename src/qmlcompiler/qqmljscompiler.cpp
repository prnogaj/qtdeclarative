// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant

#include "qqmljscompiler_p.h"

#include <private/qqmlirbuilder_p.h>
#include <private/qqmljsaotirbuilder_p.h>
#include <private/qqmljsbasicblocks_p.h>
#include <private/qqmljscodegenerator_p.h>
#include <private/qqmljscompilerstats_p.h>
#include <private/qqmljsfunctioninitializer_p.h>
#include <private/qqmljsimportvisitor_p.h>
#include <private/qqmljslexer_p.h>
#include <private/qqmljsloadergenerator_p.h>
#include <private/qqmljsoptimizations_p.h>
#include <private/qqmljsparser_p.h>
#include <private/qqmljsshadowcheck_p.h>
#include <private/qqmljsstoragegeneralizer_p.h>
#include <private/qqmljsstorageinitializer_p.h>
#include <private/qqmljstypepropagator_p.h>

#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qelapsedtimer.h>

#include <QtQml/private/qqmlsignalnames_p.h>

#include <functional>
#include <limits>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcAotCompiler, "qt.qml.compiler.aot", QtFatalMsg);

static const int FileScopeCodeIndex = -1;

void QQmlJSCompileError::print()
{
    fprintf(stderr, "%s\n", qPrintable(message));
}

QQmlJSCompileError QQmlJSCompileError::augment(const QString &contextErrorMessage) const
{
    QQmlJSCompileError augmented;
    augmented.message = contextErrorMessage + message;
    return augmented;
}

static QString diagnosticErrorMessage(const QString &fileName, const QQmlJS::DiagnosticMessage &m)
{
    QString message;
    message = fileName + QLatin1Char(':') + QString::number(m.loc.startLine) + QLatin1Char(':');
    if (m.loc.startColumn > 0)
        message += QString::number(m.loc.startColumn) + QLatin1Char(':');

    if (m.isError())
        message += QLatin1String(" error: ");
    else
        message += QLatin1String(" warning: ");
    message += m.message;
    return message;
}

void QQmlJSCompileError::appendDiagnostic(const QString &inputFileName,
                                          const QQmlJS::DiagnosticMessage &diagnostic)
{
    if (!message.isEmpty())
        message += QLatin1Char('\n');
    message += diagnosticErrorMessage(inputFileName, diagnostic);
}

void QQmlJSCompileError::appendDiagnostics(const QString &inputFileName,
                                           const QList<QQmlJS::DiagnosticMessage> &diagnostics)
{
    for (const QQmlJS::DiagnosticMessage &diagnostic: diagnostics)
        appendDiagnostic(inputFileName, diagnostic);
}

static bool checkArgumentsObjectUseInSignalHandlers(const QmlIR::Document &doc,
                                                    QQmlJSCompileError *error)
{
    for (QmlIR::Object *object: std::as_const(doc.objects)) {
        for (auto binding = object->bindingsBegin(); binding != object->bindingsEnd(); ++binding) {
            if (binding->type() != QV4::CompiledData::Binding::Type_Script)
                continue;
            const QString propName =  doc.stringAt(binding->propertyNameIndex);
            if (!QQmlSignalNames::isHandlerName(propName))
                continue;
            auto compiledFunction = doc.jsModule.functions.value(object->runtimeFunctionIndices.at(binding->value.compiledScriptIndex));
            if (!compiledFunction)
                continue;
            if (compiledFunction->usesArgumentsObject == QV4::Compiler::Context::UsesArgumentsObject::Used) {
                error->message = QLatin1Char(':') + QString::number(compiledFunction->line) + QLatin1Char(':');
                if (compiledFunction->column > 0)
                    error->message += QString::number(compiledFunction->column) + QLatin1Char(':');

                error->message += QLatin1String(" error: The use of eval() or the use of the arguments object in signal handlers is\n"
                                                "not supported when compiling qml files ahead of time. That is because it's ambiguous if \n"
                                                "any signal parameter is called \"arguments\". Similarly the string passed to eval might use\n"
                                                "\"arguments\". Unfortunately we cannot distinguish between it being a parameter or the\n"
                                                "JavaScript arguments object at this point.\n"
                                                "Consider renaming the parameter of the signal if applicable or moving the code into a\n"
                                                "helper function.");
                return false;
            }
        }
    }
    return true;
}

class BindingOrFunction
{
public:
    BindingOrFunction(const QmlIR::Binding &b) : m_binding(&b) {}
    BindingOrFunction(const QmlIR::Function &f) : m_function(&f) {}

    friend bool operator<(const BindingOrFunction &lhs, const BindingOrFunction &rhs)
    {
        return lhs.index() < rhs.index();
    }

    const QmlIR::Binding *binding() const { return m_binding; }
    const QmlIR::Function *function() const { return m_function; }

    quint32 index() const
    {
        return m_binding
                ? m_binding->value.compiledScriptIndex
                : (m_function
                   ? m_function->index
                   : std::numeric_limits<quint32>::max());
    }

private:
    const QmlIR::Binding *m_binding = nullptr;
    const QmlIR::Function *m_function = nullptr;
};

bool qCompileQmlFile(const QString &inputFileName, const QQmlJSSaveFunction &saveFunction,
                     QQmlJSAotCompiler *aotCompiler, QQmlJSCompileError *error,
                     bool storeSourceLocation, QV4::Compiler::CodegenWarningInterface *wInterface,
                     const QString *fileContents)
{
    QmlIR::Document irDocument(QString(), QString(), /*debugMode*/false);
    return qCompileQmlFile(irDocument, inputFileName, saveFunction, aotCompiler, error,
                           storeSourceLocation, wInterface, fileContents);
}

bool qCompileQmlFile(QmlIR::Document &irDocument, const QString &inputFileName,
                     const QQmlJSSaveFunction &saveFunction, QQmlJSAotCompiler *aotCompiler,
                     QQmlJSCompileError *error, bool storeSourceLocation,
                     QV4::Compiler::CodegenWarningInterface *wInterface, const QString *fileContents)
{
    QString sourceCode;

    if (fileContents != nullptr) {
        sourceCode = *fileContents;
    } else {
        QFile f(inputFileName);
        if (!f.open(QIODevice::ReadOnly)) {
            error->message = QLatin1String("Error opening ") + inputFileName + QLatin1Char(':') + f.errorString();
            return false;
        }
        sourceCode = QString::fromUtf8(f.readAll());
        if (f.error() != QFileDevice::NoError) {
            error->message = QLatin1String("Error reading from ") + inputFileName + QLatin1Char(':') + f.errorString();
            return false;
        }
    }

    {
        // For now, only use the AOT IRBuilder when linting
        std::unique_ptr<QmlIR::IRBuilder> irBuilder = aotCompiler && aotCompiler->isLintCompiler()
                ? std::make_unique<QQmlJSAOTIRBuilder>() : std::make_unique<QmlIR::IRBuilder>();
        if (!irBuilder->generateFromQml(sourceCode, inputFileName, &irDocument, wInterface)) {
            error->appendDiagnostics(inputFileName, irBuilder->errors);
            return false;
        }
    }

    QQmlJSAotFunctionMap aotFunctionsByIndex;

    {
        QmlIR::JSCodeGen v4CodeGen(&irDocument, wInterface, storeSourceLocation);

        if (aotCompiler)
            aotCompiler->setDocument(&v4CodeGen, &irDocument);

        QHash<QmlIR::Object *, QmlIR::Object *> effectiveScopes;
        for (QmlIR::Object *object: std::as_const(irDocument.objects)) {
            if (object->functionsAndExpressions->count == 0 && object->bindingCount() == 0)
                continue;

            if (!v4CodeGen.generateRuntimeFunctions(object)) {
                Q_ASSERT(v4CodeGen.hasError());
                error->appendDiagnostic(inputFileName, v4CodeGen.error());
                return false;
            }

            if (!aotCompiler)
                continue;

            QmlIR::Object *scope = object;
            for (auto it = effectiveScopes.constFind(scope), end = effectiveScopes.constEnd();
                 it != end; it = effectiveScopes.constFind(scope)) {
                scope = *it;
            }

            aotCompiler->setScope(object, scope);
            aotFunctionsByIndex[FileScopeCodeIndex] = aotCompiler->globalCode();

            std::vector<BindingOrFunction> bindingsAndFunctions;
            bindingsAndFunctions.reserve(object->bindingCount() + object->functionCount());

            std::copy(object->bindingsBegin(), object->bindingsEnd(),
                      std::back_inserter(bindingsAndFunctions));
            std::copy(object->functionsBegin(), object->functionsEnd(),
                      std::back_inserter(bindingsAndFunctions));

            QList<QmlIR::CompiledFunctionOrExpression> functionsToCompile;
            for (QmlIR::CompiledFunctionOrExpression *foe = object->functionsAndExpressions->first;
                 foe; foe = foe->next) {
                functionsToCompile << *foe;
            }

            // AOT-compile bindings and functions in the same order as above so that the runtime
            // class indices match
            auto contextMap = v4CodeGen.module()->contextMap;
            std::sort(bindingsAndFunctions.begin(), bindingsAndFunctions.end());
            std::for_each(bindingsAndFunctions.begin(), bindingsAndFunctions.end(),
                          [&](const BindingOrFunction &bindingOrFunction) {
                std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>> result;
                if (const auto *binding = bindingOrFunction.binding()) {
                    switch (binding->type()) {
                    case QmlIR::Binding::Type_AttachedProperty:
                    case QmlIR::Binding::Type_GroupedProperty:
                        effectiveScopes.insert(
                                    irDocument.objects.at(binding->value.objectIndex), scope);
                        return;
                    case QmlIR::Binding::Type_Boolean:
                    case QmlIR::Binding::Type_Number:
                    case QmlIR::Binding::Type_String:
                    case QmlIR::Binding::Type_Null:
                    case QmlIR::Binding::Type_Object:
                    case QmlIR::Binding::Type_Translation:
                    case QmlIR::Binding::Type_TranslationById:
                        return;
                    default:
                        break;
                    }

                    Q_ASSERT(quint32(functionsToCompile.size()) > binding->value.compiledScriptIndex);
                    const auto &functionToCompile
                            = functionsToCompile[binding->value.compiledScriptIndex];
                    auto *parentNode = functionToCompile.parentNode;
                    Q_ASSERT(parentNode);
                    Q_ASSERT(contextMap.contains(parentNode));
                    QV4::Compiler::Context *context = contextMap.take(parentNode);
                    Q_ASSERT(context);

                    auto *node = functionToCompile.node;
                    Q_ASSERT(node);

                    if (context->returnsClosure) {
                        QQmlJS::AST::Node *inner
                                = QQmlJS::AST::cast<QQmlJS::AST::ExpressionStatement *>(
                                    node)->expression;
                        Q_ASSERT(inner);
                        QV4::Compiler::Context *innerContext = contextMap.take(inner);
                        Q_ASSERT(innerContext);
                        qCDebug(lcAotCompiler) << "Compiling signal handler for"
                                               << irDocument.stringAt(binding->propertyNameIndex);
                        std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>> innerResult
                                = aotCompiler->compileBinding(innerContext, *binding, inner);
                        if (auto *errors = std::get_if<QList<QQmlJS::DiagnosticMessage>>(&innerResult)) {
                            for (const auto &error : std::as_const(*errors)) {
                                qCDebug(lcAotCompiler) << "Compilation failed:"
                                                       << diagnosticErrorMessage(inputFileName, error);
                            }
                        } else if (auto *func = std::get_if<QQmlJSAotFunction>(&innerResult)) {
                            qCDebug(lcAotCompiler) << "Generated code:" << func->code;
                            aotFunctionsByIndex[innerContext->functionIndex] = *func;
                        }
                    }

                    qCDebug(lcAotCompiler) << "Compiling binding for property"
                                           << irDocument.stringAt(binding->propertyNameIndex);
                    result = aotCompiler->compileBinding(context, *binding, node);
                } else if (const auto *function = bindingOrFunction.function()) {
                    if (!aotCompiler->isLintCompiler() && !function->isQmlFunction)
                        return;

                    Q_ASSERT(quint32(functionsToCompile.size()) > function->index);
                    auto *node = functionsToCompile[function->index].node;
                    Q_ASSERT(node);
                    Q_ASSERT(contextMap.contains(node));
                    QV4::Compiler::Context *context = contextMap.take(node);
                    Q_ASSERT(context);

                    const QString functionName = irDocument.stringAt(function->nameIndex);
                    qCDebug(lcAotCompiler) << "Compiling function" << functionName;
                    result = aotCompiler->compileFunction(context, functionName, node);
                } else {
                    Q_UNREACHABLE();
                }

                if (auto *errors = std::get_if<QList<QQmlJS::DiagnosticMessage>>(&result)) {
                    for (const auto &error : std::as_const(*errors)) {
                        qCDebug(lcAotCompiler) << "Compilation failed:"
                                               << diagnosticErrorMessage(inputFileName, error);
                    }
                } else if (auto *func = std::get_if<QQmlJSAotFunction>(&result)) {
                    if (func->skipReason.has_value()) {
                        qCDebug(lcAotCompiler) << "Compilation skipped:" << func->skipReason.value();
                    } else {
                        qCDebug(lcAotCompiler) << "Generated code:" << func->code;
                        auto index = object->runtimeFunctionIndices[bindingOrFunction.index()];
                        aotFunctionsByIndex[index] = *func;

                        // The closures the function creates as function objects
                        const auto closures = aotCompiler->takeClosureFunctions();
                        for (auto it = closures.constBegin(), end = closures.constEnd();
                             it != end; ++it) {
                            aotFunctionsByIndex[it.key()] = it.value();
                        }
                    }
                }
            });
        }

        if (!checkArgumentsObjectUseInSignalHandlers(irDocument, error)) {
            *error = error->augment(inputFileName);
            return false;
        }

        QmlIR::QmlUnitGenerator generator;
        irDocument.javaScriptCompilationUnit = v4CodeGen.generateCompilationUnit(/*generate unit*/false);
        generator.generate(irDocument);

        const quint32 saveFlags
                = QV4::CompiledData::Unit::StaticData
                | QV4::CompiledData::Unit::PendingTypeCompilation;
        QV4::CompiledData::SaveableUnitPointer saveable(
                irDocument.javaScriptCompilationUnit->unitData(), saveFlags);
        LookupSignatures sigs = aotCompiler ? aotCompiler->lookupSignatures() : LookupSignatures();
        if (!saveFunction(saveable, aotFunctionsByIndex, sigs, &error->message))
            return false;
    }
    return true;
}

bool qCompileJSFile(
        const QString &inputFileName, const QString &inputFileUrl,
        const QQmlJSSaveFunction &saveFunction, QQmlJSCompileError *error)
{
    Q_UNUSED(inputFileUrl);

    QQmlRefPointer<QV4::CompiledData::CompilationUnit> unit;

    QString sourceCode;
    {
        QFile f(inputFileName);
        if (!f.open(QIODevice::ReadOnly)) {
            error->message = QLatin1String("Error opening ") + inputFileName + QLatin1Char(':') + f.errorString();
            return false;
        }
        sourceCode = QString::fromUtf8(f.readAll());
        if (f.error() != QFileDevice::NoError) {
            error->message = QLatin1String("Error reading from ") + inputFileName + QLatin1Char(':') + f.errorString();
            return false;
        }
    }

    const bool isModule = inputFileName.endsWith(QLatin1String(".mjs"));
    if (isModule) {
        QList<QQmlJS::DiagnosticMessage> diagnostics;
        // Precompiled files are relocatable and the final location will be set when loading.
        QString url;
        unit = QV4::Compiler::Codegen::compileModule(/*debugMode*/false, url, sourceCode,
                                                     QDateTime(), &diagnostics);
        error->appendDiagnostics(inputFileName, diagnostics);
        if (!unit || !unit->unitData())
            return false;
    } else {
        QmlIR::Document irDocument(QString(), QString(), /*debugMode*/false);

        QQmlJS::Engine *engine = &irDocument.jsParserEngine;
        QmlIR::ScriptDirectivesCollector directivesCollector(&irDocument);
        QQmlJS::Directives *oldDirs = engine->directives();
        engine->setDirectives(&directivesCollector);
        auto directivesGuard = qScopeGuard([engine, oldDirs]{
            engine->setDirectives(oldDirs);
        });

        QQmlJS::AST::Program *program = nullptr;

        {
            QQmlJS::Lexer lexer(engine);
            lexer.setCode(sourceCode, /*line*/1, /*parseAsBinding*/false);
            QQmlJS::Parser parser(engine);

            bool parsed = parser.parseProgram();

            error->appendDiagnostics(inputFileName, parser.diagnosticMessages());

            if (!parsed)
                return false;

            program = QQmlJS::AST::cast<QQmlJS::AST::Program*>(parser.rootNode());
            if (!program) {
                lexer.setCode(QStringLiteral("undefined;"), 1, false);
                parsed = parser.parseProgram();
                Q_ASSERT(parsed);
                program = QQmlJS::AST::cast<QQmlJS::AST::Program*>(parser.rootNode());
                Q_ASSERT(program);
            }
        }

        {
            QmlIR::JSCodeGen v4CodeGen(&irDocument);
            v4CodeGen.generateFromProgram(
                    sourceCode, program, &irDocument.jsModule,
                    QV4::Compiler::ContextType::ScriptImportedByQML);
            if (v4CodeGen.hasError()) {
                error->appendDiagnostic(inputFileName, v4CodeGen.error());
                return false;
            }

            // Precompiled files are relocatable and the final location will be set when loading.
            Q_ASSERT(irDocument.jsModule.fileName.isEmpty());
            Q_ASSERT(irDocument.jsModule.finalUrl.isEmpty());

            irDocument.javaScriptCompilationUnit = v4CodeGen.generateCompilationUnit(/*generate unit*/false);
            QmlIR::QmlUnitGenerator generator;
            generator.generate(irDocument);
            unit = std::move(irDocument.javaScriptCompilationUnit);
        }
    }

    QQmlJSAotFunctionMap funcs;
    LookupSignatures sigs;
    return saveFunction(
            QV4::CompiledData::SaveableUnitPointer(unit->unitData()), funcs, sigs, &error->message);
}

static const char *funcHeaderCode = R"(
    [](const QQmlPrivate::AOTCompiledContext *aotContext, void **argv) {
Q_UNUSED(aotContext)
Q_UNUSED(argv)
)";

static QString wrapString(const QString &s)
{
    return "u\"%1\"_s"_L1.arg(s);
}

static QString typeToString(const QQmlPrivate::AOTLookupValidation::Type &type)
{
    bool isComposite = type.isComposite == QQmlPrivate::AOTLookupValidation::IsComposite::Yes;
    bool isIC = type.isInlineComponent == QQmlPrivate::AOTLookupValidation::IsIC::Yes;
    return u"Type{ %1, %2, %3, %4, %5 }"_s
            .arg(wrapString(type.module), wrapString(type.name),
                 wrapString(type.icNameOrExtensionTypeName),
                 isComposite ? "IsComposite::Yes"_L1 : "IsComposite::No"_L1,
                 isIC ? "IsIC::Yes"_L1 : "IsIC::No"_L1);
};

static QString lookupToString(const QQmlPrivate::AOTLookupValidation::Lookup &lookup)
{
    return "Lookup{ %1, %2, %3 }"_L1.arg(typeToString(lookup.base), wrapString(lookup.member),
                                         wrapString(lookup.enumName));
};

static QString signatureToString(const QQmlPrivate::AOTLookupValidation::Signature &signature)
{
    if (const auto *p = std::get_if<QQmlPrivate::AOTLookupValidation::PropertySignature>(&signature)) {
        return u"PropertySignature{ %1, %2 }"_s
                .arg(typeToString(p->type), QString::number(p->relativeIndex));
    } else if (const auto *e = std::get_if<QQmlPrivate::AOTLookupValidation::EnumKeySignature>(&signature)) {
        bool isFlag = e->isFlag == QQmlPrivate::AOTLookupValidation::IsFlag::Yes;
        return u"EnumKeySignature{ %1, %2 }"_s
                .arg(QString::number(e->value), isFlag ? "IsFlag::Yes"_L1 : "IsFlag::No"_L1);
    } else if (const auto *m = std::get_if<QQmlPrivate::AOTLookupValidation::MethodSignature>(&signature)) {
        QString paramNames = "{ "_L1;
        for (const auto &paramName : m->paramNames)
            paramNames += wrapString(paramName) + ", "_L1;
        paramNames += u'}';

        QString types = "{ "_L1;
        for (const auto &paramType : m->types)
            types += typeToString(paramType) + ", "_L1;
        types += u'}';

        bool isSignal = m->isSignal == QQmlPrivate::AOTLookupValidation::IsSignal::Yes;
        return u"MethodSignature{ %1, %2, %3, %4 }"_s
                .arg(paramNames, types, QString::number(m->relativeIndex),
                     isSignal ? "IsSignal::Yes"_L1 : "IsSignal::No"_L1);
    }
    Q_UNREACHABLE_RETURN(QString());
};

static const char *skippedValidationCode = R"(
bool validateLookupSignatures(QQmlEngine *engine, QV4::CompiledData::CompilationUnit *cu)
{
    // AOT validation code not generated (NO_GENERATE_AOT_VALIDATION)
    Q_UNUSED(engine);
    Q_UNUSED(cu);
    return true;
}

)";

template <typename WriteStr>
static bool generateAotValidationCode(
        const WriteStr &writeStr, const LookupSignatures &lookupSignatures, bool noAotValidation)
{
    if (noAotValidation) {
        if (!writeStr(skippedValidationCode))
            return false;
        return true;
    }

    using namespace QQmlPrivate::AOTLookupValidation;
    if (!writeStr("QQmlPrivate::AOTLookupValidation::LookupSignatures expectedLookupSignatures()\n"
                  "{\n"
                  "    using namespace Qt::StringLiterals;\n"
                  "    using namespace QQmlPrivate::AOTLookupValidation;\n"
                  "    return {\n")) {
        return false;
    }

    const auto &signatures = lookupSignatures.asKeyValueRange();
    QList<std::pair<Lookup, Signature>> sorted{ signatures.begin(), signatures.end() };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const auto &lhs, const auto &rhs) {
        const auto &lTypeString = typeToString(lhs.first.base);
        const auto &rTypeString = typeToString(rhs.first.base);
        if (lTypeString == rTypeString)
            return lhs.first.member < rhs.first.member;
        return lTypeString < rTypeString;
    });

    for (const auto &[memberlookup, signature] : sorted) {
        if (!writeStr("        { %1, %2 },\n"_L1
                              .arg(lookupToString(memberlookup), signatureToString(signature))
                              .toLatin1())) {
            return false;
        }
    }

    if (!writeStr("    };\n}\n"))
        return false;

    const QString validateLookupSignatures = uR"(
bool validateLookupSignatures(QQmlEngine *engine, QV4::CompiledData::CompilationUnit *cu)
{
    enum ValidationState { Pending, Failed, Succeeded };
    static ValidationState state = Pending;
    if (state == Failed)
        return false;
    if (state == Succeeded)
        return true;
    const auto &expectedSignatures = expectedLookupSignatures();
    for (const auto &[lookup, expectedSignature] : expectedSignatures.asKeyValueRange()) {
        if (!QQmlPrivate::AOTLookupValidation::validateLookupSignature(engine, cu, lookup, expectedSignature)) {
            state = Failed;
            return false;
        }
    }
    state = Succeeded;
    return true;
}

)"_s;

    if (!writeStr(validateLookupSignatures.toUtf8()))
        return false;

    return true;
}

bool qSaveQmlJSUnitAsCpp(const QString &inputFileName, const QString &outputFileName,
                         const QV4::CompiledData::SaveableUnitPointer &unit,
                         const QQmlJSAotFunctionMap &aotFunctions,
                         const LookupSignatures &lookupSignatures, bool noAotValidation,
                         QString *errorString)
{
#if QT_CONFIG(temporaryfile)
    QSaveFile f(outputFileName);
#else
    QFile f(outputFileName);
#endif
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *errorString = f.errorString();
        return false;
    }

    auto writeStr = [&f, errorString](const QByteArray &data) {
        if (f.write(data) != data.size()) {
            *errorString = f.errorString();
            return false;
        }
        return true;
    };

    if (!writeStr("// "))
        return false;

    if (!writeStr(inputFileName.toUtf8()))
        return false;

    if (!writeStr("\n"))
        return false;

    if (!writeStr("#include <QtQml/qqmlprivate.h>\n"))
        return false;

    if (!noAotValidation) {
        if (!writeStr("#include <QtCore/qhash.h>\n"))
            return false;
    }

    if (!aotFunctions.isEmpty()) {
        QStringList includes;

        for (const auto &function : aotFunctions)
            includes.append(function.includes);

        std::sort(includes.begin(), includes.end());
        const auto end = std::unique(includes.begin(), includes.end());
        for (auto it = includes.begin(); it != end; ++it) {
            if (!writeStr(QStringLiteral("#include <%1>\n").arg(*it).toUtf8()))
                return false;
        }
    }

    if (!writeStr(QByteArrayLiteral("\nnamespace QmlCacheGeneratedCode {\nnamespace ")))
        return false;

    if (!writeStr(qQmlJSSymbolNamespaceForPath(inputFileName).toUtf8()))
        return false;

    if (!writeStr(" {\n"))
        return false;

    if (!generateAotValidationCode(writeStr, lookupSignatures, noAotValidation))
        return false;

    if (!writeStr(QByteArrayLiteral("extern const unsigned char qmlData alignas(16) [];\n"
                                    "extern const unsigned char qmlData alignas(16) [] = {\n")))
        return false;

    unit.saveToDisk<uchar>([&writeStr](const uchar *begin, quint32 size) {
        QByteArray hexifiedData;
        {
            QTextStream stream(&hexifiedData);
            const uchar *end = begin + size;
            stream << Qt::hex;
            int col = 0;
            for (const uchar *data = begin; data < end; ++data, ++col) {
                if (data > begin)
                    stream << ',';
                if (col % 8 == 0) {
                    stream << '\n';
                    col = 0;
                }
                stream << "0x" << *data;
            }
            stream << '\n';
        }
        return writeStr(hexifiedData);
    });



    if (!writeStr("};\n"))
        return false;

    writeStr(aotFunctions[FileScopeCodeIndex].code.toUtf8().constData());
    if (aotFunctions.size() <= 1) {
        // FileScopeCodeIndex is always there, but it may be the only one.
        writeStr("extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];\n"
                 "extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[] = { { 0, 0, nullptr, nullptr } };\n");
    } else {
        writeStr("extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];\n"
                 "extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[] = {\n");

        QString footer = QStringLiteral("}\n");

        for (QQmlJSAotFunctionMap::ConstIterator func = aotFunctions.constBegin(),
             end = aotFunctions.constEnd();
             func != end; ++func) {

            if (func.key() == FileScopeCodeIndex)
                continue;

            const QString function = QString::fromUtf8(funcHeaderCode) + func.value().code + footer;

            writeStr(QStringLiteral("{ %1, %2, [](QV4::ExecutableCompilationUnit *contextUnit, "
                                    "QMetaType *argTypes) {\n%3}, %4 },")
                     .arg(func.key())
                     .arg(func->numArguments)
                     .arg(func->signature.isEmpty() ? u"    Q_UNUSED(contextUnit);\n    Q_UNUSED(argTypes);\n"_s : func->signature, function)
                     .toUtf8().constData());
        }

        // Conclude the list with a nullptr
        writeStr("{ 0, 0, nullptr, nullptr }");
        writeStr("};\n");
    }

    if (!writeStr("}\n}\n"))
        return false;

#if QT_CONFIG(temporaryfile)
    if (!f.commit()) {
        *errorString = f.errorString();
        return false;
    }
#endif

    return true;
}

QQmlJSAotCompiler::QQmlJSAotCompiler(
        QQmlJSImporter *importer, const QString &resourcePath, const QStringList &qmldirFiles,
        QQmlJSLogger *logger)
    : m_typeResolver(importer)
    , m_resourcePath(resourcePath)
    , m_qmldirFiles(qmldirFiles)
    , m_importer(importer)
    , m_logger(logger)
{
}

void QQmlJSAotCompiler::setDocument(
        const QmlIR::JSCodeGen *codegen, const QmlIR::Document *irDocument)
{
    Q_UNUSED(codegen);
    m_document = irDocument;
    const QFileInfo resourcePathInfo(m_resourcePath);
    if (m_logger->filePath().isEmpty())
        m_logger->setFilePath(resourcePathInfo.fileName());
    m_logger->setCode(irDocument->code);
    m_unitGenerator = &irDocument->jsGenerator;
    QQmlJSImportVisitor visitor(m_importer, m_logger,
                                resourcePathInfo.canonicalPath() + u'/',
                                m_qmldirFiles);
    m_typeResolver.init(&visitor, irDocument->program);
}

void QQmlJSAotCompiler::setScope(const QmlIR::Object *object, const QmlIR::Object *scope)
{
    m_currentObject = object;
    m_currentScope = scope;
}

static bool isStrict(const QmlIR::Document *doc)
{
    for (const QmlIR::Pragma *pragma : doc->pragmas) {
        if (pragma->type == QmlIR::Pragma::Strict)
            return true;
    }
    return false;
}

QQmlJS::DiagnosticMessage QQmlJSAotCompiler::diagnose(
        const QString &message, QtMsgType type, const QQmlJS::SourceLocation &location) const
{
    if (isStrict(m_document)
            && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            && m_logger->categorySeverity(qmlCompiler) == QQmlSA::WarningSeverity::Error) {
        qFatal("%s:%d: (strict mode) %s",
               qPrintable(QFileInfo(m_resourcePath).fileName()),
               location.startLine, qPrintable(message));
    }

    return QQmlJS::DiagnosticMessage {
        message,
        type,
        location
    };
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>> QQmlJSAotCompiler::compileBinding(
        const QV4::Compiler::Context *context, const QmlIR::Binding &irBinding,
        QQmlJS::AST::Node *astNode)
{
    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);

    const QString name = m_document->stringAt(irBinding.propertyNameIndex);
    QQmlJSCompilePass::Function function = initializer.run( context, name, astNode, irBinding);

    const QQmlJSAotFunction aotFunction = doCompileAndRecordAotStats(
            context, &function, name, astNode->firstSourceLocation());

    if (const auto errors = finalizeBindingOrFunction())
        return *errors;

    qCDebug(lcAotCompiler()) << "includes:" << aotFunction.includes;
    qCDebug(lcAotCompiler()) << "binding code:" << aotFunction.code;
    return aotFunction;
}

std::variant<QQmlJSAotFunction, QList<QQmlJS::DiagnosticMessage>> QQmlJSAotCompiler::compileFunction(
        const QV4::Compiler::Context *context, const QString &name, QQmlJS::AST::Node *astNode)
{
    QQmlJSFunctionInitializer initializer(
                &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);
    QQmlJSCompilePass::Function function = initializer.run(context, name, astNode);

    const QQmlJSAotFunction aotFunction = doCompileAndRecordAotStats(
            context, &function, name, astNode->firstSourceLocation());

    if (const auto errors = finalizeBindingOrFunction())
        return *errors;

    qCDebug(lcAotCompiler()) << "includes:" << aotFunction.includes;
    qCDebug(lcAotCompiler()) << "binding code:" << aotFunction.code;
    return aotFunction;
}

QQmlJSAotFunction QQmlJSAotCompiler::globalCode() const
{
    QQmlJSAotFunction global;
    global.includes = {
        u"QtQml/qjsengine.h"_s,
        u"QtQml/qjsprimitivevalue.h"_s,
        u"QtQml/qjsvalue.h"_s,
        u"QtQml/qqmlcomponent.h"_s,
        u"QtQml/qqmlcontext.h"_s,
        u"QtQml/qqmlengine.h"_s,
        u"QtQml/qqmllist.h"_s,

        u"QtCore/qdatetime.h"_s,
        u"QtCore/qtimezone.h"_s,
        u"QtCore/qobject.h"_s,
        u"QtCore/qstring.h"_s,
        u"QtCore/qstringlist.h"_s,
        u"QtCore/qurl.h"_s,
        u"QtCore/qvariant.h"_s,

        u"type_traits"_s
    };
    return global;
}

std::optional<QList<QQmlJS::DiagnosticMessage>> QQmlJSAotCompiler::finalizeBindingOrFunction()
{
    const auto archiveMessages = qScopeGuard([this]() { m_logger->finalizeFunction(); });

    if (!m_logger->currentFunctionHasCompileError())
        return {};

    QList<QQmlJS::DiagnosticMessage> errors;
    m_logger->iterateCurrentFunctionMessages([&](const Message &msg) {
        if (msg.compilationStatus == Message::CompilationStatus::Error)
            errors.append(diagnose(msg.message, msg.type, msg.loc));
    });
    return errors;
}

namespace {
struct InlinedClosures : QQmlJSCompilePass::ClosureSupport
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
 * Sets up the closure with index \a functionIndex for compilation as part of \a outer, where it
 * is called with \a argumentTypes. Returns the context of the closure, or nullptr if it cannot be
 * inlined.
 */
const QV4::Compiler::Context *QQmlJSAotCompiler::initializeClosure(
        int functionIndex, const QQmlJSCompilePass::Function *outer,
        const QList<QQmlJSRegisterContent> &argumentTypes,
        const QQmlJSScope::ConstPtr &returnType, QQmlJSCompilePass::Function *closure)
{
    const auto fail = [&](const QString &message) {
        m_logger->logCompileError(message, QQmlJS::SourceLocation());
        return nullptr;
    };

    const QV4::Compiler::Context *context = m_document->jsModule.functions.value(functionIndex);
    if (!context)
        return fail(u"Cannot find the closure to inline"_s);

    // "this" and "arguments" of other functions are not those of the outer function.
    if (!context->isArrowFunction
            && (context->usesThis || context->innerFunctionAccessesThis
                || context->usesArgumentsObject == QV4::Compiler::Context::UsesArgumentsObject::Used)) {
        return fail(u"Cannot inline a function that uses its own \"this\" or \"arguments\""_s);
    }

    const qsizetype formals = context->arguments.size();
    if (formals > argumentTypes.size())
        return fail(u"Cannot inline a closure that takes more arguments than we can type"_s);

    QQmlJSScope::ConstPtr declaredReturnType;

    // A closure with declared types coerces what it is called with and what it returns. We call
    // it with the types the caller has and take what it returns as it is. That is only the
    // same if the declared types are those types.
    if (QQmlJS::AST::Node *astNode = m_document->jsModule.contextMap.key(
                const_cast<QV4::Compiler::Context *>(context))) {
        if (QQmlJS::AST::FunctionExpression *ast = astNode->asFunctionDefinition()) {
            QQmlJS::AST::BoundNames declared;
            if (ast->formals)
                declared = ast->formals->formals();
            for (qsizetype i = 0; i < declared.size() && i < formals; ++i) {
                if (!declared[i].typeAnnotation)
                    continue;
                const QQmlJSScope::ConstPtr type
                        = m_typeResolver.typeFromAST(declared[i].typeAnnotation->type);
                if (!type || !argumentTypes[i].contains(type)) {
                    return fail(u"Cannot inline a closure whose parameter %1 is declared with "
                                 "another type than it is called with"_s.arg(declared[i].id));
                }
            }

            if (ast->typeAnnotation) {
                const QQmlJSScope::ConstPtr type
                        = m_typeResolver.typeFromAST(ast->typeAnnotation->type);
                if (!type) {
                    return fail(u"Cannot resolve the return type of a closure"_s);
                } else if (!returnType) {
                    // The caller takes what the closure returns. That has to be of the
                    // declared type then. The type propagator checks it.
                    declaredReturnType = type;
                } else if (type != returnType && returnType != m_typeResolver.voidType()) {
                    return fail(u"Cannot inline a closure that is declared to return another "
                                 "type than the caller expects"_s);
                }
            }
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
    closure->argumentTypes = argumentTypes.first(formals);
    // Without a return type, the type propagator infers one. See inferredReturnType.
    if (returnType)
        closure->returnType = m_typeResolver.namedType(returnType);
    else if (declaredReturnType)
        closure->returnType = m_typeResolver.namedType(declaredReturnType);
    for (int i = QQmlJSCompilePass::FirstArgument + formals;
         i < context->registerCountInFunction; ++i) {
        closure->registerTypes.append(m_typeResolver.namedType(m_typeResolver.voidType()));
    }
    closure->code = context->code;
    closure->sourceLocations = context->sourceLocationTable.get();
    closure->isFullyTyped = true;
    return context;
}

/*!
 * \internal
 * Sets up the closure with index \a functionIndex, created by \a outer, for compilation as a
 * function of its own. It becomes a JavaScript function object at run time and can be called by
 * anyone, with anything. So its signature has to be declared: all parameters need type
 * annotations. The return type is inferred if it is not declared; pass it in \a returnType
 * once it is known. Returns the context of the closure, or nullptr if it cannot be compiled.
 */
const QV4::Compiler::Context *QQmlJSAotCompiler::initializeEscapingClosure(
        int functionIndex, const QQmlJSCompilePass::Function *outer,
        const QQmlJSScope::ConstPtr &returnType, QQmlJSCompilePass::Function *closure)
{
    const auto fail = [&](const QString &message) {
        m_logger->logCompileError(message, QQmlJS::SourceLocation());
        return nullptr;
    };

    QV4::Compiler::Context *context = m_document->jsModule.functions.value(functionIndex);
    if (!context)
        return fail(u"Cannot find the closure to compile"_s);

    if (context->isGenerator)
        return fail(u"Cannot compile a generator function as closure"_s);

    // We don't know what "this" is when someone else calls the function object.
    if (context->usesThis || context->innerFunctionAccessesThis
            || context->usesArgumentsObject == QV4::Compiler::Context::UsesArgumentsObject::Used) {
        return fail(u"Cannot compile a closure that uses \"this\" or \"arguments\""_s);
    }

    QQmlJS::AST::Node *astNode = m_document->jsModule.contextMap.key(context);
    if (!astNode || !astNode->asFunctionDefinition())
        return fail(u"Cannot find the definition of the closure to compile"_s);

    // A function with a name can call itself, with anything. Otherwise, if it is passed to
    // something that calls it with known types, it does not have to declare them.
    QList<QQmlJSScope::ConstPtr> contextualArgumentTypes;
    if (context->isArrowFunction || astNode->asFunctionDefinition()->name.isEmpty()) {
        contextualArgumentTypes
                = outer->closureSupport->contextualArgumentTypes.value(functionIndex);
    }

    // This reports the remaining parameters without type annotation as errors.
    QQmlJSFunctionInitializer initializer(
            &m_typeResolver, m_currentObject->location, m_currentScope->location, m_logger);
    *closure = initializer.run(context, context->name, astNode, contextualArgumentTypes);
    if (m_logger->currentFunctionHasErrorOrSkip())
        return nullptr;

    closure->isFullyTyped = true;
    if (!closure->returnType.isValid() && returnType)
        closure->returnType = m_typeResolver.namedType(returnType);

    closure->closureSupport = outer->closureSupport;
    closure->identity = context;
    closure->contextChain = outer->contextChain;
    if (context->requiresExecutionContext) {
        closure->contextChain.prepend(context);
        closure->ownsContext = true;
        if (context->argumentsCanEscape)
            closure->firstArgumentLocal = context->locals.size();
    }
    return context;
}

/*!
 * \internal
 * Propagates the types of the closures \a function creates as function objects, and of the
 * closures those create. This may change the types of the context locals.
 */
bool QQmlJSAotCompiler::analyzeEscapingClosures(const QQmlJSCompilePass::Function *function)
{
    QQmlJSCompilePass::ClosureSupport *closureSupport = function->closureSupport;

    QList<int> escaping;
    for (auto it = closureSupport->loadedClosures.constBegin(),
              end = closureSupport->loadedClosures.constEnd(); it != end; ++it) {
        if (it.key().first == function->identity
                && !closureSupport->inlinedLoads.contains(it.key())
                && !escaping.contains(it.value())) {
            escaping.append(it.value());
        }
    }
    std::sort(escaping.begin(), escaping.end());

    for (int functionIndex : std::as_const(escaping)) {
        // The function object is bound to the contexts this function runs in.
        for (const void *context : function->contextChain)
            closureSupport->realContexts.insert(context);

        QQmlJSCompilePass::Function closure;
        const QV4::Compiler::Context *closureContext = initializeEscapingClosure(
                functionIndex, function, QQmlJSScope::ConstPtr(), &closure);
        if (!closureContext)
            return false;

        // The closure may create further closures. So, don't hold on to its entry.
        QQmlJSScope::ConstPtr inferredReturnType
                = closureSupport->escapingClosures.value(functionIndex);
        const bool infersReturnType = !closure.returnType.isValid();
        if (infersReturnType)
            closure.inferredReturnType = &inferredReturnType;

        bool basicBlocksValidationFailed = false;
        QQmlJSBasicBlocks basicBlocks(closureContext, m_unitGenerator, &m_typeResolver, m_logger);
        auto passResult = basicBlocks.run(&closure, m_flags, basicBlocksValidationFailed);
        QQmlJSTypePropagator propagator(
                m_unitGenerator, &m_typeResolver, m_logger, passResult.basicBlocks,
                passResult.annotations);
        propagator.run(&closure);
        if (m_logger->currentFunctionHasErrorOrSkip())
            return false;

        closureSupport->escapingClosures[functionIndex] = infersReturnType
                ? (inferredReturnType ? inferredReturnType : m_typeResolver.voidType())
                : closure.returnType.containedType();

        if (!analyzeEscapingClosures(&closure))
            return false;
    }

    return true;
}

/*!
 * \internal
 * Runs all passes on \a function and generates the code for it. If it has closures we can
 * inline, their code is generated first, and becomes part of the code for \a function.
 */
QQmlJSAotFunction QQmlJSAotCompiler::compilePasses(
        const QV4::Compiler::Context *context, const QQmlJSCompilePass::Function *function)
{
    QQmlJSCompilePass::ClosureSupport *closureSupport = function->closureSupport;
    Q_ASSERT(closureSupport);

    bool basicBlocksValidationFailed = false;
    QQmlJSBasicBlocks basicBlocks(context, m_unitGenerator, &m_typeResolver, m_logger);
    QQmlJSCompilePass::BlocksAndAnnotations passResult;
    auto &[blocks, annotations] = passResult;

    // The types of the locals in the call context depend on what the function and its closures
    // store in them. Propagate the types until they don't change anymore. Types are only ever
    // merged into more general ones. So this terminates.
    do {
        closureSupport->localTypesChanged = false;
        passResult = basicBlocks.run(function, m_flags, basicBlocksValidationFailed);
        QQmlJSTypePropagator propagator(
                m_unitGenerator, &m_typeResolver, m_logger, blocks, annotations);
        passResult = propagator.run(function);
        if (m_logger->currentFunctionHasErrorOrSkip())
            return QQmlJSAotFunction();

        // The closures that are not inlined store into the locals, too.
        if (!analyzeEscapingClosures(function))
            return QQmlJSAotFunction();
    } while (closureSupport->localTypesChanged);

    QQmlJSShadowCheck shadowCheck(
            m_unitGenerator, &m_typeResolver, m_logger, blocks, annotations);
    passResult = shadowCheck.run(function);
    if (m_logger->currentFunctionHasErrorOrSkip())
        return QQmlJSAotFunction();

    QQmlJSOptimizations optimizer(
            m_unitGenerator, &m_typeResolver, m_logger, blocks, annotations,
            basicBlocks.objectAndArrayDefinitions());
    passResult = optimizer.run(function);
    if (m_logger->currentFunctionHasErrorOrSkip())
        return QQmlJSAotFunction();

    QQmlJSStorageInitializer initializer(
            m_unitGenerator, &m_typeResolver, m_logger, blocks, annotations);
    passResult = initializer.run(function);

    // Generalize all arguments, registers, and the return type.
    QQmlJSStorageGeneralizer generalizer(
            m_unitGenerator, &m_typeResolver, m_logger, blocks, annotations);
    passResult = generalizer.run(function);
    if (m_logger->currentFunctionHasErrorOrSkip())
        return QQmlJSAotFunction();

    // Generate the code of the closures this function calls inline. The passes above have
    // only propagated their types. Now the types of the locals are final.
    QList<int> closures;
    for (auto it = closureSupport->inlinedCalls.constBegin(),
              end = closureSupport->inlinedCalls.constEnd(); it != end; ++it) {
        if (it.key().first == function->identity && !closures.contains(it.value()))
            closures.append(it.value());
    }
    std::sort(closures.begin(), closures.end());

    for (int functionIndex : std::as_const(closures)) {
        const QQmlJSCompilePass::ClosureSupport::Closure analyzed
                = closureSupport->closures.value(functionIndex);
        if (!analyzed.returnType) {
            m_logger->logCompileError(
                    u"Cannot determine the return type of a closure"_s, QQmlJS::SourceLocation());
            return QQmlJSAotFunction();
        }

        QQmlJSCompilePass::Function closure;
        const QV4::Compiler::Context *closureContext = initializeClosure(
                functionIndex, function, analyzed.argumentTypes, analyzed.returnType, &closure);
        if (!closureContext)
            return QQmlJSAotFunction();

        const QQmlJSAotFunction compiled = compilePasses(closureContext, &closure);
        if (m_logger->currentFunctionHasErrorOrSkip())
            return QQmlJSAotFunction();

        if (closureSupport->localTypesChanged) {
            m_logger->logCompileError(
                    u"Types of the locals changed while generating code for a closure"_s,
                    QQmlJS::SourceLocation());
            return QQmlJSAotFunction();
        }

        QQmlJSCompilePass::ClosureSupport::Closure &result
                = closureSupport->closures[functionIndex];
        result.code = compiled.code;
        result.includes = compiled.includes;
        result.argumentStorage.clear();
        for (QQmlJSRegisterContent argument : std::as_const(closure.argumentTypes)) {
            result.argumentStorage.append(
                    m_typeResolver.original(argument.storage()).containedType());
        }
        result.returnStorage = closure.returnType.storedType();
    }

    // Compile the closures this function creates as function objects. Each is a function of
    // its own in the compilation unit.
    QList<int> escaping;
    for (auto it = closureSupport->loadedClosures.constBegin(),
              end = closureSupport->loadedClosures.constEnd(); it != end; ++it) {
        if (it.key().first == function->identity
                && !closureSupport->inlinedLoads.contains(it.key())
                && !escaping.contains(it.value())) {
            escaping.append(it.value());
        }
    }
    std::sort(escaping.begin(), escaping.end());

    for (int functionIndex : std::as_const(escaping)) {
        QQmlJSCompilePass::Function closure;
        const QV4::Compiler::Context *closureContext = initializeEscapingClosure(
                functionIndex, function, closureSupport->escapingClosures.value(functionIndex),
                &closure);
        if (!closureContext)
            return QQmlJSAotFunction();

        QQmlJSAotFunction compiled = compilePasses(closureContext, &closure);
        if (m_logger->currentFunctionHasErrorOrSkip())
            return QQmlJSAotFunction();

        if (closureSupport->localTypesChanged) {
            m_logger->logCompileError(
                    u"Types of the locals changed while generating code for a closure"_s,
                    QQmlJS::SourceLocation());
            return QQmlJSAotFunction();
        }

        m_closureFunctions.insert(functionIndex, std::move(compiled));
    }

    QQmlJSCodeGenerator codegen(context, m_unitGenerator, &m_typeResolver, m_logger, blocks,
                                annotations, noAotValidation());
    QQmlJSAotFunction result = codegen.run(function, basicBlocksValidationFailed);
    if (m_logger->currentFunctionHasErrorOrSkip())
        return QQmlJSAotFunction();

    m_lookupSignatures.insert(codegen.lookupSignatures());
    return result;
}

QQmlJSAotFunction QQmlJSAotCompiler::doCompile(
        const QV4::Compiler::Context *context, const QQmlJSCompilePass::Function *function)
{
    m_closureFunctions.clear();
    if (m_logger->currentFunctionHasErrorOrSkip())
        return QQmlJSAotFunction();

    InlinedClosures closureSupport;
    closureSupport.analyze = [this, &closureSupport](
                                     int functionIndex, const QQmlJSCompilePass::Function *outer,
                                     const QList<QQmlJSRegisterContent> &argumentTypes,
                                     const QQmlJSScope::ConstPtr &returnType) {
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
        const auto recordTypes = qScopeGuard([&]() {
            QQmlJSCompilePass::ClosureSupport::Closure &analyzed
                    = closureSupport.closures[functionIndex];
            analyzed.argumentTypes = closure.argumentTypes;
            analyzed.returnType = knownReturnType ? knownReturnType : inferredReturnType;
        });

        bool basicBlocksValidationFailed = false;
        QQmlJSBasicBlocks basicBlocks(closureContext, m_unitGenerator, &m_typeResolver, m_logger);
        auto passResult = basicBlocks.run(&closure, m_flags, basicBlocksValidationFailed);
        QQmlJSTypePropagator propagator(
                m_unitGenerator, &m_typeResolver, m_logger, passResult.basicBlocks,
                passResult.annotations);
        propagator.run(&closure);
        return !m_logger->currentFunctionHasErrorOrSkip();
    };

    QQmlJSCompilePass::Function withClosures = *function;
    withClosures.closureSupport = &closureSupport;
    withClosures.identity = context;
    if (context->requiresExecutionContext) {
        withClosures.contextChain = { context };
        withClosures.ownsContext = true;
        if (context->argumentsCanEscape)
            withClosures.firstArgumentLocal = context->locals.size();
    }

    QQmlJSAotFunction result = compilePasses(context, &withClosures);

    // The code of a closure is only good together with the code of the function that creates
    // it: they agree on the types of the captured variables.
    if (m_logger->currentFunctionHasErrorOrSkip())
        m_closureFunctions.clear();
    return result;
}

QQmlJSAotFunction QQmlJSAotCompiler::doCompileAndRecordAotStats(
        const QV4::Compiler::Context *context, const QQmlJSCompilePass::Function *function,
        const QString &name, QQmlJS::SourceLocation location)
{
    QElapsedTimer timer {};
    timer.start();
    QQmlJSAotFunction result;
    if (!m_logger->currentFunctionHasCompileError())
        result = doCompile(context, function);
    auto elapsed = std::chrono::milliseconds { timer.elapsed() };

    if (QQmlJS::QQmlJSAotCompilerStats::recordAotStats()) {
        QQmlJS::AotStatsEntry entry;
        entry.codegenDuration = elapsed;
        entry.functionName = name;
        entry.message = m_logger->currentFunctionWasSkipped()
                ? m_logger->currentFunctionCompileSkipMessage()
                : m_logger->currentFunctionCompileErrorMessage();
        entry.line = location.startLine;
        entry.column = location.startColumn;
        if (m_logger->currentFunctionWasSkipped())
            entry.codegenResult = QQmlJS::CodegenResult::Skip;
        else if (m_logger->currentFunctionHasCompileError())
            entry.codegenResult = QQmlJS::CodegenResult::Failure;
        else
            entry.codegenResult = QQmlJS::CodegenResult::Success;
        QQmlJS::QQmlJSAotCompilerStats::addEntry(
                function->qmlScope.containedType()->filePath(), entry);
    }

    if (m_logger->currentFunctionWasSkipped())
        result.skipReason = m_logger->currentFunctionCompileSkipMessage();

    return result;
}

QT_END_NAMESPACE
