//
//  ScriptModuleEnvTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleEnvTests.h"

#include <memory>

#include "AccountManager.h"
#include "AddressManager.h"
#include "DomainAccountManager.h"
#include "HeldScriptServer.h"
#include "NodeList.h"
#include "ScriptEnvironment.h"
#include "ScriptModuleTestRunner.h"
#include "ScriptPermissions.h"
#include "v8/ScriptEngineV8.h"

QTEST_MAIN(ScriptModuleEnvTests)

using Options = ModuleTestOptions;

static const QString ENTITY = "{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}";
// Never fetched: the tests hand the source over. Not a file: URL, which ScriptManager would check for changes.
static const QString ENTITY_URL = "http://127.0.0.1:1/entity.js";

QString EnvironmentProbe::entityID() const {
    EntityItemID id = _manager->currentEntityIdentifier();
    return id.isNull() ? QString() : id.toString();
}

QString EnvironmentProbe::sandboxURL() const {
    return _manager->currentSandboxURL().toString();
}

bool EnvironmentProbe::allowed() const {
    return ScriptPermissions::isCurrentScriptAllowed(ScriptPermissions::Permission::SCRIPT_PERMISSION_GET_AVATAR_URL);
}

ScriptValue EnvironmentProbe::withModuleEnvironment(const QString& entityID, const QString& sandboxURL,
                                                    const ScriptValue& function) {
    auto environment = std::make_shared<ScriptEnvironment>(EntityItemID(QUuid(entityID)), QUrl(sandboxURL), 0);
    environment->noteLoaded(QUrl(sandboxURL));
    ScriptEnvironmentScope environmentScope(_manager->engine().get(), environment);
    return function.call();
}

ScriptValue EnvironmentProbe::loadClassicEntity(const QString& entityID, const QString& url, const QString& source) {
    ScriptPromise promise = _manager->engine()->newPromise();
    ScriptManager* manager = _manager;
    EntityItemID id { QUuid(entityID) };
    auto resolver = promise.resolver;
    QMetaObject::invokeMethod(manager, [manager, id, url, source, resolver] {
        bool invoked = QMetaObject::invokeMethod(manager, "entityScriptContentAvailable", Qt::DirectConnection,
            Q_ARG(EntityItemID, id), Q_ARG(QString, url), Q_ARG(QString, source), Q_ARG(bool, true), Q_ARG(bool, true),
            Q_ARG(QString, QString("OK")));
        if (invoked) {
            resolver->resolve(QVariant(true));
        } else {
            resolver->reject(QString("entityScriptContentAvailable could not be invoked"));
        }
    }, Qt::QueuedConnection);
    return promise.promise;
}

void EnvironmentProbe::unloadEntity(const QString& entityID, const QString& url) {
    _manager->unloadEntityScript(EntityItemID(QUuid(entityID)), url);
}

static Options withProbe(std::unique_ptr<EnvironmentProbe>& probe, Options options = Options()) {
    options.beforeRun = [&probe](ScriptManager* manager) {
        probe = std::make_unique<EnvironmentProbe>(manager);
        manager->engine()->globalObject().setProperty("Probe", manager->engine()->newQObject(probe.get()));
    };
    return options;
}

void ScriptModuleEnvTests::initTestCase() {
    // Loading an entity script consults the domain, for the entity script allowlist
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    initModuleTestDependencies();
}

void ScriptModuleEnvTests::testEnvironmentSelfTest() {
    // R1 of the design in #10: this V8 build must carry the embedder data across await, or every test below fails
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, "", "about:selftest");
    auto engine = std::dynamic_pointer_cast<ScriptEngineV8>(sm->engine());
    QVERIFY(engine);
    QVERIFY(engine->isEnvironmentCarriedAcrossAwait());
}

void ScriptModuleEnvTests::testTimerAfterAwaitKeepsModuleSandbox() {
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "print('body ' + Probe.sandboxURL());\n"
          "await new Promise(function(resolve) { Script.setTimeout(resolve, 0); });\n"
          "print('after await ' + Probe.sandboxURL());\n"
          "Script.setTimeout(function() {\n"
          "    print('timer ' + Probe.sandboxURL());\n"
          "    Script.stop(true);\n"
          "}, 0);\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString main = urlOf(dir, "main.mjs");
    QCOMPARE(run.printed, QStringList({ "body " + main, "after await " + main, "timer " + main }));

    // A classic top-level script has no sandbox, before and after this change
    QTemporaryDir classicDir;
    auto classic = runScript(classicDir, {
        { "classic.js",
          "Promise.resolve().then(function() {\n"
          "    Script.setTimeout(function() { print('timer [' + Probe.sandboxURL() + ']'); Script.stop(true); }, 0);\n"
          "});\n" },
    }, withProbe(probe));
    QVERIFY(!classic.timedOut);
    QCOMPARE(classic.errors, QStringList());
    QCOMPARE(classic.printed, QStringList({ "timer []" }));
}

void ScriptModuleEnvTests::testClassicEntityTimerKeepsEntity() {
    // Regression: a classic entity script's timers, also one set from a promise continuation, carry its entity
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    QString entity =
        "(function() {\n"
        "    this.preload = function(entityID) {\n"
        "        print('preload ' + Probe.entityID());\n"
        "        Script.setTimeout(function() { print('timer ' + Probe.entityID()); }, 0);\n"
        "        Promise.resolve().then(function() {\n"
        "            Script.setTimeout(function() {\n"
        "                print('continuation timer ' + Probe.entityID() + ' ' + Probe.sandboxURL());\n"
        "                Script.stop(true);\n"
        "            }, 0);\n"
        "        });\n"
        "    };\n"
        "})\n";
    auto run = runScript(dir, {
        { "main.js", "Probe.loadClassicEntity('" + ENTITY + "', '" + ENTITY_URL + "', `" + entity + "`);\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "preload " + ENTITY, "timer " + ENTITY,
                                        "continuation timer " + ENTITY + " " + ENTITY_URL }));
}

void ScriptModuleEnvTests::testEntityEnvironmentAcrossAwait() {
    // Entity .mjs scripts arrive with #11; until then the environment is entered through the test-only hook. What is
    // tested is what the engine and ScriptManager do with it: the continuation after await, a timer and a signal
    // handler made there run in it, while the code around the hook runs in the top-level module's own.
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "const entityDone = new Promise(function(resolve) {\n"
          "    Probe.withModuleEnvironment('" + ENTITY + "', '" + ENTITY_URL + "', async function() {\n"
          "        print('entered ' + Probe.entityID() + ' ' + Probe.sandboxURL());\n"
          "        await new Promise(function(r) { Script.setTimeout(r, 0); });\n"
          "        print('after await ' + Probe.entityID());\n"
          "        Probe.ping.connect(function() { print('signal ' + Probe.entityID() + ' ' + Probe.sandboxURL()); });\n"
          "        Script.setTimeout(function() { print('timer ' + Probe.entityID()); resolve(); }, 0);\n"
          "    });\n"
          "});\n"
          "print('outside [' + Probe.entityID() + ']');\n"
          "await entityDone;\n"
          "print('module [' + Probe.entityID() + ']');\n"
          "Probe.emitPing();\n"
          "Script.stop(true);\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "entered " + ENTITY + " " + ENTITY_URL, "outside []", "after await " + ENTITY,
                                        "timer " + ENTITY, "module []", "signal " + ENTITY + " " + ENTITY_URL }));
}

void ScriptModuleEnvTests::testModuleTimerClearedOnEntityUnload() {
    // A timer set after an await in the entity's environment is stopped with the entity's classic timers. If it were
    // not, it would fire before the later timer that ends the script.
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "await Probe.loadClassicEntity('" + ENTITY + "', '" + ENTITY_URL + "',\n"
          "    \"(function() { this.preload = function() { print('preload ' + Probe.entityID()); }; })\");\n"
          "await new Promise(function(resolve) {\n"
          "    Probe.withModuleEnvironment('" + ENTITY + "', '" + ENTITY_URL + "', async function() {\n"
          "        await new Promise(function(r) { Script.setTimeout(r, 0); });\n"
          "        Script.setTimeout(function() { print('module timer after unload'); }, 0);\n"
          "        Probe.unloadEntity('" + ENTITY + "', '" + ENTITY_URL + "');\n"
          "        resolve();\n"
          "    });\n"
          "});\n"
          "Script.setTimeout(function() { print('end'); Script.stop(true); }, 0);\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "preload " + ENTITY, "end" }));
}

void ScriptModuleEnvTests::testDynamicImportAfterAwaitUsesEnvironmentSandbox() {
    // In an environment whose sandbox is remote, import() after an await may not load a local file outside it, although
    // the importing module and the manager's script are local. In the top-level module's environment it may.
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "await new Promise(function(resolve) {\n"
          "    Probe.withModuleEnvironment('', 'http://127.0.0.1:1/remote/entity.js', async function() {\n"
          "        await new Promise(function(r) { Script.setTimeout(r, 0); });\n"
          "        try {\n"
          "            await import('./local.mjs');\n"
          "            print('remote sandbox imported');\n"
          "        } catch (error) {\n"
          "            print('remote sandbox refused: ' + error.message.includes('outside of origin script'));\n"
          "        }\n"
          "        resolve();\n"
          "    });\n"
          "});\n"
          "await new Promise(function(r) { Script.setTimeout(r, 0); });\n"
          "const ns = await import('./local.mjs');\n"
          "print('own sandbox imported ' + ns.secret);\n"
          "Script.stop(true);\n" },
        { "local.mjs", "export const secret = 'local';\n" },
    }, withProbe(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "remote sandbox refused: true", "own sandbox imported local" }));
}

void ScriptModuleEnvTests::testFramelessNativeCall() {
    // A gated native passed straight to then() runs with no script frame. Module code: refused, as nothing says which
    // of the environment's URLs chose it. Classic code: as before, the manager's URL decides.
    std::unique_ptr<EnvironmentProbe> probe;
    QTemporaryDir dir;
    auto module = runScript(dir, {
        { "main.mjs",
          "print('direct ' + Probe.allowed());\n"
          "const frameless = await Promise.resolve().then(Probe.allowed);\n"
          "print('frameless ' + frameless);\n"
          "Script.stop(true);\n" },
    }, withProbe(probe));
    QVERIFY(!module.timedOut);
    QCOMPARE(module.errors, QStringList());
    QCOMPARE(module.printed, QStringList({ "direct true", "frameless false" }));

    QTemporaryDir classicDir;
    auto classic = runScript(classicDir, {
        { "classic.js",
          "print('direct ' + Probe.allowed());\n"
          "Promise.resolve().then(Probe.allowed).then(function(frameless) {\n"
          "    print('frameless ' + frameless);\n"
          "    Script.stop(true);\n"
          "});\n" },
    }, withProbe(probe));
    QVERIFY(!classic.timedOut);
    QCOMPARE(classic.errors, QStringList());
    QCOMPARE(classic.printed, QStringList({ "direct true", "frameless true" }));
}

void ScriptModuleEnvTests::testUntrustedModuleRefused() {
    // A local (trusted) entry that loads a module over http: from then on its environment is refused gated APIs, also
    // in a timer, where only trusted frames are on the stack. Classic code that import()s it keeps its own rule.
    HeldScriptServer server("export const value = 'untrusted';\n");
    auto untrusted = [&server](const QString& name) { return QString(server.url()).replace("held.js", name); };
    std::unique_ptr<EnvironmentProbe> probe;

    QTemporaryDir dynamicDir;
    auto dynamic = runScript(dynamicDir, {
        { "main.mjs",
          "print('before ' + Probe.allowed());\n"
          "const ns = await import('" + untrusted("dynamic.mjs") + "');\n"
          "print('imported ' + ns.value);\n"
          "print('after ' + Probe.allowed());\n"
          "Script.setTimeout(function() { print('timer ' + Probe.allowed()); Script.stop(true); }, 0);\n" },
    }, withProbe(probe));
    QVERIFY(!dynamic.timedOut);
    QCOMPARE(dynamic.errors, QStringList());
    QCOMPARE(dynamic.printed, QStringList({ "before true", "imported untrusted", "after false", "timer false" }));

    QTemporaryDir staticDir;
    auto staticImport = runScript(staticDir, {
        { "main.mjs",
          "import { value } from '" + untrusted("static.mjs") + "';\n"
          "print('static ' + value + ' ' + Probe.allowed());\n"
          "Script.stop(true);\n" },
    }, withProbe(probe));
    QVERIFY(!staticImport.timedOut);
    QCOMPARE(staticImport.errors, QStringList());
    QCOMPARE(staticImport.printed, QStringList({ "static untrusted false" }));

    // Design question Q3 of #10, left open: classic code is still judged by its own frames
    QTemporaryDir classicDir;
    auto classic = runScript(classicDir, {
        { "classic.js",
          "import('" + untrusted("classic.mjs") + "').then(function(ns) {\n"
          "    print('classic after import ' + ns.value + ' ' + Probe.allowed());\n"
          "    Script.stop(true);\n"
          "});\n" },
    }, withProbe(probe));
    QVERIFY(!classic.timedOut);
    QCOMPARE(classic.errors, QStringList());
    QCOMPARE(classic.printed, QStringList({ "classic after import untrusted true" }));
}
