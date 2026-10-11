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

// Only an Error carries a stack naming where it was thrown. For anything else the loader's report takes the file and
// line from the record (findCommonJsErrorSite), and the message names them for import(), which rejects with it.
static void throwCommonJsFailure(ScriptEngineV8* engine, ScriptModuleRecordV8& record, const ScriptClosureFailureV8& failure) {
    v8::Isolate* isolate = engine->getIsolate();
    QString fileName = failure.fileName.isEmpty() ? record.url.toString() : failure.fileName;
    QString site = ScriptModuleLoaderV8::siteText(fileName, failure.line);
    if (!failure.exception.IsEmpty() && failure.exception.Get(isolate)->IsNativeError()) {
        isolate->ThrowException(failure.exception.Get(isolate));
        return;
    }
    QString text;
    if (!failure.exception.IsEmpty()) {
        text = QString("CommonJS module %1 threw %2")
            .arg(site, ScriptModuleLoaderV8::toQString(isolate, failure.exception.Get(isolate)));
    } else if (!failure.syntaxError.isEmpty()) {
        text = QString("Error while compiling CommonJS module %1: %2").arg(site, failure.syntaxError);
    } else {
        text = QString("CommonJS module %1 could not be evaluated").arg(record.url.toString());
    }
    v8::Local<v8::String> message = ScriptModuleLoaderV8::toV8String(isolate, text);
    v8::Local<v8::Value> exception = failure.exception.IsEmpty() && !failure.syntaxError.isEmpty()
        ? v8::Exception::SyntaxError(message) : v8::Exception::Error(message);
    record.commonJsError.Reset(isolate, exception);
    record.errorFileName = fileName;
    record.errorLine = failure.line;
    isolate->ThrowException(exception);
}

bool ScriptModuleLoaderV8::findCommonJsErrorSite(ScriptEngineV8* engine, v8::Local<v8::Value> exception,
                                                 QString* fileName, int* line) {
    v8::Isolate* isolate = engine->getIsolate();
    for (const auto& record : engine->_modules->records()) {
        if (!record->commonJsError.IsEmpty() && record->commonJsError.Get(isolate)->StrictEquals(exception)) {
            *fileName = record->errorFileName;
            *line = record->errorLine;
            return true;
        }
    }
    return false;
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
    ScriptValue module = cache.property(modulePath);
    // The test Script.require makes for a cached module. A reload's epoch evaluates the file it fetched anew instead,
    // replacing the entry for every later Script.require too; so does a graph fetched anew for a changed entry URL
    // when the entry was made by another epoch's import, not by Script.require.
    auto isOtherEpochsImport = [&] {
        v8::Local<v8::Value> value = ScriptValueV8Wrapper::fullUnwrap(engine, module).get();
        for (const auto& other : engine->_modules->records()) {
            if (other->epoch != record.epoch && !other->commonJsModule.IsEmpty()
                && other->commonJsModule.Get(isolate)->StrictEquals(value)) {
                return true;
            }
        }
        return false;
    };
    bool reevaluate = record.forceDownload
        && (engine->_reloadModuleEpochs.contains(record.epoch) || isOtherEpochsImport());
    if (reevaluate || !module.property("exports").isObject()) {
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
            throwCommonJsFailure(engine, record, failure);
            return v8::MaybeLocal<v8::Value>();
        }
        record.commonJsModule.Reset(isolate, ScriptValueV8Wrapper::fullUnwrap(engine, module).get());
        module.setProperty("loaded", true, ScriptValue::ReadOnly | ScriptValue::Undeletable);
        // Script.require's reference point for telling a deleted cache key from one never set
        jsRequire.data().setProperty(modulePath, module);
    }
    return handleScope.Escape(ScriptValueV8Wrapper::fullUnwrap(engine, module.property("exports")).get());
}
