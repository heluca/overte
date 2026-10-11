//
//  ScriptManagerModuleEntity.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// Entity scripts and serverScripts whose URL ends in .mjs: ES modules whose default export is the constructor, the
// contract of a classic entity script's (function () { ... }). Entity scripts with the same URL share one module
// instance per module map epoch, so module-level state is shared and per-entity state lives in the constructed object.
// A reload loads the graph under a new epoch; an epoch's modules are dropped when its last entity script unloads.

#include "ScriptManager.h"

#include "ScriptCache.h"
#include "ScriptEngine.h"
#include "ScriptEnvironment.h"
#include "ScriptException.h"
#include "ScriptModuleResolver.h"

int ScriptManager::entityModuleEpoch(const QString& scriptURL, bool forceRedownload) {
    if (!isModuleURL(QUrl(scriptURL))) {
        return 0;
    }
    auto current = _entityModuleEpochs.find(scriptURL);
    bool join = current != _entityModuleEpochs.end()
        && (!forceRedownload || (current->forceDownload && !current->loadStarted));
    if (!join) {
        int epoch = _engine->newModuleEpoch();
        _entityModuleEpochsInUse.insert(epoch);
        current = _entityModuleEpochs.insert(scriptURL, { epoch, forceRedownload, false });
    }
    return current->epoch;
}

void ScriptManager::loadEntityModule(const EntityItemID& entityID, const QString& scriptURL, const QString& contents,
                                     const QString& fetchStatus, int64_t lastModified) {
    // Only the classic path evaluates under the parent URL entityScriptContentAvailable set
    setParentURL("");
    EntityScriptDetails details;
    getEntityScriptDetails(entityID, scriptURL, details);
    if (details.moduleEpoch == 0) {
        // Content handed over without loadEntityScript(), which names the epoch while the entity is LOADING
        details.moduleEpoch = entityModuleEpoch(scriptURL, false);
        details.status = EntityScriptStatus::LOADING;
        setEntityScriptDetails(entityID, scriptURL, details);
    }
    int epoch = details.moduleEpoch;
    auto current = _entityModuleEpochs.find(scriptURL);
    bool startsEpoch = current != _entityModuleEpochs.end() && current->epoch == epoch && !current->loadStarted;
    if (startsEpoch) {
        current->loadStarted = true;
    }
    // The graph is fetched as freshly as its entry was: from the network after a reload, a first load or a changed URL
    // (a cache-busting query on the entry alone would otherwise leave its imports as ScriptCache had them)
    bool forceDownload = startsEpoch && fetchStatus != ScriptCache::STATUS_CACHED;

    QUrl url(scriptURL);
    QUrl sandboxURL = currentSandboxURL().isEmpty() ? url : currentSandboxURL();
    auto environment = std::make_shared<ScriptEnvironment>(entityID, sandboxURL, epoch);
    // A second entity script with the URL joins the graph already in the epoch, in flight or evaluated, and waits for
    // its top-level await if there is one
    _engine->loadModule({ url, contents, sandboxURL, epoch, forceDownload, environment },
        [this, entityID, scriptURL, epoch, environment, lastModified](const ScriptValue& moduleNamespace,
                                                                      std::shared_ptr<ScriptException> error) {
            entityModuleEvaluated(entityID, scriptURL, epoch, environment, lastModified, moduleNamespace, error);
        });
}

void ScriptManager::entityModuleEvaluated(const EntityItemID& entityID, const QString& scriptURL, int epoch,
                                          const ScriptEnvironmentPointer& environment, int64_t lastModified,
                                          const ScriptValue& moduleNamespace, std::shared_ptr<ScriptException> error) {
    EntityScriptDetails loading;
    if (!getEntityScriptDetails(entityID, scriptURL, loading) || loading.status != EntityScriptStatus::LOADING
        || loading.moduleEpoch != epoch) {
        // Unloaded, or loaded again, while the graph loaded
        return;
    }

    EntityScriptDetails newDetails;
    newDetails.scriptText = scriptURL;
    auto setError = [&](const QString& errorInfo) {
        newDetails.errorInfo = errorInfo;
        newDetails.status = EntityScriptStatus::ERROR_RUNNING_SCRIPT;
        setEntityScriptDetails(entityID, scriptURL, newDetails);
        releaseUnusedEntityModuleEpochs();
    };

    if (error) {
        // The loader has reported it already. A thrown Error's stack names its file and line; the other messages do.
        setError(error->backtrace.isEmpty() ? error->errorMessage : error->backtrace.join("\n"));
        return;
    }

    ScriptValue constructor = moduleNamespace.property("default");
    if (!constructor.isFunction()) {
        QString message = QString("Could not find constructor (%1 has no default export that is a class or function)")
            .arg(scriptURL);
        doWithEnvironment(entityID, environment->sandboxURL, [&] { scriptErrorMessage(message, scriptURL, -1); },
                          environment);
        setError(message);
        return;
    }

    ScriptValue entityScriptObject;
    doWithEnvironment(entityID, environment->sandboxURL, [&] { entityScriptObject = constructor.construct(); },
                      environment);
    if (!entityScriptObject.isObject()) {
        // construct() has logged why
        QString message = QString("Could not construct the default export of %1").arg(scriptURL);
        doWithEnvironment(entityID, environment->sandboxURL, [&] { scriptErrorMessage(message, scriptURL, -1); },
                          environment);
        setError(message);
        return;
    }

    newDetails.status = EntityScriptStatus::RUNNING;
    newDetails.scriptObject = entityScriptObject;
    newDetails.lastModified = lastModified;
    newDetails.definingSandboxURL = environment->sandboxURL;
    newDetails.moduleEpoch = epoch;
    newDetails.environment = environment;
    setEntityScriptDetails(entityID, scriptURL, newDetails);

    callEntityScriptMethodForScript(entityID, scriptURL, "preload");
    emit entityScriptPreloadFinished(entityID);
}

void ScriptManager::releaseUnusedEntityModuleEpochs() {
    if (_entityModuleEpochsInUse.isEmpty()) {
        return;
    }
    QSet<int> used;
    {
        QReadLocker locker { &_entityScriptsLock };
        for (const auto& scripts : _entityScripts) {
            for (const auto& details : scripts) {
                if (details.moduleEpoch != 0) {
                    used.insert(details.moduleEpoch);
                }
            }
        }
    }
    QSet<int> unused = _entityModuleEpochsInUse;
    unused.subtract(used);
    for (int epoch : unused) {
        _engine->releaseModuleEpoch(epoch);
        _entityModuleEpochsInUse.remove(epoch);
    }
    for (auto it = _entityModuleEpochs.begin(); it != _entityModuleEpochs.end();) {
        if (unused.contains(it->epoch)) {
            it = _entityModuleEpochs.erase(it);
        } else {
            ++it;
        }
    }
}

void ScriptManager::callEntityScriptFunction(const EntityItemID& entityID, const EntityScriptDetails& details,
                                             const QString& methodName, const ScriptValue& entityScript,
                                             const ScriptValueList& args) {
    QString scriptURL = details.scriptText;
    QUrl sandboxURL = details.definingSandboxURL;
    ScriptEnvironmentPointer environment = details.environment;
    std::weak_ptr<ScriptManager> weakSelf = shared_from_this();
    // An async method's rejection would otherwise surface as an unhandled rejection, from the event loop, without its
    // entity
    auto onRejected = [weakSelf, entityID, sandboxURL, environment, scriptURL, methodName](
                          const QString& reason, const QString& fileName, int lineNumber) {
        auto self = weakSelf.lock();
        if (!self) {
            return;
        }
        QString message = QString("Uncaught (in promise) in entity script method %1 of %2: %3")
            .arg(methodName, scriptURL, reason);
        self->doWithEnvironment(entityID, sandboxURL, [&] {
            self->scriptErrorMessage(message, fileName.isEmpty() ? scriptURL : fileName, lineNumber);
        }, environment);
    };
    doWithEnvironment(entityID, sandboxURL, [&] {
        _engine->callReportingRejection(entityScript.property(methodName), entityScript, args, onRejected);
    }, environment);
}
