//
//  ScriptModuleSyntheticV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// CommonJS (.js) and JSON (.json) files imported by ES modules: synthetic modules with a single default export. Their
// source is fetched with the rest of the graph; the default export is set when the graph evaluates them, in order.

#include "ScriptModuleLoaderV8.h"

#include <utility>

#include "../ScriptManager.h"
#include "ScriptContextV8Wrapper.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

using State = ScriptModuleRecordV8::State;

void ScriptModuleLoaderV8::compileSynthetic(ScriptEngineV8* engine, const ScriptModuleRecordV8Pointer& record,
                                            const QString& source) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine->_contexts.first()->toV8Value();
    v8::Context::Scope contextScope(context);
    QString url = record->url.toString();

    if (record->kind == ScriptModuleKind::Json) {
        // Parsed now rather than when evaluated: like a syntax error, invalid JSON fails the graph before anything runs
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Value> value;
        if (!v8::JSON::Parse(context, toV8String(isolate, source)).ToLocal(&value)) {
            record->state = State::Failed;
            record->errorFileName = url;
            record->error = QString("Error while parsing JSON module %1: %2").arg(url, toQString(isolate, tryCatch.Exception()));
            return;
        }
        record->jsonValue.Reset(isolate, value);
    } else {
        record->commonJsSource = source;
    }

    const v8::Local<v8::String> exportNames[] = { v8::String::NewFromUtf8Literal(isolate, "default") };
    v8::Local<v8::Module> module = v8::Module::CreateSyntheticModule(isolate, toV8String(isolate, url), exportNames,
                                                                     evaluateSynthetic);
    engine->_modules->setCompiled(isolate, record, module);
}

v8::MaybeLocal<v8::Value> ScriptModuleLoaderV8::evaluateSynthetic(v8::Local<v8::Context> context,
                                                                  v8::Local<v8::Module> module) {
    v8::Isolate* isolate = context->GetIsolate();
    v8::EscapableHandleScope handleScope(isolate);
    auto engine = static_cast<ScriptEngineV8*>(isolate->GetData(ENGINE_ISOLATE_DATA_SLOT));
    auto record = engine->_modules->find(isolate, module);
    if (!record) {
        isolate->ThrowError("Synthetic module is not in the module map");
        return v8::MaybeLocal<v8::Value>();
    }

    v8::Local<v8::Value> value;
    if (record->kind == ScriptModuleKind::Json) {
        value = record->jsonValue.Get(isolate);
        record->jsonValue.Reset();
    } else if (!evaluateCommonJs(engine, *record).ToLocal(&value)) {
        return v8::MaybeLocal<v8::Value>();
    }
    if (module->SetSyntheticModuleExport(isolate, v8::String::NewFromUtf8Literal(isolate, "default"), value).IsNothing()) {
        return v8::MaybeLocal<v8::Value>();
    }

    // Success is a resolved promise
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)
        || resolver->Resolve(context, v8::Undefined(isolate)).IsNothing()) {
        return v8::MaybeLocal<v8::Value>();
    }
    return handleScope.Escape(resolver->GetPromise());
}

// Evaluated as Script.require evaluates a file it fetched: through ScriptManager::instantiateModule, in a closure of its
// own, and into Script.require.cache under the resolved URL. So Script.require of the same URL and the import return
// the same exports whichever comes first, and require() inside the file works as it always has. The environment is
// the importing graph's (current while the graph evaluates), so currentSandboxURL() is its sandbox.
v8::MaybeLocal<v8::Value> ScriptModuleLoaderV8::evaluateCommonJs(ScriptEngineV8* engine, ScriptModuleRecordV8& record) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::EscapableHandleScope handleScope(isolate);
    QString modulePath = record.url.toString();
    ScriptManager* manager = engine->manager();
    if (!manager) {
        isolate->ThrowError(toV8String(isolate, "Cannot evaluate CommonJS module " + modulePath + " without a script manager"));
        return v8::MaybeLocal<v8::Value>();
    }

    ScriptValue jsRequire = engine->globalObject().property("Script").property("require");
    ScriptValue cache = jsRequire.property("cache");
    // Slice 5 (#11): the epoch reload must also clear this entry, or the reloaded graph gets the old exports
    ScriptValue module = cache.property(modulePath);
    // The test Script.require makes for a cached module
    if (!module.property("exports").isObject()) {
        module = manager->newModule(modulePath);
        // Before evaluation, as in Script.require, so that a cyclic require() finds it
        cache.setProperty(modulePath, module);
        module.setProperty("content-type", "application/javascript");

        ScriptClosureFailureV8 failure;
        engine->_closureFailure = &failure;
        manager->instantiateModule(module, std::exchange(record.commonJsSource, QString()));
        engine->_closureFailure = nullptr;
        if (failure.failed) {
            cache.setProperty(modulePath, engine->nullValue());
            QString site = siteText(modulePath, failure.line);
            v8::Local<v8::Value> exception = failure.exception.IsEmpty()
                ? v8::Exception::SyntaxError(toV8String(isolate, QString("Error while compiling CommonJS module %1: %2")
                                                                     .arg(site, failure.syntaxError)))
                : failure.exception.Get(isolate);
            if (!exception->IsNativeError()) {
                // Only an Error carries a stack naming the file and line
                exception = v8::Exception::Error(toV8String(isolate, QString("CommonJS module %1 threw %2")
                                                                         .arg(site, toQString(isolate, exception))));
            }
            isolate->ThrowException(exception);
            return v8::MaybeLocal<v8::Value>();
        }
        module.setProperty("loaded", true, ScriptValue::ReadOnly | ScriptValue::Undeletable);
        // Script.require's reference point for telling a deleted cache key from one never set
        jsRequire.data().setProperty(modulePath, module);
    }
    return handleScope.Escape(ScriptValueV8Wrapper::fullUnwrap(engine, module.property("exports")).get());
}
