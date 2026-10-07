# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    cmake_minimum_required(VERSION 3.16)
    project(tst_qqmlfuture LANGUAGES CXX)
    find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
endif()

qt_internal_add_test(tst_qqmlfuture
    SOURCES
        tst_qqmlfuture.cpp
    LIBRARIES
        Qt::Qml
        Qt::QmlPrivate
    EXCEPTIONS
)

qt_policy(SET QTP0001 NEW)

qt_add_qml_module(tst_qqmlfuture
    URI FutureTest
    SOURCES
        service.h
    QML_FILES
        data/FutureUser.qml
)

if(QT_BUILD_STANDALONE_TESTS)
    qt_import_qml_plugins(tst_qqmlfuture)
endif()
