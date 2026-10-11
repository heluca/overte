//
//  ScriptModuleEntityTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleEntityTests_h
#define overte_ScriptModuleEntityTests_h

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtTest/QtTest>

#include "EntityScriptUtils.h"
#include "ScriptEngine.h"
#include "ScriptManager.h"
#include "ScriptValue.h"

// Exposed to test scripts as Probe
class EntityModuleProbe : public QObject {
    Q_OBJECT
public:
    explicit EntityModuleProbe(ScriptManager* manager) : _manager(manager) {}

    // Loads an entity script as the entity script server does: ScriptManager::loadEntityScript() names the epoch and
    // marks it LOADING, and the fetched source is handed to entityScriptContentAvailable() from the event loop (a test
    // manager does not drain loadEntityScript's own queue). The promise resolves with the status once it is neither
    // pending nor loading, followed by ": " and the error information if there is any.
    Q_INVOKABLE ScriptValue load(const QString& entityID, const QString& url, bool reload);
    // Empty without details
    Q_INVOKABLE QString status(const QString& entityID, const QString& url) const {
        EntityScriptDetails details;
        bool found = _manager->getEntityScriptDetails(EntityItemID(QUuid(entityID)), url, details);
        return found ? EntityScriptStatus_::valueToKey(details.status).toLower() : QString();
    }
    // ScriptManager::unloadEntityScript(), removing the entity from the map as for a deleted entity
    Q_INVOKABLE void unload(const QString& entityID, const QString& url) {
        _manager->unloadEntityScript(EntityItemID(QUuid(entityID)), url, true);
    }
    // ScriptManager::callEntityScriptMethod() from the event loop, as an entity script server calls it, as a remote call
    // when remoteCallerID is not empty. The promise resolves once the call has returned.
    Q_INVOKABLE ScriptValue call(const QString& entityID, const QString& method, const QStringList& params,
                                 const QString& remoteCallerID);
    // Whether load() has handed the entity's source to entityScriptContentAvailable()
    Q_INVOKABLE bool handedOver(const QString& entityID) const { return _handedOver.contains(entityID); }
    // Test-only: the engine's module records, all epochs (ScriptEngineMemoryStatistics::moduleRecords)
    Q_INVOKABLE int moduleRecords() const {
        return static_cast<int>(_manager->engine()->getMemoryUsageStatistics().moduleRecords);
    }
    // ScriptManager::currentEntityIdentifier(), empty when null
    Q_INVOKABLE QString entityID() const {
        EntityItemID id = _manager->currentEntityIdentifier();
        return id.isNull() ? QString() : id.toString();
    }
    Q_INVOKABLE QString sandboxURL() const { return _manager->currentSandboxURL().toString(); }
    Q_INVOKABLE void writeFile(const QString& url, const QString& contents);

    // The entity of each errorEntityMessage, in order; empty for none
    QStringList errorEntities;

private:
    ScriptManager* _manager;
    QSet<QString> _handedOver;
};

// ES module entity scripts and serverScripts (#11): construction, shared instances, epochs and reload, unload
class ScriptModuleEntityTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testDefaultExportClass();
    void testDefaultExportNotConstructor();
    void testLoadFailureReported();
    void testTopLevelAwaitKeepsLoading();
    void testSharedInstance();
    void testReloadNewEpoch();
    void testUnloadReleasesEpochAndReportsStalledAwait();
    void testRemotelyCallableClassField();
    void testDynamicImportAfterAwait();
    void testRejectedAsyncMethodReported();
    void testTimerAfterAwaitClearedOnUnload();
    void testClassicEntityUnchanged();
};

#endif // overte_ScriptModuleEntityTests_h
