//
//  ScriptModuleEnvCallbackTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleEnvCallbackTests_h
#define overte_ScriptModuleEnvCallbackTests_h

#include <QtCore/QObject>
#include <QtTest/QtTest>

#include "ScriptValue.h"

class ScriptManager;

// Exposed to test scripts as Probe
class CallbackProbe : public QObject {
    Q_OBJECT
public:
    explicit CallbackProbe(ScriptManager* manager) : _manager(manager) {}

    // ScriptManager::currentEntityIdentifier(), empty when null
    Q_INVOKABLE QString entityID() const;
    // A permission-gated call: ScriptPermissions::isCurrentScriptAllowed(SCRIPT_PERMISSION_GET_AVATAR_URL)
    Q_INVOKABLE bool allowed() const;
    // Test-only: calls the function in a new ES module environment for the entity and sandbox, as an entity .mjs
    // script's code runs
    Q_INVOKABLE ScriptValue withModuleEnvironment(const QString& entityID, const QString& sandboxURL,
                                                  const ScriptValue& function);
    // Loads a classic entity script from its source as ScriptManager does once fetched, from the event loop; the
    // promise resolves once its preload has run
    Q_INVOKABLE ScriptValue loadClassicEntity(const QString& entityID, const QString& url, const QString& source);
    Q_INVOKABLE void unloadEntity(const QString& entityID, const QString& url);

private:
    ScriptManager* _manager;
};

// Native callbacks (XMLHttpRequest, WebSocket) run in the script environment their handler was assigned in
class ScriptModuleEnvCallbackTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testXhrCallbackKeepsModuleEnvironment();
    void testXhrCallbackTimerCarriesEntity();
    void testWebSocketCallbacksKeepModuleEnvironment();
};

#endif // overte_ScriptModuleEnvCallbackTests_h
