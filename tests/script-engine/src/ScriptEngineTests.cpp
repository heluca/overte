//
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include <QSignalSpy>
#include <QDebug>
#include <QFile>
#include <QTcpServer>
#include <QTemporaryFile>
#include <QTextStream>


#include "ScriptEngineTests.h"
#include "DependencyManager.h"

#include "FetchClass.h"
#include "FetchTestServer.h"
#include "ScriptEngines.h"
#include "ScriptEngine.h"
#include "ScriptCache.h"
#include "ScriptManager.h"

#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "StatTracker.h"

#include "NodeList.h"
#include "../../../libraries/entities/src/EntityScriptingInterface.h"
//#include "../../../libraries/entities/src/EntityScriptingInterface.h"

QTEST_MAIN(ScriptEngineTests)

void ScriptEngineTests::initTestCase() {
    // AudioClient starts networking, but for the purposes of the tests here we don't care,
    // so just got to use some port.
    //int listenPort = 10000;

    //DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    //DependencyManager::set<NodeList>(NodeType::Agent, listenPort);
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    // For fetch() of file: URLs; ATP stays off, as there is no asset server
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
   // DependencyManager::set<EntityScriptingInterface>(true);


}

ScriptManagerPointer ScriptEngineTests::makeManager(const QString &scriptSource, const QString &scriptFilename) {
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, scriptSource, scriptFilename);


    sm->setAbortOnUncaughtException(true);

    connect(sm.get(), &ScriptManager::scriptLoaded, [](const QString& filename){
        qWarning() << "Loaded script" << filename;
    });


    connect(sm.get(), &ScriptManager::errorLoadingScript, [](const QString& filename){
        qWarning() << "Failed to load script" << filename;
    });

    connect(sm.get(), &ScriptManager::printedMessage, [](const QString& message, const QString& engineName){
        qDebug() << "Printed message from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::infoMessage, [](const QString& message, const QString& engineName){
        qInfo() << "Info message from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::warningMessage, [](const QString& message, const QString& engineName){
        qWarning() << "Warning from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::errorMessage, [](const QString& message, const QString& engineName){
        qCritical() << "Error from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::finished, [](const QString& fileNameString, ScriptManagerPointer smp){
        qInfo() << "Finished running script" << fileNameString;
    });

    connect(sm.get(), &ScriptManager::runningStateChanged, [sm](){
        qInfo() << "Running state changed. Running = " << sm->isRunning() << "; Stopped = " << sm->isStopped() << "; Finished = " << sm->isFinished();
    });

    connect(sm.get(), &ScriptManager::unhandledException, [](std::shared_ptr<ScriptException> exception){
        qWarning() << "Exception from engine: " << exception;
    });


    return sm;
}

void ScriptEngineTests::testTrivial() {
    auto sm = makeManager("print(\"script works!\"); Script.stop(true);", "testTrivial.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    QString printed;

    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(!sm->isFinished());


    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });


    sm->run();

    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(sm->isFinished());
    QVERIFY(printed == "script works!");

}

void ScriptEngineTests::testSyntaxError() {
    auto sm = makeManager("this is not good syntax", "testSyntaxError.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    bool exceptionHappened = false;

    connect(sm.get(), &ScriptManager::unhandledException, [&exceptionHappened](std::shared_ptr<ScriptException> exception){
        exceptionHappened = true;
    });


    sm->run();

    std::shared_ptr<ScriptException> ex = sm->getUncaughtException();

    qDebug() << "Exception:" << ex;

    QVERIFY(exceptionHappened);
    QVERIFY(ex);
    QVERIFY(ex && ex->errorMessage.contains("SyntaxError"));
}


void ScriptEngineTests::testRuntimeError() {
    auto sm = makeManager("nonexisting();", "testRuntimeError.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    bool exceptionHappened = false;

    connect(sm.get(), &ScriptManager::unhandledException, [&exceptionHappened](std::shared_ptr<ScriptException> exception){
        exceptionHappened = true;
    });


    sm->run();

    std::shared_ptr<ScriptException> ex = sm->getUncaughtException();

    qDebug() << "Exception:" << ex;

    QVERIFY(exceptionHappened);
    QVERIFY(ex);
    QVERIFY(ex && ex->errorMessage.contains("ReferenceError"));

}

void ScriptEngineTests::testJSThrow() {
    auto sm = makeManager("throw(42);", "testThrow.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->run();

    std::shared_ptr<ScriptException> ex = sm->getUncaughtException();

    qDebug() << "Exception:" << ex;

    auto runtime_ex = std::dynamic_pointer_cast<ScriptRuntimeException>(ex);

    QVERIFY(ex);
    QVERIFY(runtime_ex);
    QVERIFY(runtime_ex && runtime_ex->thrownValue.toInt32() == 42);
}

void ScriptEngineTests::testRegisterClass() {
    QString printed;
    auto sm = makeManager("print(testClass.invokableFunc(4)); Script.stop(true);", "testClass.js");
    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->engine()->registerGlobalObject(scopeGuard.get(), "testClass", new TestClass());

    sm->run();

    auto ex = sm->getUncaughtException();

    QVERIFY(!ex);
    QVERIFY(printed == "14");

}

void ScriptEngineTests::testInvokeNonInvokable() {
    auto sm = makeManager("print(testClass.nonInvokableFunc(4)); Script.stop(true);", "testClass.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->engine()->registerGlobalObject(scopeGuard.get(), "testClass", new TestClass());

    sm->run();
    auto ex = sm->getUncaughtException();

    QVERIFY(ex);
    QVERIFY(ex && ex->errorMessage.contains("TypeError"));
}

void ScriptEngineTests::testRaiseException() {
    auto sm = makeManager("testClass.doRaiseTest(); Script.stop(true);", "testRaise.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->engine()->registerGlobalObject(scopeGuard.get(), "testClass", new TestClass(sm->engine()));

    sm->run();
    auto ex = sm->getUncaughtException();

    QVERIFY(ex);
    QVERIFY(ex && ex->errorMessage.contains("Exception test"));
}

void ScriptEngineTests::testRaiseExceptionAndCatch() {
    QString script =
        "try {"
        "    testClass.doRaiseTest();"
        "} catch (err) {"
        "    if (err.message.includes(\"Exception test!\")) {"
        "        print(\"Caught!\");"
        "    }"
        "}"
        "Script.stop(true);";

    QString printed;
    auto sm = makeManager(script, "testRaiseCatch.js");

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });


    auto scopeGuard = sm->engine()->getScopeGuard();
    sm->engine()->registerGlobalObject(scopeGuard.get(), "testClass", new TestClass(sm->engine()));

    sm->run();
    auto ex = sm->getUncaughtException();

    QVERIFY(!ex);
    QVERIFY(printed == "Caught!");
}


void ScriptEngineTests::testSignal() {
    QString script =
        "var count = 0;"
        "Script.update.connect(function(deltaTime) {"
        "    count++;"
        "    print(deltaTime);"
        "    if (count >= 10) {"
        "        Script.stop(true);"
        "    }"
        "});";

    QStringList printed;
    auto sm = makeManager(script, "testSignal.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    sm->run();
    QVERIFY(printed.length() >= 10);
}

void ScriptEngineTests::testSignalWithException() {
    QString script =
        "var count = 0;"
        "Script.update.connect(function(deltaTime) {"
        "    count++;"
        "    print(deltaTime);"
        "    if (count >= 3) {"
        "        Script.stop(true);"
        "    }"
        "    nonExist();"
        "});";

    QStringList printed;
    int exceptionCount = 0;

    auto sm = makeManager(script, "testSignalWithException.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    connect(sm.get(), &ScriptManager::unhandledException, [&exceptionCount](std::shared_ptr<ScriptException> exception){
        exceptionCount++;
    });


    sm->run();
    QVERIFY(printed.length() >= 3);
    QVERIFY(exceptionCount >= 3);
}

void ScriptEngineTests::testQuat() {
    QString script =
        "var x;\n"
        "print(JSON.stringify(Quat.IDENTITY));\n"
        "print(JSON.stringify(Quat.safeEulerAngles(Quat.IDENTITY)));\n"
        "print(JSON.stringify(Quat.getUp(Quat.IDENTITY)));\n"
        "print(JSON.stringify(Quat.getUp(x)));\n"
        "Script.stop(true);\n";

    int printCount = 0;
    QStringList answers{
        "{\"x\":0,\"y\":0,\"z\":0,\"w\":1}",
        "{\"x\":0,\"y\":0,\"z\":0}",
        "{\"x\":0,\"y\":1,\"z\":0}",
        ""
    };

    auto sm = makeManager(script, "testQuat.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printCount, answers](const QString& message, const QString& engineName){
        QCOMPARE(message, answers[printCount++]);
    });

    connect(sm.get(), &ScriptManager::unhandledException, [](std::shared_ptr<ScriptException> exception){
        QVERIFY(exception->errorMessage.contains("undefined to glm::quat"));
    });


    sm->run();
}

void ScriptEngineTests::testMicrotaskOrdering() {
    QString script =
        "var order = [];\n"
        "Script.setTimeout(function() {\n"
        "    order.push('timeout');\n"
        "    print(order.join(','));\n"
        "    Script.stop(true);\n"
        "}, 0);\n"
        "Promise.resolve().then(function() { order.push('then'); })\n"
        "    .then(function() { order.push('then2'); });\n"
        "(async function() { await null; order.push('await'); })();\n"
        "order.push('sync');\n";

    QString printed;
    auto sm = makeManager(script, "testMicrotaskOrdering.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(printed, QString("sync,then,await,then2,timeout"));
}

void ScriptEngineTests::testUnhandledRejection() {
    QString script =
        "async function fails(what) {\n"
        "    throw new Error(what);\n"
        "}\n"
        "fails('async boom');\n"
        "fails('caught boom').catch(function() {});\n"
        "var late = Promise.reject(new Error('late boom'));\n"
        "late.catch(function() {});\n"
        "Promise.reject(42);\n"
        "Script.setTimeout(function() { Script.stop(true); }, 0);\n";

    QStringList errors;
    auto sm = makeManager(script, "testUnhandledRejection.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        if (message.contains("promise rejection")) {
            errors.append(message);
        }
    });

    sm->run();
    qDebug() << "Reported:" << errors;
    QCOMPARE(errors.length(), 2);
    QVERIFY(errors[0].startsWith("Unhandled promise rejection: Error: async boom"));
    QVERIFY(errors[0].contains("testUnhandledRejection.js:2"));
    QVERIFY(errors[1].startsWith("Unhandled promise rejection: 42"));
    QVERIFY(errors[1].contains("testUnhandledRejection.js:8"));
}

void ScriptEngineTests::testQueueMicrotask() {
    QString script =
        "var order = [];\n"
        "setTimeout(function() {\n"
        "    order.push('timeout');\n"
        "    print(order.join(','));\n"
        "    Script.stop(true);\n"
        "});\n"
        "Promise.resolve().then(function() { order.push('then'); });\n"
        "queueMicrotask(function() {\n"
        "    order.push('microtask');\n"
        "    queueMicrotask(function() { order.push('nested'); });\n"
        "});\n"
        "queueMicrotask(function() { throw new Error('microtask boom'); });\n"
        "try { queueMicrotask(42); } catch (e) { order.push(e.name); }\n"
        "order.push('sync');\n";

    QString printed;
    QStringList errors;
    auto sm = makeManager(script, "testQueueMicrotask.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });
    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        if (message.contains("microtask boom")) {
            errors.append(message);
        }
    });

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(printed, QString("TypeError,sync,then,microtask,nested,timeout"));
    QCOMPARE(errors.length(), 1);
    QVERIFY(errors[0].contains("testQueueMicrotask.js"));
}

void ScriptEngineTests::testGlobalTimers() {
    QString script =
        "var fired = [];\n"
        "var cancelled = setTimeout(function() { fired.push('cancelled'); }, 0);\n"
        "clearTimeout(cancelled);\n"
        "clearTimeout(undefined);\n"
        "clearInterval(null);\n"
        "var crossCancelled = Script.setTimeout(function() { fired.push('crossCancelled'); }, 0);\n"
        "clearTimeout(crossCancelled);\n"
        "setTimeout(function(a, b) { fired.push('args:' + a + b); }, 1, 'x', 'y');\n"
        "var count = 0;\n"
        "var interval = setInterval(function() {\n"
        "    count++;\n"
        "    if (count === 3) {\n"
        "        clearInterval(interval);\n"
        "        Script.setTimeout(function() {\n"
        "            fired.push('interval:' + count);\n"
        "            print(typeof interval + ' ' + fired.join(','));\n"
        "            Script.stop(true);\n"
        "        }, 50);\n"
        "    }\n"
        "}, 5);\n";

    QString printed;
    auto sm = makeManager(script, "testGlobalTimers.js");
    auto scopeGuard = sm->engine()->getScopeGuard();

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(printed, QString("number args:xy,interval:3"));
}

void ScriptEngineTests::testPromiseResolve() {
    QString script =
        "var answer = { value: 42 };\n"
        "promises.make('fromScript').then(function(v) { promises.note('then:' + (v === answer) + ':' + v.value); });\n"
        "promises.resolveValue('fromScript', answer);\n"
        "promises.note('sync');\n"
        "promises.make('fromCpp').then(function(v) {\n"
        "    promises.note('then:' + v);\n"
        "    Script.stop(true);\n"
        "});\n"
        "promises.resolveLater('fromCpp', 'two');\n";

    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseResolve.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    // Settled inside script: the reaction waits for the script call to return. Settled from C++ outside any script call:
    // the reaction has already run when resolve() returns.
    QCOMPARE(promises.log, QStringList({ "sync", "then:true:42", "before", "then:two", "after" }));
}

void ScriptEngineTests::testPromiseResolveFromWorker() {
    QString script =
        "promises.make('worker').then(function(v) {\n"
        "    promises.note('then:' + v + ':' + promises.isScriptThread());\n"
        "    Script.stop(true);\n"
        "});\n"
        "promises.resolveFromWorker('worker', 'w');\n"
        "promises.note('sync');\n";

    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseResolveFromWorker.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(promises.workerCalls, 1);
    QCOMPARE(promises.log, QStringList({ "joined", "sync", "then:w:true" }));
}

void ScriptEngineTests::testPromiseReject() {
    QString script =
        "var caught = 0;\n"
        "function done() { if (++caught === 3) { Script.stop(true); } }\n"
        "promises.make('message').catch(function(e) {\n"
        "    promises.note((e instanceof Error) + ':' + e.message);\n"
        "    done();\n"
        "});\n"
        "promises.make('value').catch(function(e) {\n"
        "    promises.note((e instanceof RangeError) + ':' + e.message);\n"
        "    done();\n"
        "});\n"
        "promises.make('worker').catch(function(e) {\n"
        "    promises.note((e instanceof Error) + ':' + e.message + ':' + promises.isScriptThread());\n"
        "    done();\n"
        "});\n"
        "promises.reject('message', 'native boom');\n"
        "promises.rejectValue('value', new RangeError('range boom'));\n"
        "promises.rejectFromWorker('worker', 'worker boom');\n";

    PromiseTestClass promises;
    QStringList errors;
    auto sm = makeManager(script, "testPromiseReject.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        errors.append(message);
    });

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(errors, QStringList());
    QCOMPARE(promises.log, QStringList({ "joined", "true:native boom", "true:range boom", "true:worker boom:true" }));
}

void ScriptEngineTests::testPromiseSettleAfterStop() {
    QString script =
        "function report(v) { print('settled ' + v); }\n"
        "promises.make('onScriptThread').then(report, report);\n"
        "promises.make('onWorker').then(report, report);\n"
        "Script.stop(true);\n";

    QStringList printed;
    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseSettleAfterStop.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });

    sm->run();
    QVERIFY(sm->isFinished());

    promises.resolver("onScriptThread")->resolve(QVariant(1));
    promises.runOnWorker([resolver = promises.resolver("onWorker")] { resolver->reject(QString("late")); });
    QCoreApplication::processEvents();
    QCOMPARE(printed, QStringList());

    // An engine without a manager, destroyed while one settlement is still queued to it
    std::weak_ptr<ScriptEngine> destroyedEngine;
    ScriptPromiseResolverPointer queued, onScriptThread, onWorker;
    {
        auto engine = newScriptEngine();
        destroyedEngine = engine;
        auto engineScopeGuard = engine->getScopeGuard();
        queued = engine->newPromise().resolver;
        onScriptThread = engine->newPromise().resolver;
        onWorker = engine->newPromise().resolver;
        std::thread([queued] { queued->resolve(QVariant(3)); }).join();
    }
    QVERIFY(destroyedEngine.expired());
    onScriptThread->resolve(QVariant(4));
    std::thread([onWorker] { onWorker->reject(QString("gone")); }).join();
    QCoreApplication::processEvents();
    QCOMPARE(printed, QStringList());
}

void ScriptEngineTests::testPromiseDoubleSettle() {
    QString script =
        "function report(v) { promises.note('settled:' + (v instanceof Error ? v.message : v)); }\n"
        "promises.make('twice').then(report, report);\n"
        "promises.resolve('twice', 'first');\n"
        "promises.resolve('twice', 'second');\n"
        "promises.reject('twice', 'third');\n"
        "promises.resolveFromWorker('twice', 'fourth');\n"
        "promises.make('workerFirst').then(report, report);\n"
        "promises.resolveFromWorker('workerFirst', 'fifth');\n"
        "promises.reject('workerFirst', 'sixth');\n"
        "promises.make('dropped');\n"
        "promises.forget('dropped');\n"
        "setTimeout(function() { Script.stop(true); }, 20);\n";

    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseDoubleSettle.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(promises.log, QStringList({ "joined", "joined", "settled:first", "settled:fifth" }));
}

// Generous: a test only reaches this when a promise never settles, and then fails on the missing output
static const int FETCH_TEST_TIMEOUT_MS = 10000;

void ScriptEngineTests::runFetchScript(const QString& source, const QString& filename, QString& printed, QStringList& errors) {
    auto sm = makeManager(source, filename);
    auto scopeGuard = sm->engine()->getScopeGuard();
    // The test context is NETWORKLESS_TEST_SCRIPT, where init() registers neither XMLHttpRequest nor fetch
    registerFetchGlobals(sm->engine().get());

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });
    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        errors.append(message);
    });
    QTimer::singleShot(FETCH_TEST_TIMEOUT_MS, sm.get(), [manager = sm.get()] { manager->stop(); });

    sm->run();
    QVERIFY(!sm->getUncaughtException());
}

void ScriptEngineTests::testFetchHeaders() {
    QString script =
        "var results = [];\n"
        "var h = new Headers({ 'Content-Type': 'text/plain', 'X-B': '1' });\n"
        "h.append('x-b', '2');\n"
        "results.push(h.get('content-type'), h.get('X-b'), h.has('X-B'), h.get('missing'));\n"
        "h.set('X-B', '3');\n"
        "h.delete('Content-Type');\n"
        "results.push(h.has('content-type'), JSON.stringify(h.entries()));\n"
        "var seen = [];\n"
        "h.forEach(function(value, name) { seen.push(name + '=' + value); });\n"
        "results.push(seen.join(';'), JSON.stringify(Array.from(new Headers([['b', '2'], ['a', '1']]))));\n"
        "try { h.set('bad name', 'x'); } catch (e) { results.push(e.name); }\n"
        "var request = new Request('http://example.invalid/x', { method: 'post', headers: h, body: 'hi' });\n"
        "results.push(request.method, request.url, request.headers.get('x-b'), request.headers.get('content-type'));\n"
        "var copy = new Request(request, { method: 'PUT' });\n"
        "results.push(copy.method, copy.url, copy instanceof Request);\n"
        "try { new Request('http://example.invalid/', { body: 'x' }); } catch (e) { results.push(e.name); }\n"
        "try { new AbortSignal(); } catch (e) { results.push(e.name); }\n"
        "var response = new Response('made', { status: 201, headers: { 'X-Made': 'yes' } });\n"
        "results.push(response.status, response.ok, response.headers.get('x-made'));\n"
        "response.text().then(function(text) {\n"
        "    results.push(text, response.bodyUsed);\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchHeaders.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("text/plain,1, 2,true,,false,[[\"x-b\",\"3\"]],x-b=3,[[\"a\",\"1\"],[\"b\",\"2\"]],TypeError,"
        "POST,http://example.invalid/x,3,text/plain;charset=UTF-8,PUT,http://example.invalid/x,true,TypeError,TypeError,"
        "201,true,yes,made,true"));
}

void ScriptEngineTests::testFetchText() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "fetch(BASE + '/text').then(function(response) {\n"
        "    results.push(response.status, response.statusText, response.ok, response.url === BASE + '/text',\n"
        "        response.redirected, response.headers.get('Content-Type'), response.bodyUsed);\n"
        "    var text = response.text();\n"
        "    results.push(response.bodyUsed);\n"
        "    return text.then(function(value) { results.push(value); return response.text(); });\n"
        "}).catch(function(e) {\n"
        "    results.push(e.name);\n"
        "    return fetch(new Request(BASE + '/redirect'));\n"
        "}).then(function(response) {\n"
        "    results.push(response.redirected, response.url === BASE + '/text');\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(buffer instanceof ArrayBuffer, buffer.byteLength);\n"
        "}).catch(function(e) { results.push('unexpected ' + e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchText.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("200,OK,true,true,false,text/plain,false,true,hello fetch,TypeError,true,true,true,11"));
}

void ScriptEngineTests::testFetchJson() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/json', { headers: new Headers({ Accept: 'application/json' }) }).then(function(response) {\n"
        "    return response.json();\n"
        "}).then(function(value) {\n"
        "    print(value.answer + ',' + value.list.length);\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchJson.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("42,3"));
}

void ScriptEngineTests::testFetchPostEcho() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "fetch(BASE + '/echo', { method: 'POST', headers: { 'X-Test': 'custom value' }, body: 'posted body' })\n"
        ".then(function(response) {\n"
        "    var h = response.headers;\n"
        "    results.push(response.status, h.get('x-echo-method'), h.get('x-echo-test'), h.get('x-echo-type'),\n"
        "        h.get('x-echo-agent'));\n"
        "    return response.text();\n"
        "}).then(function(text) {\n"
        "    results.push(text);\n"
        "    return fetch(BASE + '/echo', { method: 'PUT', body: new Uint8Array([1, 2, 250]) });\n"
        "}).then(function(response) {\n"
        "    results.push(response.headers.get('x-echo-method'));\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(Array.from(new Uint8Array(buffer)).join(' '));\n"
        "    return fetch(BASE + '/echo', { method: 'DELETE' });\n"
        "}).then(function(response) {\n"
        "    results.push(response.headers.get('x-echo-method'), response.status);\n"
        "}).catch(function(e) { results.push('unexpected ' + e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchPostEcho.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("200,POST,custom value,text/plain;charset=UTF-8,Mozilla/5.0 (OverteInterface),posted body,"
        "PUT,1 2 250,DELETE,200"));
}

void ScriptEngineTests::testFetch404() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/404').then(function(response) {\n"
        "    return response.text().then(function(text) {\n"
        "        print([response.status, response.statusText, response.ok, text].join(','));\n"
        "    });\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetch404.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("404,Not Found,false,missing"));
}

void ScriptEngineTests::testFetchBadJson() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/badjson').then(function(response) {\n"
        "    return response.json().then(function(value) { print('unexpected ' + value); }, function(e) {\n"
        "        print([response.ok, e.name, e instanceof SyntaxError, response.bodyUsed].join(','));\n"
        "    });\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchBadJson.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("true,SyntaxError,true,true"));
}

void ScriptEngineTests::testFetchNetworkError() {
    quint16 refusedPort;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        refusedPort = probe.serverPort();
    }
    QString script = "var REFUSED = 'http://127.0.0.1:" + QString::number(refusedPort) + "/text';\n"
        "var results = [];\n"
        "function failure(input, init) {\n"
        "    return fetch(input, init).then(function() { results.push('resolved'); },\n"
        "        function(e) { results.push(e.name + ':' + (e instanceof TypeError)); });\n"
        "}\n"
        "failure(REFUSED)\n"
        ".then(function() { return failure('gopher://example.invalid/'); })\n"
        ".then(function() { return failure('atp:/missing.txt'); })\n"
        ".then(function() { return failure('file:///nonexistent', { method: 'POST', body: 'x' }); })\n"
        ".then(function() { return failure('http://[bad'); })\n"
        ".then(function() { print(results.join(',')); Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchNetworkError.js", printed, errors);
    QCOMPARE(errors, QStringList());
    // ATP is disabled in this test's ResourceManager, so atp: fails the same way an unknown scheme does
    QCOMPARE(printed, QString("TypeError:true,TypeError:true,TypeError:true,TypeError:true,TypeError:true"));
}

void ScriptEngineTests::testFetchAbort() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "var controller = new AbortController();\n"
        "var events = [];\n"
        "controller.signal.addEventListener('abort', function(event) { events.push(event.type); });\n"
        "var hanging = fetch(BASE + '/hang', { signal: controller.signal });\n"
        "setTimeout(function() { controller.abort(); }, 0);\n"
        "hanging.then(function() { results.push('resolved'); }, function(e) {\n"
        "    results.push(e.name, controller.signal.aborted, events.join(';'));\n"
        "    var early = new AbortController();\n"
        "    early.abort();\n"
        "    return fetch(BASE + '/text', { signal: early.signal });\n"
        "}).then(function() { results.push('resolved'); }, function(e) {\n"
        "    results.push(e.name);\n"
        "    var withReason = new AbortController();\n"
        "    var request = fetch(BASE + '/hang', { signal: withReason.signal });\n"
        "    withReason.abort('why');\n"
        "    return request;\n"
        "}).then(function() { results.push('resolved'); }, function(e) { results.push(e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchAbort.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("AbortError,true,abort,AbortError,why"));
}

void ScriptEngineTests::testFetchFile() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("file body \xC3\xA9");
    file.close();
    const QString fileUrl = QUrl::fromLocalFile(file.fileName()).toString();

    QString script = "var FILE = '" + fileUrl + "';\n"
        "var results = [];\n"
        "fetch(FILE).then(function(response) {\n"
        "    results.push(response.status, response.ok, response.url === FILE);\n"
        "    return response.text();\n"
        "}).then(function(text) {\n"
        "    results.push(text, text.length);\n"
        "    return fetch(FILE, { method: 'HEAD' });\n"
        "}).then(function(response) {\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(buffer.byteLength);\n"
        "    return fetch(FILE + '.missing');\n"
        "}).then(function() { results.push('resolved'); }, function(e) { results.push(e.name); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    runFetchScript(script, "testFetchFile.js", printed, errors);
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString::fromUtf8("200,true,true,file body \xC3\xA9,11,0,TypeError"));
}
