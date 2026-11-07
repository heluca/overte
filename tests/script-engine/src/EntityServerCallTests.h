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
#include <QtCore/QSemaphore>
#include <QtTest/QtTest>

// Exposed to a test script as Teardown
class ClientTeardown : public QObject {
    Q_OBJECT
public:
    explicit ClientTeardown(QSemaphore& gate) : _gate(gate) {}
    // Destroys the EntityScriptClient, then lets its thread run what was queued to it
    Q_INVOKABLE void destroyClient();

    bool destroyed { false };

private:
    QSemaphore& _gate;
};

// Entities calls into server entity scripts, which go through EntityScriptClient (#30). There is no domain or entity
// script server here, so these cover the process without a client and the client without a server.
class EntityServerCallTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testWithoutClientEachCallThrows();
    void testWithClientAndNoServer();
    void testClientDestroyedBeforeQueuedStart();
    void testStatusWithoutReplyFails();
    void testFailureAfterScriptFinished();
};

#endif // overte_EntityServerCallTests_h
