//
//  EntityServerCallTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_EntityServerCallTests_h
#define overte_EntityServerCallTests_h

#include <QtCore/QObject>
#include <QtTest/QtTest>

// Entities calls into server entity scripts, which go through EntityScriptClient (#30). There is no domain or entity
// script server here, so these cover the process without a client and the client without a server.
class EntityServerCallTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testWithoutClientEachCallThrows();
    void testWithClientAndNoServer();
};

#endif // overte_EntityServerCallTests_h
