//
//  PromiseApiTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "PromiseApiTests.h"

#include <memory>

#include "AccountManager.h"
#include "AddressManager.h"
#include "AssetClient.h"
#include "AssetScriptingInterface.h"
#include "DomainAccountManager.h"
#include "NodeList.h"
#include "ResourceCache.h"
#include "ScriptEnvironment.h"
#include "ScriptModuleTestRunner.h"
#include "ScriptValueUtils.h"
#include <recording/ClipCache.h>
#include <recording/RecordingScriptingInterface.h>

QTEST_MAIN(PromiseApiTests)

using Options = ModuleTestOptions;

static const QString ENTITY = "{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}";
static const QString ENTITY_URL = "http://127.0.0.1:1/entity.js";
// A valid hash, so that only the missing asset server fails the requests
static const QString HASH = QString(64, 'a');
static const QString HASH_SCRIPT = "const HASH = '" + HASH + "';\n";
// The asset server never answers in these tests: there is none
static const QString NO_SERVER = "Unable to communicate with Asset Server";
static const QString NO_SERVER_UPLOAD =
    "There was a problem reaching your Asset Server. Please check your network connectivity.";

QString PromiseApiProbe::sandboxURL() const {
    return _manager->currentSandboxURL().toString();
}

QString PromiseApiProbe::entityID() const {
    EntityItemID id = _manager->currentEntityIdentifier();
    return id.isNull() ? QString() : id.toString();
}

ScriptValue PromiseApiProbe::withModuleEnvironment(const QString& entityID, const QString& sandboxURL,
                                                   const ScriptValue& function) {
    auto environment = std::make_shared<ScriptEnvironment>(EntityItemID(QUuid(entityID)), QUrl(sandboxURL), 0);
    environment->noteLoaded(QUrl(sandboxURL));
    ScriptEnvironmentScope environmentScope(_manager->engine().get(), environment);
    return function.call();
}

ScriptValue PromiseApiProbe::later(const ScriptValue& scope, const ScriptValue& callback) {
    kept.emplace(_manager->engine(), scope, callback);
    return kept->takeReturnValue();
}

void PromiseApiProbe::completeLater(const QString& error, const QVariantMap& result) {
    QTimer::singleShot(0, this, [this, error, result] { kept->complete(error, result); });
}

int PromiseApiProbe::pendingResolvers() const {
    return static_cast<int>(_manager->engine()->getMemoryUsageStatistics().pendingPromiseResolvers);
}

static Options withGlobals(std::unique_ptr<PromiseApiProbe>& probe, Options options = Options()) {
    options.beforeRun = [&probe](ScriptManager* manager) {
        auto engine = manager->engine();
        // The QByteArray conversions, which an entities library initializer registers outside these tests
        registerMetaTypes(engine.get());
        probe = std::make_unique<PromiseApiProbe>(manager);
        engine->globalObject().setProperty("Probe", engine->newQObject(probe.get()));
        // A NETWORKLESS_TEST_SCRIPT manager registers no Assets. Parented to the manager, as its own would be.
        engine->globalObject().setProperty("Assets", engine->newQObject(new AssetScriptingInterface(manager)));
        engine->globalObject().setProperty("Recording",
            engine->newQObject(DependencyManager::get<RecordingScriptingInterface>().data()));
    };
    return options;
}

static QStringList sorted(QStringList lines) {
    lines.sort();
    return lines;
}

void PromiseApiTests::initTestCase() {
    // A real AssetClient with no asset server: every request fails at once, as in an agent before the domain connects
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    DependencyManager::set<AssetClient>();
    initModuleTestDependencies();
    DependencyManager::set<ResourceCacheSharedItems>();
    DependencyManager::set<recording::ClipCache>();
    DependencyManager::set<RecordingScriptingInterface>();
}

void PromiseApiTests::testCallbackForms() {
    // Regression: each converted method, given a callback, calls it with the arguments and this it always did, and
    // returns undefined. The overload taken follows the argument count, as moc clones each defaulted parameter.
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.js", HASH_SCRIPT + R"JS(
var scope = { done: function(error, result) { print('compress scoped ' + (this === scope) + ' ' + error + ' ' + result.compressed); } };
print('returns ' + typeof Assets.compressData('hello', scope, 'done'));
Assets.compressData({ data: 'hello hello hello', level: 9 }, function(error, compressed) {
    print('compress ' + error + ' ' + compressed.compressed + ' ' + (compressed.data instanceof ArrayBuffer));
    Assets.decompressData({ data: compressed.data }, function(error, result) { print('decompress ' + error + ' ' + result.response); });
});
Assets.resolveAsset('atp:' + HASH, function(error, result) { print('resolve ' + error + ' ' + (result.hash === HASH)); });
Assets.getMapping('/a.txt', function(error, hash) { print('getMapping ' + (this === Assets) + ' ' + error + ' [' + hash + ']'); });
Assets.setMapping('/a.txt', HASH, function(error, result) { print('setMapping ' + (this === Assets) + ' ' + error + ' ' + typeof result.error); });
Assets.getAsset({ url: '/a.txt' }, { callback: function(error, result) { print('getAsset ' + error + ' ' + typeof result.error); } });
Assets.putAsset({ data: 'hello', path: '/a.txt' }, function(error, result) { print('putAsset ' + error + ' ' + typeof result.error); });
try { Assets.getAsset({ url: 'not a path' }, function() { print('never'); }); } catch (e) { print('throws ' + e.name + ': ' + e.message); }
Script.setTimeout(function() { Script.stop(true); }, 10);
)JS" },
    }, withGlobals(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(sorted(run.printed), sorted({
        "returns undefined",
        "compress scoped true null true",
        "compress null true true",
        "decompress null hello hello hello",
        "resolve null true",
        "getMapping true " + NO_SERVER + " []",
        "setMapping true " + NO_SERVER + " number",
        "getAsset " + NO_SERVER + " number",
        "putAsset " + NO_SERVER_UPLOAD + " number",
        "throws Error: Invalid ATP url 'not a path'",
    }));
}

void PromiseApiTests::testPromiseForms() {
    // Without a callback each method returns a promise that settles with what the callback gets
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", HASH_SCRIPT + R"JS(
function shape(value) {
    return JSON.stringify(value, (key, v) => key === '_compressMS' ? undefined
        : v instanceof ArrayBuffer ? 'ArrayBuffer(' + v.byteLength + ')' : v);
}
const viaCallback = (call) => new Promise(resolve => call((error, result) => resolve({ error, result })));
const pending = Assets.compressData({ data: 'hello hello hello', level: 9 });
print('compress returns a promise ' + (pending instanceof Promise));
const compressed = await pending;
const compressedCallback = await viaCallback(cb => Assets.compressData({ data: 'hello hello hello', level: 9 }, cb));
print('compress ' + compressed.compressed + ' same shape ' + (shape(compressed) === shape(compressedCallback.result)));
const decompressed = await Assets.decompressData({ data: compressed.data }, undefined, undefined);
const decompressedCallback = await viaCallback(cb => Assets.decompressData({ data: compressed.data }, cb));
print('decompress ' + decompressed.response + ' same shape ' + (shape(decompressed) === shape(decompressedCallback.result)));
const resolved = await Assets.resolveAsset({ url: 'atp:' + HASH }, undefined);
const resolvedCallback = await viaCallback(cb => Assets.resolveAsset({ url: 'atp:' + HASH }, cb));
print('resolve ' + (resolved.hash === HASH) + ' same shape ' + (shape(resolved) === shape(resolvedCallback.result)));

async function failure(name, promise, callbackCall) {
    const callback = await viaCallback(callbackCall);
    try {
        print(name + ' resolved ' + shape(await promise));
    } catch (e) {
        const details = typeof callback.result === 'object' ? shape(callback.result) : '{}';
        print(name + ' ' + e.name + ': ' + e.message + ' same message ' + (e.message === callback.error)
            + ' same details ' + (shape(Object.assign({}, e)) === details));
    }
}
await failure('getMapping', Assets.getMapping('/a.txt'), cb => Assets.getMapping('/a.txt', cb));
await failure('getMapping null', Assets.getMapping('/a.txt', null), cb => Assets.getMapping('/a.txt', cb));
await failure('setMapping', Assets.setMapping('/a.txt', HASH, undefined), cb => Assets.setMapping('/a.txt', HASH, cb));
await failure('getAsset', Assets.getAsset('/a.txt'), cb => Assets.getAsset('/a.txt', cb));
await failure('putAsset', Assets.putAsset({ data: 'hello', path: '/a.txt' }), cb => Assets.putAsset({ data: 'hello', path: '/a.txt' }, cb));
Script.stop(true);
)JS" },
    }, withGlobals(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({
        "compress returns a promise true",
        "compress true same shape true",
        "decompress hello hello hello same shape true",
        "resolve true same shape true",
        "getMapping Error: " + NO_SERVER + " same message true same details true",
        "getMapping null Error: " + NO_SERVER + " same message true same details true",
        "setMapping Error: " + NO_SERVER + " same message true same details true",
        "getAsset Error: " + NO_SERVER + " same message true same details true",
        "putAsset Error: " + NO_SERVER_UPLOAD + " same message true same details true",
    }));
}

void PromiseApiTests::testArgumentErrors() {
    // Promise form: bad arguments reject with a TypeError, as fetch does. A callback that is not a function rejects
    // the same way whatever else is wrong, and starts nothing. The callback form still throws for bad arguments.
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs", HASH_SCRIPT + R"JS(
async function rejection(name, promise) {
    try {
        await promise;
        print(name + ' resolved');
    } catch (e) {
        print(name + ' ' + e.name + ': ' + e.message);
    }
}
await rejection('getAsset url', Assets.getAsset({ url: 'not a path' }));
await rejection('getAsset responseType', Assets.getAsset({ url: '/a.txt', responseType: 'xml' }));
await rejection('getAsset options', Assets.getAsset(42));
await rejection('getMapping path', Assets.getMapping('bad'));
await rejection('putAsset data', Assets.putAsset({ data: '', path: '/a.txt' }));
await rejection('putAsset path', Assets.putAsset({ data: 'x', path: 'atp:' + HASH }));
await rejection('resolveAsset url', Assets.resolveAsset('bad'));
await rejection('getAsset callback', Assets.getAsset('/a.txt', 42));
await rejection('getAsset callback and url', Assets.getAsset('not a path', 42));
await rejection('compressData method', Assets.compressData('x', {}, 'missing'));
await rejection('decompressData callback', Assets.decompressData({ data: new ArrayBuffer(1) }, 1));
await rejection('getMapping callback', Assets.getMapping('/a.txt', 'notAFunction'));
await rejection('setMapping callback', Assets.setMapping('/a.txt', HASH, {}));
await rejection('putAsset callback', Assets.putAsset({ data: 'x' }, undefined, 7));
try {
    Assets.getMapping('bad', function() { print('never'); });
} catch (e) {
    print('callback form throws ' + e.name + ': ' + e.message);
}
Script.stop(true);
)JS" },
    }, withGlobals(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({
        "getAsset url TypeError: Invalid ATP url 'not a path'",
        "getAsset responseType TypeError: Invalid responseType: 'xml' (expected: text | arraybuffer | json)",
        "getAsset options TypeError: expected request options Object or URL as first parameter",
        "getMapping path TypeError: invalid ATP file path: bad(path:)",
        "putAsset data TypeError: expected non-zero .data (got QString / #0 bytes)",
        "putAsset path TypeError: expected valid ATP file path 'atp:" + HASH + "' ('" + HASH + "')",
        "resolveAsset url TypeError: expected options to be an asset URL or request options containing .url property",
        "getAsset callback TypeError: callback is not a function",
        "getAsset callback and url TypeError: callback is not a function",
        "compressData method TypeError: callback is not a function",
        "decompressData callback TypeError: callback is not a function",
        "getMapping callback TypeError: callback is not a function",
        "setMapping callback TypeError: callback is not a function",
        "putAsset callback TypeError: callback is not a function",
        "callback form throws Error: invalid ATP file path: bad(path:)",
    }));
}

void PromiseApiTests::testEnvironmentAfterAwait() {
    // V8 carries the module's environment across the await, also when the promise settles later from the event loop,
    // where no environment is current
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    auto run = runScript(dir, {
        { "main.mjs",
          "const ENTITY = '" + ENTITY + "', ENTITY_URL = '" + ENTITY_URL + "';\n" + R"JS(
print('body ' + Probe.sandboxURL());
await Assets.compressData('hello');
print('after compressData ' + Probe.sandboxURL());
const later = Probe.later();
Probe.completeLater('', { value: 7 });
print('after later ' + (await later).value + ' ' + Probe.sandboxURL());
await new Promise(resolve => {
    Probe.withModuleEnvironment(ENTITY, ENTITY_URL, async function() {
        await Assets.getMapping('/a.txt').catch(() => {});
        print('entity after getMapping ' + Probe.entityID());
        const entityLater = Probe.later();
        Probe.completeLater('', { value: 8 });
        print('entity after later ' + (await entityLater).value + ' ' + Probe.entityID() + ' ' + Probe.sandboxURL());
        resolve();
    });
});
print('module [' + Probe.entityID() + ']');
Script.stop(true);
)JS" },
    }, withGlobals(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QString main = urlOf(dir, "main.mjs");
    QCOMPARE(run.printed, QStringList({ "body " + main, "after compressData " + main, "after later 7 " + main,
                                        "entity after getMapping " + ENTITY,
                                        "entity after later 8 " + ENTITY + " " + ENTITY_URL, "module []" }));
}

void PromiseApiTests::testStopBeforeSettle() {
    // A promise form whose script stops before it settles: completing it later, with the engine stopped and then with
    // it gone, settles nothing and does not crash
    std::unique_ptr<PromiseApiProbe> probe;
    int pendingBefore = -1;
    int pendingAfter = -1;
    QTemporaryDir dir;
    Options options = withGlobals(probe);
    options.afterRun = [&probe, &pendingBefore, &pendingAfter](ScriptManager*) {
        pendingBefore = probe->pendingResolvers();
        probe->kept->complete(QString(), QVariantMap({ { "value", 1 } }));
        QCoreApplication::processEvents();
        pendingAfter = probe->pendingResolvers();
    };
    auto run = runScript(dir, {
        { "main.js", R"JS(
Probe.later().then(function(v) { print('settled ' + v.value); }, function(e) { print('rejected ' + e); });
print('pending ' + Probe.pendingResolvers());
Script.stop(true);
)JS" },
    }, options);
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({ "pending 1" }));
    QCOMPARE(pendingBefore, 1);
    QCOMPARE(pendingAfter, 0);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!probe->kept->engine());
    probe->kept->complete("late", QVariantMap());
    probe->kept.reset();
}

void PromiseApiTests::testUnhandledRejection() {
    // A promise form left without a handler is reported as any unhandled rejection is, with where it was made
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    Options options = withGlobals(probe);
    options.abortOnUncaughtException = false;
    auto run = runScript(dir, {
        { "main.js",
          "Assets.getMapping('/a.txt');\n"
          "Assets.getAsset({ url: 'not a path' });\n"
          "Assets.compressData('x', 42);\n"
          "Script.setTimeout(function() { Script.stop(true); }, 0);\n" },
    }, options);
    QVERIFY(!run.timedOut);
    QVERIFY(!run.uncaughtException);
    qDebug() << "Reported:" << run.errors;
    QCOMPARE(run.errors.size(), 3);
    QVERIFY(run.errors[0].startsWith("Unhandled promise rejection: Error: " + NO_SERVER));
    QVERIFY(run.errors[0].contains("main.js:1"));
    QVERIFY(run.errors[1].startsWith("Unhandled promise rejection: TypeError: Invalid ATP url 'not a path'"));
    QVERIFY(run.errors[1].contains("main.js:2"));
    QVERIFY(run.errors[2].startsWith("Unhandled promise rejection: TypeError: callback is not a function"));
    QVERIFY(run.errors[2].contains("main.js:3"));
}

void PromiseApiTests::testLoadRecording() {
    // #31: without a callback and with the clip not cached, loadRecording dereferenced a null engine. It now returns a
    // promise, pending while the clip loads. The URLs differ because the clip cache keeps a failed clip, which then
    // never signals again.
    std::unique_ptr<PromiseApiProbe> probe;
    QTemporaryDir dir;
    auto missing = [&dir](const QString& name) { return urlOf(dir, name); };
    auto run = runScript(dir, {
        { "main.mjs",
          "const URLS = ['" + missing("one.hfr") + "', '" + missing("two.hfr") + "', '" + missing("three.hfr") + "', '" +
              missing("four.hfr") + "'];\n" + R"JS(
const loading = Recording.loadRecording(URLS[0]);
let settled = false;
loading.then(() => { settled = true; }, () => { settled = true; });
await null;
print('promise ' + (loading instanceof Promise) + ' pending ' + !settled);
try {
    await loading;
    print('resolved');
} catch (e) {
    print(e.name + ': ' + e.message);
}
const callback = await new Promise(resolve => {
    const returned = Recording.loadRecording(URLS[1], function(success, url) { resolve(typeof returned + ' ' + success + ' ' + url); });
});
print('callback ' + callback);
await Recording.loadRecording(URLS[2], null).catch(e => print('null ' + e.message));
await Recording.loadRecording(URLS[3], 42).catch(e => print('42 ' + e.name + ': ' + e.message));
Script.stop(true);
)JS" },
    }, withGlobals(probe));
    QVERIFY(!run.timedOut);
    QCOMPARE(run.errors, QStringList());
    QCOMPARE(run.printed, QStringList({
        "promise true pending true",
        "Error: Failed to load recording: " + missing("one.hfr"),
        "callback undefined false " + missing("two.hfr"),
        "null Failed to load recording: " + missing("three.hfr"),
        "42 TypeError: callback is not a function",
    }));
}
