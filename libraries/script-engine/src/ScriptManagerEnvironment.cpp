//
//  ScriptManagerEnvironment.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// ScriptManager's entity and sandbox environment: ES module code's comes from the engine (ScriptEnvironment), classic
// script code's from the members doWithEnvironment() sets.

#include "ScriptManager.h"

#include "ScriptEnvironment.h"

EntityItemID ScriptManager::currentEntityIdentifier() const {
    if (auto environment = _engine->currentEnvironment()) {
        return environment->entityID;
    }
    return _currentEntityIdentifier;
}

QUrl ScriptManager::currentSandboxURL() const {
    if (auto environment = _engine->currentEnvironment()) {
        return environment->sandboxURL;
    }
    return _currentSandboxURL;
}

// Classic code run for an entity or sandbox gets an environment that carries them, so that a promise continuation that
// resumes outside this call still has them: after awaiting a promise settled from the event loop (fetch()) or by
// another script's handler. The current one is reused for nested calls with the same entity and sandbox.
static ScriptEnvironmentPointer classicEnvironment(ScriptEngine* engine, const EntityItemID& entityID,
                                                   const QUrl& sandboxURL) {
    if (entityID.isInvalidID() && sandboxURL.isEmpty()) {
        return ScriptEnvironmentPointer();
    }
    ScriptEnvironmentPointer current = engine->currentEnvironment();
    if (current && current->classic && current->entityID == entityID && current->sandboxURL == sandboxURL) {
        return current;
    }
    return std::make_shared<ScriptEnvironment>(entityID, sandboxURL, 0, true);
}

// Execute operation in the appropriate context for (the possibly empty) entityID.
// Even if entityID is supplied as currentEntityIdentifier, this still documents the source
// of the code being executed (e.g., if we ever sandbox different entity scripts, or provide different
// global values for different entity scripts).
void ScriptManager::doWithEnvironment(const EntityItemID& entityID, const QUrl& sandboxURL, std::function<void()> operation,
                                      const ScriptEnvironmentPointer& environment) {
    EntityItemID oldIdentifier = _currentEntityIdentifier;
    QUrl oldSandboxURL = _currentSandboxURL;
    _currentEntityIdentifier = entityID;
    _currentSandboxURL = sandboxURL;
    ScriptEnvironmentPointer current = environment;
    if (!current) {
        current = classicEnvironment(_engine.get(), entityID, sandboxURL);
    }
    ScriptEnvironmentScope environmentScope(_engine.get(), current);

#if DEBUG_CURRENT_ENTITY
    ScriptValue oldData = this->globalObject().property("debugEntityID");
    this->globalObject().setProperty("debugEntityID", entityID.toScriptValue(this)); // Make the entityID available to javascript as a global.
    operation();
    this->globalObject().setProperty("debugEntityID", oldData);
#else
    operation();
#endif
    _currentEntityIdentifier = oldIdentifier;
    _currentSandboxURL = oldSandboxURL;
}

void ScriptManager::callWithEnvironment(const EntityItemID& entityID, const QUrl& sandboxURL, const ScriptValue& function,
                                        const ScriptValue& thisObject, const ScriptValueList& args,
                                        const ScriptEnvironmentPointer& environment) {
    auto operation = [&]() {
        function.call(thisObject, args);
    };
    doWithEnvironment(entityID, sandboxURL, operation, environment);
}

void ScriptManager::noteClassicLoad(const QUrl& url, const ScriptEnvironmentPointer& environment) {
    if constexpr (CLASSIC_LOADS_TAINT_ENVIRONMENT) {
        if (environment) {
            environment->noteLoaded(url);
        }
    }
}
