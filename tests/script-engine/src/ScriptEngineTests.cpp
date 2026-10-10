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
#include <QTextStream>


#include "ScriptEngineTests.h"
#include "DependencyManager.h"

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
   // DependencyManager::set<ResourceManager>();
   // DependencyManager::set<ResourceRequestObserver>();
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

    // An engine without a manager, destroyed with one settlement still queued to it and another one submitted while
    // it is being destroyed: both reach the closed bridge, and neither may settle.
    PromiseTestClass standalone;
    std::weak_ptr<ScriptEngine> destroyedEngine;
    ScriptPromiseResolverPointer afterDestroy, afterDestroyOnWorker;
    {
        auto engine = newScriptEngine();
        destroyedEngine = engine;
        auto engineScopeGuard = engine->getScopeGuard();
        standalone.setEngine(engine.get());
        engine->registerGlobalObject(engineScopeGuard.get(), "standalone", &standalone);
        engine->evaluate(
            "function report(v) { standalone.note('settled:' + v); }\n"
            "['queued', 'duringDestroy', 'afterDestroy', 'afterDestroyOnWorker'].forEach(function(name) {\n"
            "    standalone.make(name).then(report, report);\n"
            "});\n");
        QVERIFY(!engine->hasUncaughtException());
        standalone.resolveFromWorker("queued", "q");
        afterDestroy = standalone.resolver("afterDestroy");
        afterDestroyOnWorker = standalone.resolver("afterDestroyOnWorker");
        // Delivered by the event processing in ~ScriptEngineV8, after it closed the bridge but before releasing it
        QMetaObject::invokeMethod(&standalone, [&standalone, &destroyedEngine] {
            standalone.note(QString("duringDestroy:") + (destroyedEngine.expired() ? "destroying" : "alive"));
            standalone.resolver("duringDestroy")->resolve(QVariant(5));
        }, Qt::QueuedConnection);
    }
    QVERIFY(destroyedEngine.expired());
    QCOMPARE(standalone.log, QStringList({ "joined", "duringDestroy:destroying" }));

    // Once the bridge itself is gone there is nothing left to observe: these only must not crash
    afterDestroy->resolve(QVariant(6));
    std::thread([afterDestroyOnWorker] { afterDestroyOnWorker->reject(QString("gone")); }).join();
    QCoreApplication::processEvents();
}

void ScriptEngineTests::testPromiseDoubleSettle() {
    QString script =
        "var settled = 0;\n"
        "function report(v) {\n"
        "    promises.note('settled:' + (v instanceof Error ? v.message : v));\n"
        "    if (++settled === 2) { Script.stop(true); }\n"
        "}\n"
        "promises.make('twice').then(report, report);\n"
        "promises.resolve('twice', 'first');\n"
        "promises.resolve('twice', 'second');\n"
        "promises.reject('twice', 'third');\n"
        "promises.resolveFromWorker('twice', 'fourth');\n"
        "promises.make('workerFirst').then(report, report);\n"
        "promises.resolveFromWorker('workerFirst', 'fifth');\n"
        "promises.reject('workerFirst', 'sixth');\n"
        "promises.make('dropped');\n"
        "promises.forget('dropped');\n";

    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseDoubleSettle.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(promises.log, QStringList({ "joined", "joined", "settled:first", "settled:fifth" }));
    QCOMPARE(promises.pendingResolvers(), 0);
}

void ScriptEngineTests::testPromiseForget() {
    QString script =
        "promises.make('kept');\n"
        "promises.make('dropped');\n"
        "promises.make('workerDropped');\n"
        "promises.make('marker').then(function() {\n"
        "    promises.note('drained:' + promises.pendingResolvers());\n"
        "    Script.stop(true);\n"
        "});\n"
        "promises.note('made:' + promises.pendingResolvers());\n"
        "promises.forget('dropped');\n"
        "promises.note('forgot:' + promises.pendingResolvers());\n"
        "promises.forgetFromWorker('workerDropped');\n"
        "promises.resolveFromWorker('marker', 'm');\n"
        "promises.note('queued:' + promises.pendingResolvers());\n";

    PromiseTestClass promises;
    auto sm = makeManager(script, "testPromiseForget.js");
    auto scopeGuard = sm->engine()->getScopeGuard();
    promises.setEngine(sm->engine().get());
    sm->engine()->registerGlobalObject(scopeGuard.get(), "promises", &promises);

    sm->run();
    QVERIFY(!sm->getUncaughtException());
    QCOMPARE(promises.workerCalls, 2);
    // Dropped on the script thread the resolver is released at once; dropped on a worker the release is queued, and
    // applied before the settlement queued after it.
    QCOMPARE(promises.log, QStringList({ "made:4", "forgot:3", "joined", "joined", "queued:3", "drained:1" }));
}
