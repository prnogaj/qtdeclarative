// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant

#ifndef QQMLJSCOMPILEPASS_P_H
#define QQMLJSCOMPILEPASS_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.


#include <private/qqmljslogger_p.h>
#include <private/qqmljsregistercontent_p.h>
#include <private/qqmljsscope_p.h>
#include <private/qqmljstyperesolver_p.h>
#include <private/qv4bytecodehandler_p.h>
#include <private/qv4compiler_p.h>
#include <private/qflatmap_p.h>

QT_BEGIN_NAMESPACE

class QQmlJSCompilePass : public QV4::Moth::ByteCodeHandler
{
    Q_DISABLE_COPY_MOVE(QQmlJSCompilePass)
public:
    enum RegisterShortcuts {
        InvalidRegister = -1,

        // TODO: Should be called "Function", requires refactoring
        CurrentFunction = QV4::CallData::Function,

        Context = QV4::CallData::Context,
        Accumulator = QV4::CallData::Accumulator,
        This = QV4::CallData::This,
        NewTarget = QV4::CallData::NewTarget,
        Argc = QV4::CallData::Argc,
        FirstArgument = QV4::CallData::OffsetCount
    };

    using SourceLocationTable = QV4::Compiler::Context::SourceLocationTable;

    struct VirtualRegister
    {
        QQmlJSRegisterContent content;
        bool canMove = false;
        bool affectedBySideEffects = false;
        bool isShadowable = false;

    private:
        friend bool operator==(const VirtualRegister &a, const VirtualRegister &b)
        {
            return a.content == b.content && a.canMove == b.canMove
                && a.affectedBySideEffects == b.affectedBySideEffects;
        }
    };

    // map from register index to expected type
    using VirtualRegisters = QFlatMap<int, VirtualRegister>;

    struct BasicBlock
    {
        QList<int> jumpOrigins;
        QList<int> readRegisters;
        int jumpTarget = -1;
        bool jumpIsUnconditional = false;
        bool isReturnBlock = false;
        bool isThrowBlock = false;
    };

    using BasicBlocks = QFlatMap<int, BasicBlock>;

    struct InstructionAnnotation
    {
        // Registers explicit read as part of the instruction.
        VirtualRegisters readRegisters;

        // Registers that have to be converted for future instructions after a jump.
        VirtualRegisters typeConversions;

        QQmlJSRegisterContent changedRegister;
        int changedRegisterIndex = InvalidRegister;
        bool hasInternalSideEffects = false;
        bool hasExternalSideEffects = false;
        bool isRename = false;
        bool isShadowable = false;
    };

    using InstructionAnnotations = QFlatMap<int, InstructionAnnotation>;
    struct BlocksAndAnnotations
    {
        BasicBlocks basicBlocks;
        InstructionAnnotations annotations;
    };

    struct Function;

    // What the passes of a function and of the closures inlined into it share.
    //
    // A closure that is passed directly to a method we generate inline code for is compiled
    // into a C++ lambda inside the function that creates it. The variables it captures are
    // locals of the call contexts of the functions around it. They are C++ variables, declared
    // where the context is created: in the function body, or in the lambda of a closure that
    // needs a context of its own because its variables are captured in turn.
    struct ClosureSupport
    {
        struct Closure
        {
            // Types of the arguments the closure is called with
            QList<QQmlJSRegisterContent> argumentTypes;

            // What the caller converts the result to (void if it ignores it), or what the
            // closure returns if the caller takes whatever that is.
            QQmlJSScope::ConstPtr returnType;

            // Filled in before the code for the outer function is generated
            QList<QQmlJSScope::ConstPtr> argumentStorage;
            QQmlJSScope::ConstPtr returnStorage;
            QString code;
            QStringList includes;
        };

        virtual ~ClosureSupport() = default;

        // The linter only wants to know the types. It does not care whether code can be
        // generated for what it finds, so the restrictions that exist for the sake of the
        // generated code do not apply to it.
        bool analysisOnly = false;

        // Run the type propagation on the closure with the given index, as it would be called
        // with the given arguments. This may change localTypes. If returnType is null, the
        // return type is inferred from what the closure returns. Either way, it is stored in
        // closures afterwards.
        virtual bool analyzeClosure(
                int functionIndex, const Function *outer,
                const QList<QQmlJSRegisterContent> &argumentTypes,
                const QQmlJSScope::ConstPtr &returnType) = 0;

        // A local of a JavaScript context: the context and the index in it
        using Local = std::pair<const void *, int>;

        // The name of the C++ variable that holds the local
        QString localName(const Local &local)
        {
            auto it = contextNumbers.find(local.first);
            if (it == contextNumbers.end())
                it = contextNumbers.insert(local.first, contextNumbers.size());
            return QStringLiteral("c%1_local%2").arg(*it).arg(local.second);
        }

        // Contained types of the locals of the call contexts
        QHash<Local, QQmlJSScope::ConstPtr> localTypes;
        bool localTypesChanged = false;
        QHash<const void *, int> contextNumbers;

        // Closures to be inlined, by function index
        QHash<int, Closure> closures;

        // LoadClosure instructions whose result is only used for inlining:
        // (identity of the function, instruction offset) -> function index of the closure
        QHash<std::pair<const void *, int>, int> inlinedLoads;

        // The same for the instructions that call the closure
        QHash<std::pair<const void *, int>, int> inlinedCalls;

        // All LoadClosure instructions we have seen, in the same form. Those that are not
        // inlined create JavaScript function objects: The closure escapes.
        QHash<std::pair<const void *, int>, int> loadedClosures;

        // The closures that escape, by function index, with their return types. They are
        // compiled as functions of their own.
        QHash<int, QQmlJSScope::ConstPtr> escapingClosures;

        // What we know about the arguments of closures that escape from where they are
        // passed to, by function index. Used for parameters without type annotation.
        QHash<int, QList<QQmlJSScope::ConstPtr>> contextualArgumentTypes;

        // Instructions that change a value type or a list of values loaded from a local of a
        // context: (identity of the function, instruction offset) -> (scope, index) of the
        // local. A local holds a copy, and what is loaded from it is a copy of that. So the
        // changed value has to be stored in the local again.
        QHash<std::pair<const void *, int>, std::pair<int, int>> localWriteBacks;

        // The contexts a closure that escapes is created in. Their locals have to live where a
        // function object can find them: in JavaScript contexts, not in C++ variables. The
        // other contexts, those that only inlined closures see, do not exist at run time.
        QSet<const void *> realContexts;
    };

    struct Function
    {
        ClosureSupport *closureSupport = nullptr;
        const void *identity = nullptr;
        bool isInlinedClosure = false;

        // The JavaScript contexts the function runs in, innermost first. The first one is
        // the function's own if ownsContext is set. Otherwise the function has none, and the
        // list starts with the context of the function around it.
        QList<const void *> contextChain;
        bool ownsContext = false;

        // If the arguments are captured by a closure, they are locals of the function's own
        // context, too, starting at this index.
        int firstArgumentLocal = -1;

        // If set, the function has no return type yet. The type propagator merges the types of
        // all returned values into this.
        QQmlJSScope::ConstPtr *inferredReturnType = nullptr;

        QQmlJSScopesById addressableScopes;
        QList<QQmlJSRegisterContent> argumentTypes;
        QList<QQmlJSRegisterContent> registerTypes;
        QQmlJSRegisterContent returnType;
        QQmlJSRegisterContent qmlScope;
        QByteArray code;
        const SourceLocationTable *sourceLocations = nullptr;
        bool isSignalHandler = false;
        bool isQPropertyBinding = false;
        bool isProperty = false;
        bool isFullyTyped = false;
    };

    struct ObjectOrArrayDefinition
    {
        enum {
            ArrayClassId = -1,
            ArrayConstruct1ArgId = -2,
        };

        int instructionOffset = -1;
        int internalClassId = ArrayClassId;
        int argc = 0;
        int argv = -1;
    };

    struct State
    {
        VirtualRegisters registers;
        VirtualRegisters lookups;

        /*!
            \internal
            \brief The accumulatorIn is the input register of the current instruction.

            It holds a content, a type that content is acctually stored in, and an enclosing type
            of the stored type called the scope. Note that passes after the original type
            propagation may change the type of this register to a different type that the original
            one can be coerced to. Therefore, when analyzing the same instruction in a later pass,
            the type may differ from what was seen or requested ealier. See \l {readAccumulator()}.
            The input type may then need to be converted to the expected type.
        */
        QQmlJSRegisterContent accumulatorIn() const
        {
            auto it = registers.find(Accumulator);
            Q_ASSERT(it != registers.end());
            return it.value().content;
        };

        /*!
            \internal
            \brief The accumulatorOut is the output register of the current instruction.
        */
        QQmlJSRegisterContent accumulatorOut() const
        {
            Q_ASSERT(m_changedRegisterIndex == Accumulator);
            return m_changedRegister;
        };

        void setRegister(int registerIndex, QQmlJSRegisterContent content)
        {
            const int lookupIndex = content.resultLookupIndex();
            if (lookupIndex != QQmlJSRegisterContent::InvalidLookupIndex)
                lookups[lookupIndex] = { content, false, false };

            m_changedRegister = std::move(content);
            m_changedRegisterIndex = registerIndex;
        }

        void clearChangedRegister()
        {
            m_changedRegisterIndex = InvalidRegister;
            m_changedRegister = QQmlJSRegisterContent();
        }

        int changedRegisterIndex() const { return m_changedRegisterIndex; }
        QQmlJSRegisterContent changedRegister() const { return m_changedRegister; }

        void addReadRegister(int registerIndex, QQmlJSRegisterContent reg)
        {
            VirtualRegister &target = m_readRegisters[registerIndex];
            target.content = reg;

            const auto source = registers.find(registerIndex);
            if (source == registers.end()) {
                target.canMove = false;
                target.affectedBySideEffects = false;
            } else {
                target.canMove = source->second.canMove;
                target.affectedBySideEffects = source->second.affectedBySideEffects;
            }
        }

        void addReadAccumulator(QQmlJSRegisterContent reg)
        {
            addReadRegister(Accumulator, reg);
        }

        VirtualRegisters takeReadRegisters() const { return std::move(m_readRegisters); }
        void setReadRegisters(VirtualRegisters readReagisters)
        {
            m_readRegisters = std::move(readReagisters);
        }

        QQmlJSRegisterContent readRegister(int registerIndex) const
        {
            Q_ASSERT(m_readRegisters.contains(registerIndex));
            return m_readRegisters[registerIndex].content;
        }

        bool canMoveReadRegister(int registerIndex) const
        {
            auto it = m_readRegisters.find(registerIndex);
            return it != m_readRegisters.end() && it->second.canMove;
        }

        bool isRegisterAffectedBySideEffects(int registerIndex) const
        {
            auto it = m_readRegisters.find(registerIndex);
            return it != m_readRegisters.end() && it->second.affectedBySideEffects;
        }

        /*!
            \internal
            \brief The readAccumulator is the register content expected by the current instruction.

            It may differ from the actual input type of the accumulatorIn register and usage of the
            value may require a conversion.
        */
        QQmlJSRegisterContent readAccumulator() const
        {
            return readRegister(Accumulator);
        }

        bool readsRegister(int registerIndex) const
        {
            return m_readRegisters.contains(registerIndex);
        }

        bool hasInternalSideEffects() const { return m_hasInternalSideEffects; }
        bool hasExternalSideEffects() const { return m_hasExternalSideEffects; }

        void resetSideEffects()
        {
            m_hasInternalSideEffects = false;
            m_hasExternalSideEffects = false;
        }

        void applyExternalSideEffects(bool hasExternalSideEffects)
        {
            if (!hasExternalSideEffects)
                return;

            for (auto it = registers.begin(), end = registers.end(); it != end; ++it)
                it.value().affectedBySideEffects = true;

            for (auto it = lookups.begin(), end = lookups.end(); it != end; ++it)
                it.value().affectedBySideEffects = true;
        }

        void setHasInternalSideEffects() { m_hasInternalSideEffects = true; }
        void setHasExternalSideEffects()
        {
            m_hasExternalSideEffects = true;
            m_hasInternalSideEffects = true;
            applyExternalSideEffects(true);
        }


        bool isRename() const { return m_isRename; }
        void setIsRename(bool isRename) { m_isRename = isRename; }

        bool isShadowable() const { return m_isShadowable; }
        void setIsShadowable(bool isShadowable) { m_isShadowable = isShadowable; }

        int renameSourceRegisterIndex() const
        {
            Q_ASSERT(m_isRename);
            Q_ASSERT(m_readRegisters.size() == 1);
            return m_readRegisters.begin().key();
        }

        void applyAnnotation(const InstructionAnnotation &annotation)
        {
            m_readRegisters = annotation.readRegisters;

            m_hasInternalSideEffects = annotation.hasInternalSideEffects;
            m_hasExternalSideEffects = annotation.hasExternalSideEffects;
            m_isRename = annotation.isRename;
            m_isShadowable = annotation.isShadowable;

            for (auto it = annotation.typeConversions.constBegin(),
                 end = annotation.typeConversions.constEnd(); it != end; ++it) {
                Q_ASSERT(it.key() != InvalidRegister);
                registers[it.key()] = it.value();
            }

            if (annotation.changedRegisterIndex != InvalidRegister)
                setRegister(annotation.changedRegisterIndex, annotation.changedRegister);
        }

    private:
        VirtualRegisters m_readRegisters;
        QQmlJSRegisterContent m_changedRegister;
        int m_changedRegisterIndex = InvalidRegister;

        // If the instruction's value is unused, we still cannot optimize it out.
        bool m_hasInternalSideEffects = false;

        // Side effect created by calls to other functions or writes to properties,
        // affects tracked value types and lists. Implies the effects of Internal.
        bool m_hasExternalSideEffects = false;

        bool m_isRename = false;
        bool m_isShadowable = false;
    };

    QQmlJSCompilePass(const QV4::Compiler::JSUnitGenerator *jsUnitGenerator,
                      const QQmlJSTypeResolver *typeResolver, QQmlJSLogger *logger,
                      const BasicBlocks &basicBlocks = {},
                      const InstructionAnnotations &annotations = {})
        : m_jsUnitGenerator(jsUnitGenerator)
        , m_typeResolver(typeResolver)
        , m_pool(typeResolver->registerContentPool())
        , m_logger(logger)
        , m_basicBlocks(basicBlocks)
        , m_annotations(annotations)
    {}

protected:
    const QV4::Compiler::JSUnitGenerator *m_jsUnitGenerator = nullptr;
    const QQmlJSTypeResolver *m_typeResolver = nullptr;
    QQmlJSRegisterContentPool *m_pool = nullptr;
    QQmlJSLogger *m_logger = nullptr;

    const Function *m_function = nullptr;
    BasicBlocks m_basicBlocks;
    InstructionAnnotations m_annotations;

    int firstRegisterIndex() const
    {
        return FirstArgument + m_function->argumentTypes.size();
    }

    bool isArgument(int registerIndex) const
    {
        return registerIndex >= FirstArgument && registerIndex < firstRegisterIndex();
    }

    QQmlJSRegisterContent argumentType(int registerIndex) const
    {
        Q_ASSERT(isArgument(registerIndex));
        return m_function->argumentTypes[registerIndex - FirstArgument];
    }

    /*!
     * \internal
     * Determines whether this is the _QML_ scope object
     * (in contrast to the JavaScript global or some other scope).
     *
     * We omit any module prefixes seen on top of the object.
     * The module prefixes don't actually add anything unless they
     * are the prefix to an attachment.
     */
    bool isQmlScopeObject(QQmlJSRegisterContent content)
    {
        switch (content.variant()) {
        case QQmlJSRegisterContent::ScopeObject:
            return content.contains(m_function->qmlScope.containedType());
        case QQmlJSRegisterContent::ModulePrefix:
            return content.scope().contains(m_function->qmlScope.containedType());
        default:
            break;
        }

        return false;
    }

    State initialState(const Function *function)
    {
        State state;
        for (int i = 0, end = function->argumentTypes.size(); i < end; ++i) {
            state.registers[FirstArgument + i].content = function->argumentTypes.at(i);
            Q_ASSERT(state.registers[FirstArgument + i].content.isValid());
        }
        for (int i = 0, end = function->registerTypes.size(); i != end; ++i)
            state.registers[firstRegisterIndex() + i].content = function->registerTypes[i];
        return state;
    }

    State nextStateFromAnnotations(
            const State &oldState, const InstructionAnnotations &annotations)
    {
        State newState;

        const auto instruction = annotations.find(currentInstructionOffset());
        newState.registers = oldState.registers;
        newState.lookups = oldState.lookups;

        // Usually the initial accumulator type is the output of the previous instruction, but ...
        if (oldState.changedRegisterIndex() != InvalidRegister) {
            newState.registers[oldState.changedRegisterIndex()].affectedBySideEffects = false;
            newState.registers[oldState.changedRegisterIndex()].content
                    = oldState.changedRegister();
            newState.registers[oldState.changedRegisterIndex()].isShadowable
                    = oldState.isShadowable();
        }

        // Side effects are applied at the end of an instruction: An instruction with side
        // effects can still read its registers before the side effects happen.
        newState.applyExternalSideEffects(oldState.hasExternalSideEffects());

        if (instruction != annotations.constEnd())
            newState.applyAnnotation(instruction->second);

        return newState;
    }

    QList<SourceLocationTable::Entry>::const_iterator sourceLocationEntry(
            int instructionOffset) const
    {
        Q_ASSERT(m_function);
        Q_ASSERT(m_function->sourceLocations);
        const auto &entries = m_function->sourceLocations->entries;
        const auto entry = std::lower_bound(
                entries.begin(), entries.end(), instructionOffset,
                [](auto entry, uint offset) { return entry.offset < offset; });
        Q_ASSERT(entry != entries.end());
        return entry;
    }

    QQmlJS::SourceLocation sourceLocation(int instructionOffset) const
    {
        return sourceLocationEntry(instructionOffset)->location;
    }

    QQmlJS::SourceLocation nonEmptySourceLocation(int instructionOffset) const
    {
        auto entry = sourceLocationEntry(instructionOffset);

        // filter out empty locations
        const auto begin = m_function->sourceLocations->entries.begin();
        while (entry->location.length == 0 && entry != begin)
            --entry;

        return entry->location;
    }

    QQmlJS::SourceLocation currentSourceLocation() const
    {
        return sourceLocation(currentInstructionOffset());
    }

    QQmlJS::SourceLocation currentNonEmptySourceLocation() const
    {
        return nonEmptySourceLocation(currentInstructionOffset());
    }

    QQmlJS::SourceLocation currentFunctionSourceLocation() const
    {
        Q_ASSERT(m_function->sourceLocations);
        const auto &entries = m_function->sourceLocations->entries;

        Q_ASSERT(!entries.isEmpty());
        return combine(entries.constFirst().location, entries.constLast().location);
    }

    void addError(const QString &message, int instructionOffset)
    {
        m_logger->logCompileError(message, sourceLocation(instructionOffset));
    }

    void addSkip(const QString &message, int instructionOffset)
    {
        m_logger->logCompileSkip(message, sourceLocation(instructionOffset));
    }

    void addError(const QString &message)
    {
        addError(message, currentInstructionOffset());
    }

    void addSkip(const QString &message)
    {
        addSkip(message, currentInstructionOffset());
    }

    static bool instructionManipulatesContext(QV4::Moth::Instr::Type type)
    {
        using Type = QV4::Moth::Instr::Type;
        switch (type) {
        case Type::PopContext:
        case Type::PopScriptContext:
        case Type::CreateCallContext:
        case Type::CreateCallContext_Wide:
        case Type::PushCatchContext:
        case Type::PushCatchContext_Wide:
        case Type::PushWithContext:
        case Type::PushWithContext_Wide:
        case Type::PushBlockContext:
        case Type::PushBlockContext_Wide:
        case Type::CloneBlockContext:
        case Type::CloneBlockContext_Wide:
        case Type::PushScriptContext:
        case Type::PushScriptContext_Wide:
            return true;
        default:
            break;
        }
        return false;
    }

    // Stub out all the methods so that passes can choose to only implement part of them.
    void generate_Add(int) override {}
    void generate_As(int) override {}
    void generate_BitAnd(int) override {}
    void generate_BitAndConst(int) override {}
    void generate_BitOr(int) override {}
    void generate_BitOrConst(int) override {}
    void generate_BitXor(int) override {}
    void generate_BitXorConst(int) override {}
    void generate_CallGlobalLookup(int, int, int) override {}
    void generate_CallName(int, int, int) override {}
    void generate_CallPossiblyDirectEval(int, int) override {}
    void generate_CallProperty(int, int, int, int) override {}
    void generate_CallPropertyLookup(int, int, int, int) override {}
    void generate_CallQmlContextPropertyLookup(int, int, int) override {}
    void generate_CallValue(int, int, int) override {}
    void generate_CallWithReceiver(int, int, int, int) override {}
    void generate_CallWithSpread(int, int, int, int) override {}
    void generate_CheckException() override {}
    void generate_CloneBlockContext() override {}
    void generate_CmpEq(int) override {}
    void generate_CmpEqInt(int) override {}
    void generate_CmpEqNull() override {}
    void generate_CmpGe(int) override {}
    void generate_CmpGt(int) override {}
    void generate_CmpIn(int) override {}
    void generate_CmpInstanceOf(int) override {}
    void generate_CmpLe(int) override {}
    void generate_CmpLt(int) override {}
    void generate_CmpNe(int) override {}
    void generate_CmpNeInt(int) override {}
    void generate_CmpNeNull() override {}
    void generate_CmpStrictEqual(int) override {}
    void generate_CmpStrictNotEqual(int) override {}
    void generate_Construct(int, int, int) override {}
    void generate_ConstructWithSpread(int, int, int) override {}
    void generate_ConvertThisToObject() override {}
    void generate_CreateCallContext() override {}
    void generate_CreateClass(int, int, int) override {}
    void generate_CreateMappedArgumentsObject() override {}
    void generate_CreateRestParameter(int) override {}
    void generate_CreateUnmappedArgumentsObject() override {}
    void generate_DeadTemporalZoneCheck(int) override {}
    void generate_Debug() override {}
    void generate_DeclareVar(int, int) override {}
    void generate_Decrement() override {}
    void generate_DefineArray(int, int) override {}
    void generate_DefineObjectLiteral(int, int, int) override {}
    void generate_DeleteName(int) override {}
    void generate_DeleteProperty(int, int) override {}
    void generate_DestructureRestElement() override {}
    void generate_Div(int) override {}
    void generate_Exp(int) override {}
    void generate_GetException() override {}
    void generate_GetIterator(int) override {}
    void generate_GetLookup(int) override {}
    void generate_GetOptionalLookup(int, int) override {}
    void generate_GetTemplateObject(int) override {}
    void generate_Increment() override {}
    void generate_InitializeBlockDeadTemporalZone(int, int) override {}
    void generate_IteratorClose() override {}
    void generate_IteratorNext(int, int) override {}
    void generate_IteratorNextForYieldStar(int, int, int) override {}
    void generate_Jump(int) override {}
    void generate_JumpFalse(int) override {}
    void generate_JumpNoException(int) override {}
    void generate_JumpNotUndefined(int) override {}
    void generate_JumpTrue(int) override {}
    void generate_LoadClosure(int) override {}
    void generate_LoadConst(int) override {}
    void generate_LoadElement(int) override {}
    void generate_LoadFalse() override {}
    void generate_LoadGlobalLookup(int) override {}
    void generate_LoadImport(int) override {}
    void generate_LoadInt(int) override {}
    void generate_LoadLocal(int) override {}
    void generate_LoadName(int) override {}
    void generate_LoadNull() override {}
    void generate_LoadOptionalProperty(int, int) override {}
    void generate_LoadProperty(int) override {}
    void generate_LoadQmlContextPropertyLookup(int) override {}
    void generate_LoadReg(int) override {}
    void generate_LoadRuntimeString(int) override {}
    void generate_LoadScopedLocal(int, int) override {}
    void generate_LoadSuperConstructor() override {}
    void generate_LoadSuperProperty(int) override {}
    void generate_LoadTrue() override {}
    void generate_LoadUndefined() override {}
    void generate_LoadZero() override {}
    void generate_Mod(int) override {}
    void generate_MoveConst(int, int) override {}
    void generate_MoveReg(int, int) override {}
    void generate_MoveRegExp(int, int) override {}
    void generate_Mul(int) override {}
    void generate_PopContext() override {}
    void generate_PopScriptContext() override {}
    void generate_PushBlockContext(int) override {}
    void generate_PushCatchContext(int, int) override {}
    void generate_PushScriptContext(int) override {}
    void generate_PushWithContext() override {}
    void generate_Resume(int) override {}
    void generate_Ret() override {}
    void generate_SetException() override {}
    void generate_SetLookup(int, int) override {}
    void generate_SetUnwindHandler(int) override {}
    void generate_Shl(int) override {}
    void generate_ShlConst(int) override {}
    void generate_Shr(int) override {}
    void generate_ShrConst(int) override {}
    void generate_StoreElement(int, int) override {}
    void generate_StoreLocal(int) override {}
    void generate_StoreNameSloppy(int) override {}
    void generate_StoreNameStrict(int) override {}
    void generate_StoreProperty(int, int) override {}
    void generate_StoreReg(int) override {}
    void generate_StoreScopedLocal(int, int) override {}
    void generate_StoreSuperProperty(int) override {}
    void generate_Sub(int) override {}
    void generate_TailCall(int, int, int, int) override {}
    void generate_ThrowException() override {}
    void generate_ThrowOnNullOrUndefined() override {}
    void generate_ToObject() override {}
    void generate_TypeofName(int) override {}
    void generate_TypeofValue() override {}
    void generate_UCompl() override {}
    void generate_UMinus() override {}
    void generate_UNot() override {}
    void generate_UPlus() override {}
    void generate_UShr(int) override {}
    void generate_UShrConst(int) override {}
    void generate_UnwindDispatch() override {}
    void generate_UnwindToLabel(int, int) override {}
    void generate_Yield() override {}
    void generate_YieldStar() override {}
};

QT_END_NAMESPACE

#endif // QQMLJSCOMPILEPASS_P_H
