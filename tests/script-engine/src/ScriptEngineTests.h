//
//  SciptEngineTests.h
//  tests/script-engine/src
//
//  Created by Dale Glass
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptingEngineTests_h
#define overte_ScriptingEngineTests_h

#include <functional>
#include <thread>

#include <QtTest/QtTest>
#include "ScriptManager.h"
#include "ScriptEngine.h"

using ScriptManagerPointer = std::shared_ptr<ScriptManager>;


class TestClass : public QObject {
    Q_OBJECT

    public:
        TestClass() {};

        TestClass(ScriptEnginePointer ptr) : _engine(ptr) {};

        Q_INVOKABLE int invokableFunc(int val) {
            qDebug() << "invokableFunc called with value" << val;
            return val + 10;
        }

        Q_INVOKABLE void doRaiseTest() {
            qDebug() << "About to raise an exception";
            _engine->raiseException("Exception test!");
        }


        int nonInvokableFunc(int val) {
            qCritical() << "nonInvokableFunc called with value" << val;
            return val + 20;
        }

    private:
        ScriptEnginePointer _engine;

};


// Hands script code native promises made by ScriptEngine::newPromise(), and settles them from C++ on request
class PromiseTestClass : public QObject {
    Q_OBJECT

    public:
        void setEngine(ScriptEngine* engine) { _engine = engine; }
        ScriptPromiseResolverPointer resolver(const QString& name) { return _resolvers.value(name); }

        Q_INVOKABLE ScriptValue make(const QString& name) {
            ScriptPromise promise = _engine->newPromise();
            _resolvers.insert(name, promise.resolver);
            return promise.promise;
        }
        Q_INVOKABLE void forget(const QString& name) { _resolvers.remove(name); }

        Q_INVOKABLE void resolve(const QString& name, const QVariant& value) { _resolvers.value(name)->resolve(value); }
        Q_INVOKABLE void resolveValue(const QString& name, const ScriptValue& value) { _resolvers.value(name)->resolve(value); }
        Q_INVOKABLE void reject(const QString& name, const QString& message) { _resolvers.value(name)->reject(message); }
        Q_INVOKABLE void rejectValue(const QString& name, const ScriptValue& reason) { _resolvers.value(name)->reject(reason); }

        // Resolves from a timer, outside any script call, noting what ran during the resolve call
        Q_INVOKABLE void resolveLater(const QString& name, const QVariant& value) {
            QTimer::singleShot(0, this, [this, name, value] {
                note("before");
                _resolvers.value(name)->resolve(value);
                note("after");
            });
        }

        // Settles from a thread of its own and waits for it, so the settlement is queued to the script thread
        Q_INVOKABLE void resolveFromWorker(const QString& name, const QString& value) {
            runOnWorker([resolver = _resolvers.value(name), value] { resolver->resolve(QVariant(value)); });
        }
        Q_INVOKABLE void rejectFromWorker(const QString& name, const QString& message) {
            runOnWorker([resolver = _resolvers.value(name), message] { resolver->reject(message); });
        }
        void runOnWorker(std::function<void()> function) {
            QThread* scriptThread = _engine->thread();
            std::thread worker([this, function, scriptThread] {
                if (QThread::currentThread() != scriptThread) {
                    workerCalls++;
                }
                function();
            });
            worker.join();
            note("joined");
        }

        Q_INVOKABLE bool isScriptThread() const { return QThread::currentThread() == _engine->thread(); }
        Q_INVOKABLE void note(const QString& entry) { log.append(entry); }

        QStringList log;
        int workerCalls { 0 };

    private:
        ScriptEngine* _engine { nullptr };
        QHash<QString, ScriptPromiseResolverPointer> _resolvers;
};


class ScriptEngineTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testTrivial();
    void testSyntaxError();
    void testRuntimeError();
    void testJSThrow();
    void testRegisterClass();
    void testInvokeNonInvokable();
    void testRaiseException();
    void testRaiseExceptionAndCatch();
    void testSignal();
    void testSignalWithException();
    void testQuat();
    void testMicrotaskOrdering();
    void testUnhandledRejection();
    void testQueueMicrotask();
    void testGlobalTimers();
    void testPromiseResolve();
    void testPromiseResolveFromWorker();
    void testPromiseReject();
    void testPromiseSettleAfterStop();
    void testPromiseDoubleSettle();
    void testFetchHeaders();
    void testFetchText();
    void testFetchJson();
    void testFetchPostEcho();
    void testFetch404();
    void testFetchBadJson();
    void testFetchNetworkError();
    void testFetchAbort();
    void testFetchFile();


private:
    ScriptManagerPointer makeManager(const QString &source, const QString &filename);
    void runFetchScript(const QString& source, const QString& filename, QString& printed, QStringList& errors);

};

#endif // overte_ScriptingEngineTests_h
