// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef SERVICE_H
#define SERVICE_H

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QtCore/qfuture.h>
#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qpromise.h>
#include <QtCore/qthread.h>
#include <QtCore/qvariant.h>

#include <memory>

class Person final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name MEMBER m_name CONSTANT)
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    explicit Person(const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_name(name)
    {}

private:
    QString m_name;
};

// A value type result is copied, so it doesn't matter which thread produces it.
struct Contact
{
    Q_GADGET
    QML_VALUE_TYPE(contact)
    Q_PROPERTY(QString name MEMBER name FINAL)
public:
    QString name;
};

class Service final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QFuture<int> current READ current CONSTANT FINAL)
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    enum Level { Low, High };
    Q_ENUM(Level)

    QPromise<int> promise;
    QPromise<void> voidPromise;
    QFuture<int> takenFuture;
    QFuture<void> takenVoidFuture;
    QPointer<Person> person;
    std::unique_ptr<QThread> personThread;

    Service() { promise.start(); voidPromise.start(); }
    ~Service() override
    {
        if (personThread)
            personThread->wait();
    }

    QFuture<int> current() { return promise.future(); }

    Q_INVOKABLE QFuture<int> pending() { return promise.future(); }
    Q_INVOKABLE QFuture<void> pendingVoid() { return voidPromise.future(); }
    Q_INVOKABLE QFuture<int> ready(int value) { return QtFuture::makeReadyValueFuture(value); }

    Q_INVOKABLE QVariant readyAsVariant(int value) { return QVariant::fromValue(ready(value)); }
    Q_INVOKABLE QVariantList readyList()
    {
        return { QVariant::fromValue(ready(1)), QVariant::fromValue(ready(2)) };
    }
    Q_INVOKABLE QVariantMap readyMap() { return { { QStringLiteral("a"), QVariant::fromValue(ready(3)) } }; }
    Q_INVOKABLE QList<QFuture<int>> readySequence() { return { ready(4), ready(5) }; }

    Q_INVOKABLE void deliver() { emit delivered(promise.future()); }

    Q_INVOKABLE QFuture<QString> fromThread(const QString &value)
    {
        auto threadPromise = std::make_shared<QPromise<QString>>();
        QFuture<QString> future = threadPromise->future();
        QThread *thread = QThread::create([threadPromise, value] {
            threadPromise->start();
            QThread::msleep(20);
            threadPromise->addResult(value);
            threadPromise->finish();
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
        return future;
    }

    Q_INVOKABLE QFuture<Person *> newPerson(const QString &name)
    {
        person = new Person(name);
        return QtFuture::makeReadyValueFuture(person.get());
    }

    // A list of objects, as C++ holds it. QML declares that as list<Person>.
    Q_INVOKABLE QFuture<QList<Person *>> people()
    {
        return QtFuture::makeReadyValueFuture(QList<Person *>{
                new Person(QStringLiteral("Alice"), this), new Person(QStringLiteral("Bob"), this) });
    }

    Q_INVOKABLE QFuture<Person *> childPerson(const QString &name)
    {
        person = new Person(name, this);
        return QtFuture::makeReadyValueFuture(person.get());
    }

    Q_INVOKABLE QFuture<Person *> newPersonInThread(const QString &name)
    {
        auto threadPromise = std::make_shared<QPromise<Person *>>();
        QFuture<Person *> future = threadPromise->future();
        personThread.reset(QThread::create([this, threadPromise, name] {
            threadPromise->start();
            person = new Person(name); // lives in the worker thread
            threadPromise->addResult(person.get());
            threadPromise->finish();
        }));
        personThread->start();
        return future;
    }

    Q_INVOKABLE QFuture<Contact> contactInThread(const QString &name)
    {
        auto threadPromise = std::make_shared<QPromise<Contact>>();
        QFuture<Contact> future = threadPromise->future();
        QThread *thread = QThread::create([threadPromise, name] {
            threadPromise->start();
            threadPromise->addResult(Contact { name });
            threadPromise->finish();
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
        return future;
    }

    Q_INVOKABLE QFuture<Level> readyLevel() { return QtFuture::makeReadyValueFuture(High); }
    Q_INVOKABLE QFuture<QList<Contact>> readyContacts()
    {
        return QtFuture::makeReadyValueFuture(
                QList<Contact>{ Contact{ QStringLiteral("Ann") }, Contact{ QStringLiteral("Ben") } });
    }

    Q_INVOKABLE void take(const QFuture<int> &future) { takenFuture = future; }
    Q_INVOKABLE void takeVoid(const QFuture<void> &future) { takenVoidFuture = future; }

signals:
    void delivered(const QFuture<int> &future);
};

// The same service as a singleton. Members of singletons can't be shadowed, so that
// qmlcachegen generates strongly typed code for them.
struct Backend
{
    Q_GADGET
    QML_FOREIGN(Service)
    QML_NAMED_ELEMENT(Backend)
    QML_SINGLETON
public:
    static Service *create(QQmlEngine *, QJSEngine *) { return instance; }
    static inline Service *instance = nullptr;
};

#endif // SERVICE_H
