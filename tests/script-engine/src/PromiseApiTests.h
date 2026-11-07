//
//  PromiseApiTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_PromiseApiTests_h
#define overte_PromiseApiTests_h

#include <optional>

#include <QtCore/QObject>
#include <QtTest/QtTest>

#include "ScriptCompletion.h"
#include "ScriptValue.h"

class ScriptManager;

// Exposed to test scripts as Probe
class PromiseApiProbe : public QObject {
    Q_OBJECT
public:
    explicit PromiseApiProbe(ScriptManager* manager) : _manager(manager) {}

    Q_INVOKABLE QString sandboxURL() const;
    Q_INVOKABLE QString entityID() const;
    // Calls the function in a new ES module environment for the entity, as ScriptModuleEnvTests does
    Q_INVOKABLE ScriptValue withModuleEnvironment(const QString& entityID, const QString& sandboxURL,
                                                  const ScriptValue& function);
    // A native method shaped like the converted APIs, whose completion is kept for C++ to complete
    Q_INVOKABLE ScriptValue later(const ScriptValue& scope = ScriptValue(), const ScriptValue& callback = ScriptValue());
    // Completes the kept completion from the event loop, outside any script call
    Q_INVOKABLE void completeLater(const QString& error, const QVariantMap& result);
    Q_INVOKABLE int pendingResolvers() const;

    std::optional<ScriptCompletion> kept;

private:
    ScriptManager* _manager;
};

// Promise forms of Assets and Recording.loadRecording, and their unchanged callback forms
class PromiseApiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testCallbackForms();
    void testPromiseForms();
    void testArgumentErrors();
    void testEnvironmentAfterAwait();
    void testStopBeforeSettle();
    void testEngineGoneBeforeSettle();
    void testUnhandledRejection();
    void testLoadRecording();
    void testLoadRecordingLoaded();
    void testLoadRecordingWithoutManager();
    void testRejectionDetails();
};

#endif // overte_PromiseApiTests_h
