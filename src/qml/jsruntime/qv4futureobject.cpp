// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant

#include "qv4futureobject_p.h"

#include <private/qqmldata_p.h>
#include <private/qv4arrayobject_p.h>
#include <private/qv4errorobject_p.h>
#include <private/qv4persistent_p.h>
#include <private/qv4promiseobject_p.h>

#include <QtCore/qfuture.h>
#include <QtCore/qfuturewatcher.h>
#include <QtCore/qthread.h>

#include <limits>

QT_BEGIN_NAMESPACE

using namespace QV4;

/*!
 * \class QV4::FutureObject
 * \internal
 *
 * A FutureObject holds a QFuture<T> of any T. It stays a QFuture<T>, so that it can be
 * passed back to C++. then() and catch() make it a thenable, returning regular promises.
 *
 * The first call to then() or catch() starts watching the future. From then on, until the
 * future finishes, the object keeps itself alive, so that the reactions registered on it
 * are triggered even if nothing else references it anymore. XMLHttpRequest does the same
 * while a request is in flight. Without then() or catch(), the object is garbage collected
 * as usual. After the future finishes, the object remembers the outcome, like a promise.
 */

DEFINE_OBJECT_VTABLE(FutureObject);

struct Heap::FutureObject::Data
{
    Data(QMetaType type, const void *data) : future(type, data) {}

    QVariant future;
    QFutureWatcher<void> watcher;
    PersistentValue self;
};

void Heap::FutureObject::init(QMetaType type, const void *data)
{
    Object::init();
    state = Idle;
    m_data = new Data(type, data);
}

void Heap::FutureObject::destroy()
{
    // Only while the engine is destroyed can we still be pending here. The watcher doesn't
    // emit anything anymore once it is destroyed.
    delete m_data;
    Object::destroy();
}

const QVariant &Heap::FutureObject::future() const
{
    return m_data->future;
}

void Heap::FutureObject::watch(ExecutionEngine *engine)
{
    Q_ASSERT(state == Idle);
    state = Pending;
    fulfillReactions.set(engine, engine->newArrayObject());
    rejectReactions.set(engine, engine->newArrayObject());

    const QVariant &future = m_data->future;
    QFuture<void> observed;
    if (future.metaType() == QMetaType::fromType<QFuture<void>>()) {
        observed = *static_cast<const QFuture<void> *>(future.constData());
    } else if (!QMetaType::convert(future.metaType(), future.constData(),
                                   QMetaType::fromType<QFuture<void>>(), &observed)) {
        Q_UNREACHABLE(); // FutureObject::isFutureType() ensures the conversion
    }

    // Keep ourselves alive until the future finishes.
    m_data->self.set(engine, this);
    // We only wait for the future to finish, and don't consume results as they arrive. A busy
    // engine thread must not throttle the producer.
    QFutureWatcher<void> *watcher = &m_data->watcher;
    watcher->setPendingResultsLimit(std::numeric_limits<int>::max());
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher, [this]() { settle(); });
    watcher->setFuture(observed);
}

void Heap::FutureObject::settle()
{
    Q_ASSERT(state == Pending);
    ExecutionEngine *engine = internalClass->engine;
    Scope scope(engine);
    ScopedValue value(scope);

    const QFutureInterfaceBase futureInterface = QFutureInterfaceBase::get(m_data->watcher.future());
    if (futureInterface.hasException()) {
        // QtQml is built without exception support. QtCore looks into the exception for us.
        QString message = QtPrivate::futureExceptionMessage(futureInterface);
        if (message.isEmpty())
            message = QStringLiteral("The QFuture failed with an exception");
        value = engine->newErrorObject(message);
        state = Rejected;
    } else if (futureInterface.isCanceled()) {
        value = engine->newErrorObject(QStringLiteral("The QFuture was canceled"));
        state = Rejected;
    } else {
        const QtPrivate::QFutureTypeInterface *iface
                = QtPrivate::futureTypeInterface(m_data->future.metaType());
        Q_ASSERT(iface);
        const QVariant result = iface->readResult(m_data->future.constData(), 0);

        if (result.metaType().flags() & QMetaType::PointerToQObject) {
            if (QObject *object = *static_cast<QObject *const *>(result.constData())) {
                if (object->thread() != QThread::currentThread()) {
                    // Futures are often completed in worker threads. We must not take
                    // ownership of such an object, as we would delete it in the wrong thread.
                    qWarning().nospace()
                            << "The result of a QFuture, " << object
                            << ", lives in a different thread than the JavaScript engine. "
                               "Move it to the engine's thread before reporting it.";
                } else {
                    // Same ownership rule as for a QObject returned from a Q_INVOKABLE.
                    QQmlData *ddata = QQmlData::get(object, true);
                    if (!ddata->explicitIndestructibleSet)
                        ddata->indestructible = false;
                }
            }
        }

        value = engine->fromVariant(result);
        state = Fulfilled;
    }

    resolution.set(engine, value);

    // Like a promise: trigger the reactions of the outcome, and forget all of them.
    ScopedArrayObject reactions(scope, fulfillReactions);
    if (state == Rejected)
        reactions = rejectReactions;
    const uint length = reactions->getLength();
    Scoped<QV4::PromiseReaction> reaction(scope);
    for (uint i = 0; i < length; ++i) {
        reaction = reactions->get(i);
        reaction->d()->triggerWithValue(engine, value);
    }
    fulfillReactions.set(engine, Value::undefinedValue());
    rejectReactions.set(engine, Value::undefinedValue());

    // From now on we are garbage collected as usual.
    m_data->self.clear();
}

bool FutureObject::isFutureType(QMetaType type)
{
    // QtCore registers an interface along with the metatype of any QFuture<T> with
    // a copyable T known to the metatype system.
    return QtPrivate::futureTypeInterface(type) != nullptr;
}

void FuturePrototype::init()
{
    defineDefaultProperty(QStringLiteral("then"), method_then, 2);
    defineDefaultProperty(QStringLiteral("catch"), method_catch, 1);
}

ReturnedValue FuturePrototype::method_then(
        const FunctionObject *f, const Value *thisObject, const Value *argv, int argc)
{
    Scope scope(f);
    ExecutionEngine *e = scope.engine;

    Scoped<FutureObject> future(scope, thisObject);
    if (!future)
        THROW_TYPE_ERROR();

    // Like Promise.prototype.then, only that the reactions are triggered by the future.
    ScopedFunctionObject onFulfilled(scope, argc >= 1 ? argv[0] : Value::undefinedValue());
    ScopedFunctionObject onRejected(scope, argc >= 2 ? argv[1] : Value::undefinedValue());

    Scoped<PromiseCapability> capability(scope, e->memoryManager->allocate<PromiseCapability>());
    ScopedObject promise(scope, e->newPromiseObject(e->promiseCtor(), capability));
    if (scope.hasException())
        return Encode::undefined();
    capability->d()->promise.set(e, promise);

    Scoped<PromiseReaction> fulfillReaction(
            scope, Heap::PromiseReaction::createFulfillReaction(e, capability, onFulfilled));
    Scoped<PromiseReaction> rejectReaction(
            scope, Heap::PromiseReaction::createRejectReaction(e, capability, onRejected));

    Heap::FutureObject *d = future->d();
    switch (d->state) {
    case Heap::FutureObject::Idle:
        d->watch(e);
        Q_FALLTHROUGH();
    case Heap::FutureObject::Pending: {
        ScopedArrayObject reactions(scope, d->fulfillReactions);
        ScopedValue reaction(scope, fulfillReaction->d());
        reactions->push_back(reaction);
        reactions = d->rejectReactions;
        reaction = rejectReaction->d();
        reactions->push_back(reaction);
        break;
    }
    case Heap::FutureObject::Fulfilled: {
        ScopedValue resolution(scope, d->resolution);
        fulfillReaction->d()->triggerWithValue(e, resolution);
        break;
    }
    case Heap::FutureObject::Rejected: {
        ScopedValue resolution(scope, d->resolution);
        rejectReaction->d()->triggerWithValue(e, resolution);
        break;
    }
    }

    return promise->asReturnedValue();
}

ReturnedValue FuturePrototype::method_catch(
        const FunctionObject *f, const Value *thisObject, const Value *argv, int argc)
{
    Scope scope(f);
    Value *args = scope.constructUndefined(2);
    if (argc >= 1)
        args[1] = argv[0];
    return method_then(f, thisObject, args, 2);
}

QT_END_NAMESPACE
