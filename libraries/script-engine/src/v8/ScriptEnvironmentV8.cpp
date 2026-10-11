//
//  ScriptEnvironmentV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptEnvironmentV8.h"

#include <QtCore/QThread>

#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptModuleLoaderV8.h"

ScriptEnvironmentTableV8::ScriptEnvironmentTableV8(ScriptEngineV8* engine) : _engine(engine) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::ObjectTemplate> objectTemplate = v8::ObjectTemplate::New(isolate);
    objectTemplate->SetInternalFieldCount(1);
    _template.Reset(isolate, objectTemplate);
}

ScriptEnvironmentTableV8::~ScriptEnvironmentTableV8() {
    // The object in CPED, if any, points to an entry that is about to go
    v8::Isolate* isolate = _engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    isolate->SetContinuationPreservedEmbedderData(v8::Undefined(isolate));
    _entries.clear();
    _template.Reset();
}

ScriptEnvironmentPointer ScriptEnvironmentTableV8::current() {
    v8::Isolate* isolate = _engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Value> data = isolate->GetContinuationPreservedEmbedderData();
    if (!data->IsObject()) {
        return ScriptEnvironmentPointer();
    }
    v8::Local<v8::Object> object = data.As<v8::Object>();
    if (object->InternalFieldCount() != 1) {
        return ScriptEnvironmentPointer();
    }
    // Only setCurrent() puts objects in CPED, and an entry lives as long as its object
    auto entry = static_cast<Entry*>(object->GetAlignedPointerFromInternalField(0));
    return entry && entry->table == this ? entry->environment : ScriptEnvironmentPointer();
}

void ScriptEnvironmentTableV8::setCurrent(const ScriptEnvironmentPointer& environment) {
    v8::Isolate* isolate = _engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    if (!environment) {
        isolate->SetContinuationPreservedEmbedderData(v8::Undefined(isolate));
        return;
    }
    auto& entry = _entries[environment.get()];
    if (!entry) {
        entry = std::make_unique<Entry>();
        entry->table = this;
        entry->environment = environment;
        v8::Local<v8::Object> object = _template.Get(isolate)->NewInstance(_engine->getContext()).ToLocalChecked();
        object->SetAlignedPointerInInternalField(0, entry.get());
        entry->handle.Reset(isolate, object);
        entry->handle.SetWeak(entry.get(), collected, v8::WeakCallbackType::kParameter);
    }
    isolate->SetContinuationPreservedEmbedderData(entry->handle.Get(isolate));
}

void ScriptEnvironmentTableV8::collected(const v8::WeakCallbackInfo<Entry>& info) {
    Entry* entry = info.GetParameter();
    entry->handle.Reset();
    const ScriptEnvironment* key = entry->environment.get();
    entry->table->_entries.erase(key);
}

bool ScriptEnvironmentTableV8::selfTest(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Object> marker = v8::Object::New(isolate);
    v8::Local<v8::Array> seen = v8::Array::New(isolate, 1);
    auto record = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* isolate = info.GetIsolate();
        auto stored = info.Data().As<v8::Array>()->Set(isolate->GetCurrentContext(), 0,
                                                       isolate->GetContinuationPreservedEmbedderData());
        Q_UNUSED(stored);
    };
    v8::Local<v8::Function> reaction;
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Function::New(context, record, seen).ToLocal(&reaction) || !v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return false;
    }

    // As for an await: the reaction is created under the marker and runs after the marker has been replaced
    isolate->SetContinuationPreservedEmbedderData(marker);
    auto chained = resolver->GetPromise()->Then(context, reaction);
    isolate->SetContinuationPreservedEmbedderData(v8::Undefined(isolate));
    auto resolved = resolver->Resolve(context, v8::Undefined(isolate));
    Q_UNUSED(chained);
    Q_UNUSED(resolved);
    isolate->PerformMicrotaskCheckpoint();
    isolate->SetContinuationPreservedEmbedderData(v8::Undefined(isolate));

    v8::Local<v8::Value> value;
    return seen->Get(context, 0).ToLocal(&value) && value->StrictEquals(marker);
}

void ScriptEngineV8::initEnvironments(v8::Local<v8::Context> context) {
    _environments = std::make_unique<ScriptEnvironmentTableV8>(this);
    _environmentCarriedAcrossAwait = ScriptEnvironmentTableV8::selfTest(_v8Isolate, context);
    if (!_environmentCarriedAcrossAwait) {
        // R1 of the design in #10: module code would lose its entity, sandbox and permission environment at every await
        qCCritical(scriptengine_v8) << "This V8 build does not preserve continuation embedder data across await;"
                                    << "ES module code will run as classic script code after an await";
        Q_ASSERT(_environmentCarriedAcrossAwait);
    }
}

void ScriptEngineV8::refuseModuleLoad(const QUrl& url, ScriptModuleCallback onEvaluated) {
    // Reported from the event loop, as a load failure always is
    QMetaObject::invokeMethod(this, [this, url, onEvaluated = std::move(onEvaluated)] {
        QString message = ScriptModuleLoaderV8::MODULES_UNAVAILABLE;
        auto exception = std::make_shared<ScriptEngineException>(message, "module loading", -1);
        if (_manager) {
            _manager->scriptErrorMessage(message, url.toString(), -1);
        } else {
            qCWarning(scriptengine_v8) << message;
        }
        setUncaughtException(exception);
        if (onEvaluated) {
            onEvaluated(undefinedValue(), exception);
        }
    }, Qt::QueuedConnection);
}

ScriptEnvironmentPointer ScriptEngineV8::currentEnvironment() {
    // Other threads (log messages, for one) get none: the script thread holds the isolate's lock while it runs
    if (!_environments || QThread::currentThread() != thread()) {
        return ScriptEnvironmentPointer();
    }
    auto scopeGuard = getScopeGuard();
    return _environments->current();
}

ScriptEnvironmentPointer ScriptEngineV8::swapCurrentEnvironment(const ScriptEnvironmentPointer& environment) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!_environments || QThread::currentThread() != thread()) {
        return ScriptEnvironmentPointer();
    }
    auto scopeGuard = getScopeGuard();
    ScriptEnvironmentPointer previous = _environments->current();
    _environments->setCurrent(environment);
    return previous;
}
