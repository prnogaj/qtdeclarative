// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtTest/qtest.h>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/private/qjsvalue_p.h>
#include <QtQml/private/qv4persistent_p.h>

#include <QtCore/qregularexpression.h>

using namespace Qt::StringLiterals;

#include <memory>
#include <stdexcept>

#include "service.h"

class tst_qqmlfuture : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void resolves();
    void resolvesReadyFutureAsynchronously();
    void resolvesVoid();
    void resolvesWithoutResult();
    void resolvesWithFirstResult();
    void resolvesFromOtherThread();
    void resolvesQObject();
    void resolvesParentedQObject();
    void resolvesQObjectWithCppOwnership();
    void resolvesQObjectFromOtherThread();
    void resolvesValueTypeFromOtherThread();
    void resolvesEnum();
    void resolvesListOfValueTypes();
    void rejectsCanceled();
    void rejectsException();
    void rejectsWrappedException();
    void rejectsOtherException();
    void catchRecovers();
    void thenReturnsPromise();
    void branches();
    void thenAfterFulfilled();
    void thenAfterRejected();
    void keepsCppContinuation();
    void doesNotThrottleProducer();
    void handlerThrows();
    void adoptedByPromise();
    void handlerReturnsPromise();
    void handlerReturnsRejectedPromise();
    void handlerReturnsFuture();
    void handlerReturnsCanceledFuture();
    void promiseHandlerReturnsFuture();
    void thenRequiresFuture();
    void passedBackToCpp();
    void passedBackConverted();
    void convertsToVariant();
    void convertsFromCpp();
    void survivesGarbageCollection();
    void collectedWithoutThen();
    void engineDestroyedWhilePending();
    void engineDestroyedWhileSettling();
    void qmlComponent();

    void fromProperty();
    void fromSignal();
    void fromVariant();
    void fromVariantList();
    void fromVariantMap();
    void fromSequence();

    void compiledStore();
    void compiledThenDirect();
    void compiledThenFromBinding();
    void compiledThenProperty();
    void compiledPassStored();
    void compiledPassDirect();
    void compiledPassProperty();
    void typedStore();
    void typedThen();
    void typedPassDirect();
    void typedPassProperty();
    void typedPassStored();

private:
    QVariant eval(const QString &code);
    std::unique_ptr<QObject> createFutureUser();
    QVariant global(const char *name) { return m_engine->globalObject().property(QString::fromLatin1(name)).toVariant(); }

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<Service> m_service;
};

void tst_qqmlfuture::init()
{
    m_service = std::make_unique<Service>();
    Backend::instance = m_service.get();
    m_engine = std::make_unique<QQmlEngine>();
    QJSEngine::setObjectOwnership(m_service.get(), QJSEngine::CppOwnership);
    m_engine->globalObject().setProperty(QStringLiteral("service"),
                                         m_engine->newQObject(m_service.get()));
}

void tst_qqmlfuture::cleanup()
{
    m_engine.reset();
    m_service.reset();
}

QVariant tst_qqmlfuture::eval(const QString &code)
{
    const QJSValue result = m_engine->evaluate(code);
    if (result.isError())
        qWarning() << result.toString();
    return result.toVariant();
}

void tst_qqmlfuture::resolves()
{
    eval("var result; service.pending().then(value => result = value)");
    QCoreApplication::processEvents();
    QVERIFY(global("result").isNull() || !global("result").isValid());

    m_service->promise.addResult(42);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(42));
}

void tst_qqmlfuture::resolvesReadyFutureAsynchronously()
{
    eval("var order = []; service.ready(7).then(value => order.push(value)); order.push('sync')");
    QTRY_COMPARE(global("order").toList().size(), 2);
    QCOMPARE(global("order").toList(), QVariantList({ QStringLiteral("sync"), 7 }));
}

void tst_qqmlfuture::resolvesVoid()
{
    eval("var called = false; var result = 1;"
         "service.pendingVoid().then(value => { called = true; result = value })");
    m_service->voidPromise.finish();
    QTRY_VERIFY(global("called").toBool());
    QCOMPARE(eval("typeof result").toString(), QStringLiteral("undefined"));
}

void tst_qqmlfuture::resolvesWithoutResult()
{
    eval("var called = false; var result = 1;"
         "service.pending().then(value => { called = true; result = value })");
    m_service->promise.finish();
    QTRY_VERIFY(global("called").toBool());
    QCOMPARE(eval("typeof result").toString(), QStringLiteral("undefined"));
}

void tst_qqmlfuture::resolvesWithFirstResult()
{
    eval("var result; service.pending().then(value => result = value)");
    m_service->promise.addResult(1);
    m_service->promise.addResult(2);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(1));
}

void tst_qqmlfuture::resolvesFromOtherThread()
{
    eval("var result; service.fromThread('threaded').then(value => result = value)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("threaded")));
}

void tst_qqmlfuture::resolvesQObject()
{
    eval("var result; service.newPerson('Alice').then(person => result = person.name)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("Alice")));

    // Same as for a QObject returned from a Q_INVOKABLE: without a parent, it's garbage
    // collected once JavaScript doesn't reference it anymore.
    QVERIFY(m_service->person);
    QCOMPARE(QJSEngine::objectOwnership(m_service->person), QJSEngine::JavaScriptOwnership);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        m_engine->collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return m_service->person.isNull();
    }()), 5000);
}

void tst_qqmlfuture::resolvesParentedQObject()
{
    eval("var result; service.childPerson('Bob').then(person => result = person.name)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("Bob")));

    for (int i = 0; i < 3; ++i) {
        m_engine->collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QVERIFY(m_service->person);
}

void tst_qqmlfuture::resolvesQObjectWithCppOwnership()
{
    const QFuture<Person *> future = m_service->newPerson(QStringLiteral("Carol"));
    QJSEngine::setObjectOwnership(m_service->person, QJSEngine::CppOwnership);
    m_engine->globalObject().setProperty(QStringLiteral("future"),
                                         m_engine->toScriptValue(future));
    eval("var result; future.then(person => result = person.name); future = null");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("Carol")));

    for (int i = 0; i < 3; ++i) {
        m_engine->collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QVERIFY(m_service->person);
    QCOMPARE(QJSEngine::objectOwnership(m_service->person), QJSEngine::CppOwnership);
    delete m_service->person;
}

void tst_qqmlfuture::resolvesQObjectFromOtherThread()
{
    // An object living in another thread can't be owned by the engine, which would delete it
    // from the wrong thread. It is still passed on, but with a warning.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            u"The result of a QFuture, Person\\(.*\\), lives in a different thread"_s));
    eval("var result; service.newPersonInThread('Dave').then(person => result = person.name)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("Dave")));

    QVERIFY(m_service->person);
    QCOMPARE(QJSEngine::objectOwnership(m_service->person), QJSEngine::CppOwnership);
    for (int i = 0; i < 3; ++i) {
        m_engine->collectGarbage();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QVERIFY(m_service->person);
    delete m_service->person;
}

void tst_qqmlfuture::resolvesValueTypeFromOtherThread()
{
    eval("var result; service.contactInThread('Eve').then(contact => result = contact.name)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("Eve")));
}

void tst_qqmlfuture::resolvesEnum()
{
    eval("var result; service.readyLevel().then(level => result = level)");
    QTRY_COMPARE(global("result").toInt(), int(Service::High));
}

void tst_qqmlfuture::resolvesListOfValueTypes()
{
    eval("var result; service.readyContacts().then(contacts => result = contacts.map(c => c.name))");
    QTRY_COMPARE(global("result").toStringList(), QStringList({ u"Ann"_s, u"Ben"_s }));
}

void tst_qqmlfuture::rejectsCanceled()
{
    eval("var error; service.pending().then(value => {}, e => error = e)");
    m_service->promise.future().cancel();
    m_service->promise.finish();
    QTRY_VERIFY(eval("error instanceof Error").toBool());
    QCOMPARE(eval("error.message").toString(), QStringLiteral("The QFuture was canceled"));
}

void tst_qqmlfuture::rejectsException()
{
    eval("var error; service.pending().catch(e => error = e)");
    m_service->promise.setException(std::make_exception_ptr(std::runtime_error("boom")));
    m_service->promise.finish();
    QTRY_VERIFY(eval("error instanceof Error").toBool());
    QCOMPARE(eval("error.message").toString(), QStringLiteral("boom"));
}

void tst_qqmlfuture::rejectsWrappedException()
{
    // QtConcurrent::run() reports a std::exception wrapped in a QUnhandledException.
    eval("var error; service.pending().catch(e => error = e)");
    m_service->promise.setException(
            QUnhandledException(std::make_exception_ptr(std::runtime_error("wrapped boom"))));
    m_service->promise.finish();
    QTRY_VERIFY(eval("error instanceof Error").toBool());
    QCOMPARE(eval("error.message").toString(), QStringLiteral("wrapped boom"));
}

void tst_qqmlfuture::rejectsOtherException()
{
    eval("var error; service.pending().catch(e => error = e)");
    m_service->promise.setException(std::make_exception_ptr(42));
    m_service->promise.finish();
    QTRY_VERIFY(eval("error instanceof Error").toBool());
    QCOMPARE(eval("error.message").toString(),
             QStringLiteral("The QFuture failed with an exception"));
}

void tst_qqmlfuture::catchRecovers()
{
    eval("var result; service.pending().catch(e => 5).then(value => result = value)");
    m_service->promise.future().cancel();
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(5));
}

void tst_qqmlfuture::thenReturnsPromise()
{
    QVERIFY(eval("service.pending().then() instanceof Promise").toBool());
    QVERIFY(eval("service.pending().catch() instanceof Promise").toBool());
    QVERIFY(!eval("service.pending() instanceof Promise").toBool());

    eval("var result; service.pending().then(x => x + 1).then(x => result = x * 2)");
    m_service->promise.addResult(20);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(42));
}

void tst_qqmlfuture::branches()
{
    eval("var a, b; var future = service.pending();"
         "future.then(x => a = x + 1); future.then(x => b = x + 2)");
    m_service->promise.addResult(1);
    m_service->promise.finish();
    QTRY_COMPARE(global("b"), QVariant(3));
    QCOMPARE(global("a"), QVariant(2));
}

void tst_qqmlfuture::thenAfterFulfilled()
{
    eval("var a, b; var future = service.pending(); future.then(x => a = x)");
    m_service->promise.addResult(7);
    m_service->promise.finish();
    QTRY_COMPARE(global("a"), QVariant(7));

    // The object remembers the outcome, like a promise.
    eval("var order = []; future.then(x => { b = x; order.push('then') }); order.push('sync')");
    QTRY_COMPARE(global("b"), QVariant(7));
    QCOMPARE(global("order").toList(),
             QVariantList({ QStringLiteral("sync"), QStringLiteral("then") }));
}

void tst_qqmlfuture::thenAfterRejected()
{
    eval("var a, b; var future = service.pending(); future.catch(e => a = e.message)");
    m_service->promise.future().cancel();
    m_service->promise.finish();
    QTRY_COMPARE(global("a"), QVariant(QStringLiteral("The QFuture was canceled")));

    eval("future.then(x => b = 'fulfilled', e => b = e.message)");
    QTRY_COMPARE(global("b"), QVariant(QStringLiteral("The QFuture was canceled")));
}

void tst_qqmlfuture::keepsCppContinuation()
{
    // A QFuture has only one continuation. Observing it from QML must not replace it.
    bool cppCalled = false;
    auto continuation = m_service->promise.future().then([&](int) { cppCalled = true; });
    eval("var result; service.pending().then(x => result = x)");

    m_service->promise.addResult(4);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(4));
    QVERIFY(cppCalled);
}

void tst_qqmlfuture::doesNotThrottleProducer()
{
    // QML only waits for the future to finish. However many results pile up while the engine's
    // thread is busy, a producer that honors isThrottled() must not be slowed down.
    eval("var result; service.pending().then(x => result = x)");
    const QFutureInterfaceBase producer = QFutureInterfaceBase::get(m_service->promise.future());
    const int count = QThread::idealThreadCount() * 2 + 10;
    for (int i = 0; i < count; ++i) {
        m_service->promise.addResult(i);
        QVERIFY2(!producer.isThrottled(), qPrintable(QString::number(i)));
    }
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(0));
}

void tst_qqmlfuture::handlerThrows()
{
    eval("var result; service.ready(1).then(x => { throw new Error('oops') })"
         "    .catch(e => result = e.message)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("oops")));
}

void tst_qqmlfuture::adoptedByPromise()
{
    eval("var single, all;"
         "Promise.resolve(service.ready(3)).then(x => single = x);"
         "Promise.all([service.ready(1), service.ready(2)]).then(x => all = x)");
    QTRY_COMPARE(global("single"), QVariant(3));
    QTRY_COMPARE(global("all").toList(), QVariantList({ 1, 2 }));
}

void tst_qqmlfuture::handlerReturnsPromise()
{
    eval("var result; service.ready(1)"
         "    .then(x => new Promise(resolve => resolve(x + 1)))"
         "    .then(x => result = x)");
    QTRY_COMPARE(global("result"), QVariant(2));
}

void tst_qqmlfuture::handlerReturnsRejectedPromise()
{
    eval("var result; service.ready(1)"
         "    .then(x => Promise.reject(new Error('inner')))"
         "    .then(x => result = 'fulfilled', e => result = e.message)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("inner")));
}

void tst_qqmlfuture::handlerReturnsFuture()
{
    // The inner future is adopted as a thenable, and the chain waits for it.
    eval("var result; var outerDone = false; service.ready(1)"
         "    .then(x => { outerDone = true; return service.pending() })"
         "    .then(x => result = x)");
    QTRY_VERIFY(global("outerDone").toBool());
    QCoreApplication::processEvents();
    QVERIFY(!global("result").isValid() || global("result").isNull());

    m_service->promise.addResult(5);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(5));
}

void tst_qqmlfuture::handlerReturnsCanceledFuture()
{
    eval("var result; service.ready(1)"
         "    .then(x => service.pending())"
         "    .catch(e => result = e.message)");
    m_service->promise.future().cancel();
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("The QFuture was canceled")));
}

void tst_qqmlfuture::promiseHandlerReturnsFuture()
{
    eval("var result; Promise.resolve(1)"
         "    .then(x => service.pending())"
         "    .then(x => result = x)");
    m_service->promise.addResult(6);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(6));
}

void tst_qqmlfuture::thenRequiresFuture()
{
    const QJSValue error = m_engine->evaluate(
            "Object.getPrototypeOf(service.ready(1)).then.call({}, x => x)");
    QVERIFY(error.isError());
    QCOMPARE(error.errorType(), QJSValue::TypeError);
}

void tst_qqmlfuture::passedBackToCpp()
{
    eval("var future = service.pending(); future.then(x => x); service.take(future)");
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::passedBackConverted()
{
    eval("service.takeVoid(service.pending())");
    QVERIFY(QFutureInterfaceBase::get(m_service->takenVoidFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::convertsToVariant()
{
    const QJSValue value = m_engine->evaluate("service.pending()");
    QCOMPARE(value.toVariant().metaType(), QMetaType::fromType<QFuture<int>>());

    const QFuture<int> future = m_engine->fromScriptValue<QFuture<int>>(value);
    QVERIFY(QFutureInterfaceBase::get(future)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::convertsFromCpp()
{
    m_engine->globalObject().setProperty(
            QStringLiteral("future"),
            m_engine->toScriptValue(QtFuture::makeReadyValueFuture(QStringLiteral("direct"))));
    eval("var result; future.then(x => result = x)");
    QTRY_COMPARE(global("result"), QVariant(QStringLiteral("direct")));
}

void tst_qqmlfuture::survivesGarbageCollection()
{
    eval("var result; (function() { service.pending().then(x => result = x) })()");
    m_engine->collectGarbage();
    QCoreApplication::processEvents();
    m_engine->collectGarbage();

    m_service->promise.addResult(11);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(11));
}

void tst_qqmlfuture::collectedWithoutThen()
{
    // Only then() starts watching the future. Before that, nothing keeps the object alive.
    eval("var future = service.pending()");
    QV4::WeakValue future;
    {
        const QJSValue value = m_engine->globalObject().property(QStringLiteral("future"));
        future.set(m_engine->handle(), QJSValuePrivate::asReturnedValue(&value));
    }
    QVERIFY(!future.isNullOrUndefined());
    eval("future = undefined");
    m_engine->collectGarbage();
    QVERIFY(future.isNullOrUndefined());
}

void tst_qqmlfuture::engineDestroyedWhilePending()
{
    eval("service.pending().then(x => x)");
    m_engine.reset();

    m_service->promise.addResult(1);
    m_service->promise.finish();
    QCoreApplication::processEvents();
}

void tst_qqmlfuture::engineDestroyedWhileSettling()
{
    eval("service.pending().then(x => x)");

    // The continuation is queued to the engine's thread, but not delivered yet.
    m_service->promise.addResult(1);
    m_service->promise.finish();
    m_engine.reset();
    QCoreApplication::processEvents();
}

void tst_qqmlfuture::qmlComponent()
{
    QQmlComponent component(m_engine.get());
    component.setData(R"(
        import QtQml
        QtObject {
            property var service
            property var request
            property int value: -1
            Component.onCompleted: {
                request = service.pending()
                request.then(result => value = result)
            }
        }
    )", QUrl());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
            { { QStringLiteral("service"), QVariant::fromValue<QObject *>(m_service.get()) } }));
    QVERIFY(object);

    QCOMPARE(object->property("request").metaType(), QMetaType::fromType<QFuture<int>>());
    m_service->promise.addResult(5);
    m_service->promise.finish();
    QTRY_COMPARE(object->property("value").toInt(), 5);
}

void tst_qqmlfuture::fromProperty()
{
    eval("var result; service.current.then(x => result = x)");
    m_service->promise.addResult(1);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(1));
}

void tst_qqmlfuture::fromSignal()
{
    eval("var result; service.delivered.connect(future => future.then(x => result = x))");
    m_service->deliver();
    m_service->promise.addResult(2);
    m_service->promise.finish();
    QTRY_COMPARE(global("result"), QVariant(2));
}

void tst_qqmlfuture::fromVariant()
{
    eval("var result; service.readyAsVariant(3).then(x => result = x)");
    QTRY_COMPARE(global("result"), QVariant(3));
}

void tst_qqmlfuture::fromVariantList()
{
    eval("var result; Promise.all(service.readyList()).then(x => result = x)");
    QTRY_COMPARE(global("result").toList(), QVariantList({ 1, 2 }));
}

void tst_qqmlfuture::fromVariantMap()
{
    eval("var result; service.readyMap().a.then(x => result = x)");
    QTRY_COMPARE(global("result"), QVariant(3));
}

void tst_qqmlfuture::fromSequence()
{
    eval("var result; Promise.all(service.readySequence()).then(x => result = x)");
    QTRY_COMPARE(global("result").toList(), QVariantList({ 4, 5 }));
}

std::unique_ptr<QObject> tst_qqmlfuture::createFutureUser()
{
    QQmlComponent component(m_engine.get());
    component.loadFromModule("FutureTest", "FutureUser");
    if (!component.isReady()) {
        qWarning() << component.errorString();
        return nullptr;
    }
    return std::unique_ptr<QObject>(component.createWithInitialProperties(
            { { QStringLiteral("service"), QVariant::fromValue(m_service.get()) } }));
}

void tst_qqmlfuture::compiledStore()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "store"));
    QCOMPARE(user->property("request").metaType(), QMetaType::fromType<QFuture<int>>());
    QVERIFY(QMetaObject::invokeMethod(user.get(), "thenStored"));
    m_service->promise.addResult(10);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 10);
}

void tst_qqmlfuture::compiledThenDirect()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "thenDirect"));
    m_service->promise.addResult(11);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 11);
}

void tst_qqmlfuture::compiledThenFromBinding()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QCOMPARE(user->property("fromBinding").metaType(), QMetaType::fromType<QFuture<int>>());
    QVERIFY(QMetaObject::invokeMethod(user.get(), "thenFromBinding"));
    m_service->promise.addResult(12);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 12);
}

void tst_qqmlfuture::compiledThenProperty()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "thenProperty"));
    m_service->promise.addResult(13);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 13);
}

void tst_qqmlfuture::compiledPassStored()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "store"));
    QVERIFY(QMetaObject::invokeMethod(user.get(), "passStored"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::compiledPassDirect()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "passDirect"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::compiledPassProperty()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "passProperty"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::typedStore()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedStore"));
    QCOMPARE(user->property("request").metaType(), QMetaType::fromType<QFuture<int>>());
    QVERIFY(QMetaObject::invokeMethod(user.get(), "thenStored"));
    m_service->promise.addResult(20);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 20);
}

void tst_qqmlfuture::typedThen()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedThen"));
    m_service->promise.addResult(21);
    m_service->promise.finish();
    QTRY_COMPARE(user->property("value").toInt(), 21);
}

void tst_qqmlfuture::typedPassDirect()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedPassDirect"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::typedPassProperty()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedPassProperty"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

void tst_qqmlfuture::typedPassStored()
{
    const auto user = createFutureUser();
    QVERIFY(user);
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedStore"));
    QVERIFY(QMetaObject::invokeMethod(user.get(), "typedPassStored"));
    QVERIFY(QFutureInterfaceBase::get(m_service->takenFuture)
            == QFutureInterfaceBase::get(m_service->promise.future()));
}

QTEST_MAIN(tst_qqmlfuture)

#include "tst_qqmlfuture.moc"
