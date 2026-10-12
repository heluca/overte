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

#include "FetchClass.h"
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

// Entity scripts share one realm, so a script's environment does not say whose code is running: another script's
// replacement for a built-in runs in it when the script calls that built-in. Every script frame on the stack counts, by
// the name it was compiled with, as ScriptPermissions does for module code; a frame without a name (an embedded entity
// script, new Function) is under no prefix, and neither is a call with no script frame at all. An ES module
// environment's loaded URLs count too: a script that imports code from elsewhere shares its bindings with that code.
QList<QUrl> ScriptManager::scriptCredentialScope() const {
    const QUrl sandboxURL = currentSandboxURL();
    QList<QUrl> scope { sandboxURL.isEmpty() ? QUrl(_fileNameString) : sandboxURL };
    auto environment = _engine->currentEnvironment();
    if (environment && !environment->classic) {
        for (const QString& url : environment->loadedURLs()) {
            scope.append(QUrl(url));
        }
    }
    bool hasScriptFrame = false;
    for (const QString& frame : _engine->getCurrentScriptURLs(true)) {
        if (frame != FETCH_PRELUDE_NAME) {
            scope.append(QUrl(frame));
            hasScriptFrame = true;
        }
    }
    if (!hasScriptFrame) {
        scope.append(QUrl());
    }
    return scope;
}

// Classic code run for an entity or sandbox gets an environment that carries them, so that a promise continuation that
// resumes outside this call still has them: after awaiting a promise settled from the event loop (fetch()) or by
// another script's handler. An entity keeps one until it unloads, and the current one is reused for nested calls.
ScriptEnvironmentPointer ScriptManager::classicEnvironment(const EntityItemID& entityID, const QUrl& sandboxURL) {
    if (entityID.isInvalidID() && sandboxURL.isEmpty()) {
        return ScriptEnvironmentPointer();
    }
    ScriptEnvironmentPointer current = _engine->currentEnvironment();
    if (current && current->classic && current->entityID == entityID && current->sandboxURL == sandboxURL) {
        return current;
    }
    if (entityID.isInvalidID()) {
        return std::make_shared<ScriptEnvironment>(entityID, sandboxURL, 0, true);
    }
    ScriptEnvironmentPointer& cached = _classicEnvironments[entityID];
    if (!cached || cached->sandboxURL != sandboxURL) {
        cached = std::make_shared<ScriptEnvironment>(entityID, sandboxURL, 0, true);
    }
    return cached;
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
        current = classicEnvironment(entityID, sandboxURL);
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
