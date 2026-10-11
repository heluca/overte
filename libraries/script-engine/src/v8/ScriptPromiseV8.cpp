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
    // Used unlocked: this runs on the engine's thread, which is also where the engine is deleted (ScriptManager's
    // deleteLater deleter), and the isolate is never disposed.
    for (const auto& settlement : settlements) {
        apply(engine, settlement);
    }
}

void ScriptPromiseBridgeV8::apply(ScriptEngineV8* engine, const Settlement& settlement) {
    // Always on the script thread, but not always inside ScriptManager::run(), which otherwise holds the isolate: entity
    // script engines and stopped managers settle outside it. Callers on other threads never get here, their settlements
    // are queued, which is why only the QVariant and QString overloads are safe for a caller without the isolate.
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

ScriptValue ScriptEngineV8::callReportingRejection(const ScriptValue& function, const ScriptValue& thisObject,
                                                   const ScriptValueList& args, RejectionHandler onRejected) {
    Q_ASSERT(QThread::currentThread() == thread());
    // One scope around the call and the handler: the call's own checkpoint would otherwise report a promise rejected
    // before its first await as unhandled before the handler is attached
    MicrotaskCheckpointScopeV8 microtaskCheckpointScope(this);
    ScriptValue result = function.call(thisObject, args);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Local<v8::Context> context = getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Value> value = ScriptValueV8Wrapper::fullUnwrap(this, result).get();
    // A promise the script already handles, as in p.catch(log); return p;, is its own business
    if (!value->IsPromise() || value.As<v8::Promise>()->HasHandler()) {
        return result;
    }
    quint64 id = ++_lastRejectionHandler;
    v8::Local<v8::Value> data = v8::Number::New(_v8Isolate, static_cast<double>(id));
    v8::Local<v8::Function> fulfilled;
    v8::Local<v8::Function> rejected;
    if (v8::Function::New(context, rejectionHandlerFulfilled, data).ToLocal(&fulfilled)
        && v8::Function::New(context, rejectionHandlerRejected, data).ToLocal(&rejected)) {
        _rejectionHandlers.emplace(id, std::move(onRejected));
        auto reactions = value.As<v8::Promise>()->Then(context, fulfilled, rejected);
        Q_UNUSED(reactions);
    }
    return result;
}

void ScriptEngineV8::rejectionHandlerFulfilled(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto engine = static_cast<ScriptEngineV8*>(info.GetIsolate()->GetData(ENGINE_ISOLATE_DATA_SLOT));
    engine->_rejectionHandlers.erase(static_cast<quint64>(info.Data().As<v8::Number>()->Value()));
}

void ScriptEngineV8::rejectionHandlerRejected(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto engine = static_cast<ScriptEngineV8*>(isolate->GetData(ENGINE_ISOLATE_DATA_SLOT));
    auto found = engine->_rejectionHandlers.find(static_cast<quint64>(info.Data().As<v8::Number>()->Value()));
    if (found == engine->_rejectionHandlers.end()) {
        return;
    }
    RejectionHandler handler = std::move(found->second);
    engine->_rejectionHandlers.erase(found);

    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    // The reason's own conversions may run script that throws
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::Value> reason = info[0];
    QString text;
    QString fileName;
    int lineNumber = -1;
    v8::Local<v8::Value> stack;
    if (reason->IsNativeError()
        && reason.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "stack")).ToLocal(&stack)
        && stack->IsString()) {
        text = QString(*v8::String::Utf8Value(isolate, stack));
        // Only an Error says where it was thrown; for anything else this would be the reaction's own location
        v8::Local<v8::Message> location = v8::Exception::CreateMessage(isolate, reason);
        if (!location.IsEmpty() && location->GetScriptResourceName()->IsString()) {
            fileName = QString(*v8::String::Utf8Value(isolate, location->GetScriptResourceName()));
            lineNumber = location->GetLineNumber(context).FromMaybe(-1);
        }
    } else {
        v8::Local<v8::String> detail;
        if (reason->ToDetailString(context).ToLocal(&detail)) {
            text = QString(*v8::String::Utf8Value(isolate, detail));
        }
    }
    handler(text, fileName, lineNumber);
}

ScriptValue ScriptEngineV8::constructCatching(const ScriptValue& constructor, std::shared_ptr<ScriptException>* thrown,
                                              QString* thrownFileName) {
    Q_ASSERT(QThread::currentThread() == thread());
    MicrotaskCheckpointScopeV8 microtaskCheckpointScope(this);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Local<v8::Context> context = getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Value> function = ScriptValueV8Wrapper::fullUnwrap(this, constructor).get();
    v8::TryCatch tryCatch(_v8Isolate);
    v8::Local<v8::Object> object;
    if (function->IsFunction() && function.As<v8::Function>()->NewInstance(context, 0, nullptr).ToLocal(&object)) {
        return ScriptValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, object)));
    }

    auto exception = std::make_shared<ScriptRuntimeException>();
    exception->additionalInfo = "constructor";
    if (tryCatch.HasTerminated()) {
        exception->errorMessage = "Constructor was terminated";
    } else if (!tryCatch.HasCaught()) {
        exception->errorMessage = "Not a constructor";
    } else {
        exception->errorMessage = QString(*v8::String::Utf8Value(_v8Isolate, tryCatch.Exception()));
        exception->thrownValue = ScriptValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, tryCatch.Exception())));
        v8::Local<v8::Message> message = tryCatch.Message();
        if (!message.IsEmpty()) {
            exception->errorLine = message->GetLineNumber(context).FromMaybe(-1);
            exception->errorColumn = message->GetStartColumn(context).FromMaybe(0);
            if (message->GetScriptResourceName()->IsString()) {
                *thrownFileName = QString(*v8::String::Utf8Value(_v8Isolate, message->GetScriptResourceName()));
            }
        }
        v8::Local<v8::Value> stack;
        if (tryCatch.StackTrace(context).ToLocal(&stack) && stack->IsString()) {
            exception->backtrace = QString(*v8::String::Utf8Value(_v8Isolate, stack)).split("\n");
        }
    }
    *thrown = exception;
    return undefinedValue();
}
