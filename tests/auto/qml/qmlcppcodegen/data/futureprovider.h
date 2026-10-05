// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#ifndef FUTUREPROVIDER_H
#define FUTUREPROVIDER_H

#include "person.h"

#include <QtCore/qfuture.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>
#include <QtCore/qpromise.h>
#include <QtQml/qqmlregistration.h>

// Returns futures for code that calls then() and catch() on them.
class FutureProvider : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    FutureProvider(QObject *parent = nullptr) : QObject(parent)
    {
        for (int i = 0; i < 3; ++i) {
            Person *person = new Person(this);
            person->setName(QStringLiteral("p%1").arg(i));
            person->setShoeSize(40 + i);
            m_people.append(person);
        }
    }

    Q_INVOKABLE QFuture<int> number(int value)
    {
        return QtFuture::makeReadyValueFuture(value);
    }

    Q_INVOKABLE QFuture<QList<Person *>> people()
    {
        return QtFuture::makeReadyValueFuture(m_people);
    }

    Q_INVOKABLE QFuture<int> canceled()
    {
        QPromise<int> promise;
        QFuture<int> future = promise.future();
        promise.start();
        future.cancel();
        promise.finish();
        return future;
    }

private:
    QList<Person *> m_people;
};

#endif // FUTUREPROVIDER_H
