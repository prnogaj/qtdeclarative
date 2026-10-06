// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QQMLJSCALLBACKSIGNATURES_P_H
#define QQMLJSCALLBACKSIGNATURES_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.

#include <QtCore/qlatin1stringview.h>
#include <QtCore/qstringview.h>

#include <initializer_list>

QT_BEGIN_NAMESPACE

/*!
 * \internal
 * What the methods that take callbacks call them with, and what they make of the result. The
 * type information of JavaScript's built-in objects and of QFuture only says that such a method
 * takes a function. This is the rest: one entry per callback argument of a method. Tools that
 * need to know the type of a callback's parameter, or that write a callback for the user, read
 * it from here rather than knowing the methods one by one.
 */
namespace QQmlJSCallbackSignatures {

// What the method is called on. A Promise is what then() and catch() of a future or of another
// promise return.
enum class Receiver { List, Future, Promise };

// What a parameter of the callback receives, in terms of the receiver
enum class Argument {
    Element,      // an element of the list
    Index,        // the index of that element
    Container,    // the list itself
    FutureResult, // the result of the future, or what the promise is resolved with
    Any           // something we know nothing about, for example an error or an accumulator
};

// What the method makes of what the callback returns
enum class Result { Ignored, Boolean, Number, Any };

struct Parameter
{
    QLatin1StringView name;
    Argument argument;
};

struct Signature
{
    Receiver receiver;
    QLatin1StringView method;
    int callbackPosition;      // which argument of the method the callback is
    int minimumParameters;     // a callback that takes fewer makes no sense
    std::initializer_list<Parameter> parameters;
    Result result;
};

inline const Signature *find(Receiver receiver, QStringView method, int callbackPosition)
{
    using namespace Qt::StringLiterals;

    static constexpr std::initializer_list<Parameter> visit = {
        { "element"_L1, Argument::Element },
        { "index"_L1, Argument::Index },
        { "array"_L1, Argument::Container },
    };
    static constexpr std::initializer_list<Parameter> reduce = {
        { "accumulator"_L1, Argument::Any },
        { "element"_L1, Argument::Element },
        { "index"_L1, Argument::Index },
        { "array"_L1, Argument::Container },
    };
    static constexpr std::initializer_list<Parameter> compare = {
        { "a"_L1, Argument::Element },
        { "b"_L1, Argument::Element },
    };
    static constexpr std::initializer_list<Parameter> fulfilled = {
        { "result"_L1, Argument::FutureResult },
    };
    static constexpr std::initializer_list<Parameter> rejected = {
        { "error"_L1, Argument::Any },
    };
    static constexpr std::initializer_list<Parameter> resolved = {
        { "value"_L1, Argument::FutureResult },
    };
    static constexpr std::initializer_list<Parameter> none = {};

    static const Signature signatures[] = {
        { Receiver::List, "forEach"_L1, 0, 1, visit, Result::Ignored },
        { Receiver::List, "map"_L1, 0, 1, visit, Result::Any },
        { Receiver::List, "flatMap"_L1, 0, 1, visit, Result::Any },
        { Receiver::List, "filter"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "some"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "every"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "find"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "findIndex"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "findLast"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "findLastIndex"_L1, 0, 1, visit, Result::Boolean },
        { Receiver::List, "reduce"_L1, 0, 2, reduce, Result::Any },
        { Receiver::List, "reduceRight"_L1, 0, 2, reduce, Result::Any },
        { Receiver::List, "sort"_L1, 0, 2, compare, Result::Number },
        { Receiver::List, "toSorted"_L1, 0, 2, compare, Result::Number },
        { Receiver::Future, "then"_L1, 0, 1, fulfilled, Result::Any },
        { Receiver::Future, "then"_L1, 1, 1, rejected, Result::Any },
        { Receiver::Future, "catch"_L1, 0, 1, rejected, Result::Ignored },
        { Receiver::Promise, "then"_L1, 0, 1, resolved, Result::Any },
        { Receiver::Promise, "then"_L1, 1, 1, rejected, Result::Any },
        { Receiver::Promise, "catch"_L1, 0, 1, rejected, Result::Any },
        { Receiver::Promise, "finally"_L1, 0, 0, none, Result::Ignored },
    };

    for (const Signature &signature : signatures) {
        if (signature.receiver == receiver && signature.callbackPosition == callbackPosition
                && signature.method == method) {
            return &signature;
        }
    }
    return nullptr;
}

} // namespace QQmlJSCallbackSignatures

QT_END_NAMESPACE

#endif // QQMLJSCALLBACKSIGNATURES_P_H
