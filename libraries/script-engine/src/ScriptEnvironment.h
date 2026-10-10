//
//  ScriptEnvironment.h
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_ScriptEnvironment_h
#define overte_ScriptEnvironment_h

#include <memory>

#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include "EntityItemID.h"
#include "ScriptEngine.h"

/// Design question Q3 of #10: whether files that ES module code pulls in with Script.include and Script.require count
/// towards its environment's loaded URLs. Off: they keep their classic behaviour. Turning this on also needs classic
/// scripts to get an environment of their own before it affects them, see ScriptManager::noteClassicLoad().
inline constexpr bool CLASSIC_LOADS_TAINT_ENVIRONMENT { false };

/// [ScriptInterface] Whose code is running, carried across <code>await</code>: the engine stores the current environment
/// with every promise continuation and restores it when the continuation runs, and ScriptManager stores it with every
/// timer and handler created while it is current.
///
/// ES module code always runs in one: a top-level module script has its own, and so does each import() made by classic
/// code. Classic script code runs without one and uses ScriptManager's entity and sandbox members, as it always has.
///
/// The entity, sandbox and epoch never change. The loaded URLs only grow: a module that import()s untrusted code
/// shares bindings with it, so continuations captured before the import must not stay more trusted than ones after it.
class ScriptEnvironment {
public:
    ScriptEnvironment(const EntityItemID& entityID, const QUrl& sandboxURL, int epoch) :
        entityID(entityID), sandboxURL(sandboxURL), epoch(epoch) {}

    /// The entity whose script this is; null outside entity scripts
    const EntityItemID entityID;
    /// Top-level URL of the script; local files outside its directory may not be loaded unless it is local itself
    const QUrl sandboxURL;
    /// Module map generation of the environment's modules, see ScriptModuleRequest::epoch
    const int epoch;

    /// Records a URL whose code was loaded into the environment
    void noteLoaded(const QUrl& url) { _loadedURLs.insert(url.toString()); }
    /// Every URL whose code was loaded into the environment; ScriptPermissions trusts the environment only if it
    /// trusts all of them
    const QSet<QString>& loadedURLs() const { return _loadedURLs; }

private:
    QSet<QString> _loadedURLs;
};

/// Makes an environment current, or none (classic code) for a null one, until the scope ends
class ScriptEnvironmentScope {
public:
    ScriptEnvironmentScope(ScriptEngine* engine, const ScriptEnvironmentPointer& environment) :
        _engine(engine), _previous(engine->swapCurrentEnvironment(environment)) {}
    ~ScriptEnvironmentScope() { _engine->swapCurrentEnvironment(_previous); }
    ScriptEnvironmentScope(const ScriptEnvironmentScope&) = delete;
    ScriptEnvironmentScope& operator=(const ScriptEnvironmentScope&) = delete;

private:
    ScriptEngine* _engine;
    ScriptEnvironmentPointer _previous;
};

#endif  // overte_ScriptEnvironment_h

/// @}
