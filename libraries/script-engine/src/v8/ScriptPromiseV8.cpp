//
//  ScriptPromiseV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptPromiseV8.h"

#include <QtCore/QThread>

#include "../ScriptManager.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

ScriptPromise ScriptEngineV8::newPromise() {
    return _promiseBridge->newPromise();
}

ScriptPromise ScriptPromiseBridgeV8::newPromise() {
    // _engine only changes in close(), which runs as the engine is destroyed; no promise can be made after that.
    ScriptEngineV8* engine = _engine;
    Q_ASSERT(engine && QThread::currentThread() == engine->thread());
    v8::Isolate* isolate = engine->getIsolate();
    Q_ASSERT(isolate->IsCurrent());
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine->getContext();
    v8::Context::Scope contextScope(context);

    v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
    quint64 id = _nextId++;
    _resolvers.emplace(id, v8::Global<v8::Promise::Resolver>(isolate, resolver));

    V8ScriptValue promise(engine, resolver->GetPromise());
    return { ScriptValue(new ScriptValueV8Wrapper(engine, std::move(promise))),
             std::make_shared<ScriptPromiseResolverV8>(weak_from_this(), id) };
}

void ScriptPromiseBridgeV8::submit(Settlement settlement) {
    QMutexLocker locker(&_mutex);
    if (!_engine) {
        return;
    }
    if (QThread::currentThread() == _engine->thread()) {
        ScriptEngineV8* engine = _engine;
        // Not held while applying: the reactions run script, which may settle or make other promises.
        locker.unlock();
        apply(engine, settlement);
        return;
    }
    bool drainQueued = !_queued.empty();
    _queued.push_back(std::move(settlement));
    if (!drainQueued) {
        // Posting under the lock keeps the engine alive for the call, as close() must take the lock first. The event
        // carries no settlement, so it is harmless if the engine is gone before it is delivered.
        QMetaObject::invokeMethod(_engine, [bridge = weak_from_this()] {
            if (auto strongBridge = bridge.lock()) {
                strongBridge->drain();
            }
        }, Qt::QueuedConnection);
    }
}

void ScriptPromiseBridgeV8::drain() {
    ScriptEngineV8* engine;
    std::vector<Settlement> settlements;
    {
        QMutexLocker locker(&_mutex);
        engine = _engine;
        std::swap(settlements, _queued);
    }
    if (!engine) {
        return;
    }
    for (const auto& settlement : settlements) {
        apply(engine, settlement);
    }
}

void ScriptPromiseBridgeV8::apply(ScriptEngineV8* engine, const Settlement& settlement) {
    // Entity script engines and stopped managers settle outside ScriptManager::run(), which otherwise holds the isolate.
    auto scopeGuard = engine->getScopeGuard();
    auto found = _resolvers.find(settlement.id);
    if (found == _resolvers.end()) {
        return;
    }
    v8::Global<v8::Promise::Resolver> resolver = std::move(found->second);
    _resolvers.erase(found);
    if (settlement.outcome == Outcome::Forget || (engine->manager() && engine->manager()->isFinished())) {
        return;
    }

    v8::Isolate* isolate = engine->getIsolate();
    // Outermost when settling from C++, so the reactions run before this returns; inside a script call they run when
    // that call returns, as for a promise settled by script.
    MicrotaskCheckpointScopeV8 microtaskCheckpointScope(engine);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine->getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Value> value = settlement.makeValue(engine);
    v8::Local<v8::Promise::Resolver> localResolver = resolver.Get(isolate);
    auto result = settlement.outcome == Outcome::Fulfil ? localResolver->Resolve(context, value)
                                                        : localResolver->Reject(context, value);
    Q_UNUSED(result);
}

void ScriptPromiseBridgeV8::close() {
    std::vector<Settlement> dropped;
    {
        QMutexLocker locker(&_mutex);
        _engine = nullptr;
        std::swap(dropped, _queued);
    }
    _resolvers.clear();
}

ScriptPromiseResolverV8::~ScriptPromiseResolverV8() {
    if (!_settled) {
        // Releases the V8 resolver, which may only be done on the script thread
        settle(ScriptPromiseBridgeV8::Outcome::Forget, nullptr);
    }
}

void ScriptPromiseResolverV8::settle(ScriptPromiseBridgeV8::Outcome outcome, ScriptPromiseBridgeV8::MakeValue makeValue) {
    if (_settled.exchange(true)) {
        return;
    }
    if (auto bridge = _bridge.lock()) {
        bridge->submit({ _id, outcome, std::move(makeValue) });
    }
}

void ScriptPromiseResolverV8::resolve(const ScriptValue& value) {
    settle(ScriptPromiseBridgeV8::Outcome::Fulfil, [value](ScriptEngineV8* engine) {
        return ScriptValueV8Wrapper::fullUnwrap(engine, value).get();
    });
}

void ScriptPromiseResolverV8::resolve(const QVariant& value) {
    settle(ScriptPromiseBridgeV8::Outcome::Fulfil, [value](ScriptEngineV8* engine) {
        return engine->castVariantToValue(value).get();
    });
}

void ScriptPromiseResolverV8::reject(const ScriptValue& reason) {
    settle(ScriptPromiseBridgeV8::Outcome::Reject, [reason](ScriptEngineV8* engine) {
        return ScriptValueV8Wrapper::fullUnwrap(engine, reason).get();
    });
}

void ScriptPromiseResolverV8::reject(const QString& message) {
    settle(ScriptPromiseBridgeV8::Outcome::Reject, [message](ScriptEngineV8* engine) {
        v8::Isolate* isolate = engine->getIsolate();
        return v8::Exception::Error(v8::String::NewFromUtf8(isolate, message.toStdString().c_str()).ToLocalChecked());
    });
}
