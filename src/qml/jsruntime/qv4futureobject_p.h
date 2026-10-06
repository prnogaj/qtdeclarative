// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant

#ifndef QV4FUTUREOBJECT_P_H
#define QV4FUTUREOBJECT_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qtcoreglobal.h>

QT_REQUIRE_CONFIG(future);

#include <private/qv4object_p.h>

#include <QtCore/qmetatype.h>
#include <QtCore/qvariant.h>

QT_BEGIN_NAMESPACE

namespace QV4 {

namespace Heap {

#define FutureObjectMembers(class, Member) \
    Member(class, HeapValue, HeapValue, resolution) \
    Member(class, HeapValue, HeapValue, fulfillReactions) \
    Member(class, HeapValue, HeapValue, rejectReactions)

DECLARE_HEAP_OBJECT(FutureObject, Object) {
    DECLARE_MARKOBJECTS(FutureObject)

    enum State {
        Idle,
        Pending,
        Fulfilled,
        Rejected
    };

    void init(QMetaType type, const void *data);
    void destroy();

    const QVariant &future() const;

    void watch(ExecutionEngine *engine);
    void settle();

    State state;

private:
    struct Data;
    Data *m_data;
};

}

struct FutureObject : Object
{
    V4_OBJECT2(FutureObject, Object)
    V4_PROTOTYPE(futurePrototype)
    V4_NEEDS_DESTROY

    static bool isFutureType(QMetaType type);
};

struct FuturePrototype : Object
{
    V4_PROTOTYPE(objectPrototype)
    void init();

    static ReturnedValue method_then(const FunctionObject *, const Value *thisObject, const Value *argv, int argc);
    static ReturnedValue method_catch(const FunctionObject *, const Value *thisObject, const Value *argv, int argc);
};

} // namespace QV4

QT_END_NAMESPACE

#endif // QV4FUTUREOBJECT_P_H
