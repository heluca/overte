//
//  ScriptModuleEpochV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// Module map epochs: making one, and releasing one with its records and its loads in flight once no entity script uses
// it; the stalled top-level await report; and how a loader ends, by finishing or by being abandoned.

#include "ScriptModuleLoaderV8.h"

#include <QtCore/QStringList>
#include <QtCore/QThread>

#include "../ScriptManager.h"
#include "ScriptContextV8Wrapper.h"
#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

int ScriptEngineV8::newModuleEpoch() {
    Q_ASSERT(QThread::currentThread() == thread());
    return ++_lastModuleEpoch;
}

void ScriptEngineV8::applyModuleEpochDownload(ScriptModuleRequest& request) {
    // Epoch 0 is shared by everything that is not an entity script, which a reload of the top-level script must not
    // force for good
    if (request.forceDownload && request.epoch != 0) {
        _downloadModuleEpochs.insert(request.epoch);
    } else if (_downloadModuleEpochs.contains(request.epoch)) {
        request.forceDownload = true;
    }
}

void ScriptEngineV8::releaseModuleEpoch(int epoch) {
    Q_ASSERT(QThread::currentThread() == thread());
    reportStalledTopLevelAwaits(epoch, "Entity script unloaded while a top-level await was still pending at ");
    _downloadModuleEpochs.remove(epoch);
    ScriptModuleLoaderV8::releaseEpoch(this, epoch);
}

// Only the entry the record's own evaluation made: a later Script.require, or a reload's epoch, may have replaced it
static void removeRequireCacheEntry(ScriptEngineV8* engine, const QString& url, v8::Local<v8::Value> module) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ScriptValue jsRequire = engine->globalObject().property("Script").property("require");
    if (!jsRequire.isObject()) {
        return;
    }
    v8::Local<v8::String> key = ScriptModuleLoaderV8::toV8String(isolate, url);
    // The cache, and Script.require's record of what it cached there (see ScriptManager::require)
    for (const ScriptValue& holder : { jsRequire.property("cache"), jsRequire.data() }) {
        v8::Local<v8::Value> unwrapped = ScriptValueV8Wrapper::fullUnwrap(engine, holder).get();
        if (!unwrapped->IsObject()) {
            continue;
        }
        v8::Local<v8::Object> object = unwrapped.As<v8::Object>();
        v8::Local<v8::Value> entry;
        if (object->Get(context, key).ToLocal(&entry) && entry->StrictEquals(module)) {
            auto deleted = object->Delete(context, key);
            Q_UNUSED(deleted);
        }
    }
}

void ScriptModuleLoaderV8::releaseEpoch(ScriptEngineV8* engine, int epoch) {
    // Its loads, import() included, would otherwise evaluate code of an entity script that is gone
    for (auto loader : engine->findChildren<ScriptModuleLoaderV8*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (loader->_request.epoch == epoch && !loader->_finished) {
            loader->abandon();
        }
    }

    auto scopeGuard = engine->getScopeGuard();
    v8::Isolate* isolate = engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engine->_contexts.first()->toV8Value());
    v8::TryCatch tryCatch(isolate);
    for (const auto& record : engine->_modules->releaseEpoch(epoch)) {
        record->released = true;
        record->waiters.clear();
        if (!record->commonJsModule.IsEmpty()) {
            removeRequireCacheEntry(engine, record->url.toString(), record->commonJsModule.Get(isolate));
        }
        record->module.Reset();
        record->jsonValue.Reset();
        record->commonJsError.Reset();
        record->commonJsModule.Reset();
    }
}

void ScriptEngineV8::reportStalledTopLevelAwaits() {
    reportStalledTopLevelAwaits(-1, "Script stopped while a top-level await was still pending at ");
}

void ScriptEngineV8::reportStalledTopLevelAwaits(int epoch, const QString& what) {
    Q_ASSERT(QThread::currentThread() == thread());
    auto scopeGuard = getScopeGuard();
    v8::HandleScope handleScope(_v8Isolate);
    v8::Local<v8::Context> context = _contexts.first()->toV8Value();
    v8::Context::Scope contextScope(context);

    // Every evaluated graph is searched, not only the entry's: import() starts graphs of its own
    QStringList sites;
    QString fileName;
    int line = -1;
    for (const auto& record : _modules->records()) {
        if (record->state != ScriptModuleRecordV8::State::Compiled || (epoch >= 0 && record->epoch != epoch)) {
            continue;
        }
        v8::Local<v8::Module> module = record->module.Get(_v8Isolate);
        // The search walks the requested modules, which are only filled in once the graph is instantiated
        v8::Module::Status status = module->GetStatus();
        if (!module->IsSourceTextModule() || (status != v8::Module::kEvaluating && status != v8::Module::kEvaluated)
            || !module->IsGraphAsync()) {
            continue;
        }
        auto stalled = module->GetStalledTopLevelAwaitMessages(_v8Isolate);
        for (size_t i = 0; i < stalled.second.size(); i++) {
            v8::Local<v8::Message> message = stalled.second[i];
            QString stalledFileName = ScriptModuleLoaderV8::toQString(_v8Isolate, message->GetScriptResourceName());
            int stalledLine = message->GetLineNumber(context).FromMaybe(-1);
            QString site = ScriptModuleLoaderV8::siteText(stalledFileName, stalledLine);
            if (sites.contains(site)) {
                continue;
            }
            if (sites.isEmpty()) {
                fileName = stalledFileName;
                line = stalledLine;
            }
            sites.append(site);
        }
    }
    if (sites.isEmpty()) {
        return;
    }

    // A warning: stopping mid-await (a reload, or a user stop during an await on the network) is routine
    QString message = what + sites.join(", ");
    if (_manager) {
        _manager->scriptWarningMessage(message, fileName, line);
    } else {
        qCWarning(scriptengine_v8) << message;
    }
}

void ScriptModuleLoaderV8::rejectImport(v8::Local<v8::Value> reason) {
    _importResolver->reject(ScriptValue(new ScriptValueV8Wrapper(_engine, V8ScriptValue(_engine, reason))));
    finish(_engine->undefinedValue(), std::shared_ptr<ScriptException>());
}

bool ScriptModuleLoaderV8::abandonIfStopping() {
    auto manager = _engine->manager();
    if (!manager || !manager->isStopping()) {
        return false;
    }
    // A stopped script neither reports nor hears about a load that completes late, e.g. in run()'s final event pass
    abandon();
    return true;
}

void ScriptModuleLoaderV8::abandon() {
    _finished = true;
    _onEvaluated = ScriptModuleCallback();
    _importResolver.reset();
    deleteLater();
}

void ScriptModuleLoaderV8::finish(const ScriptValue& moduleNamespace, std::shared_ptr<ScriptException> error) {
    _finished = true;
    deleteLater();
    _importResolver.reset();
    auto onEvaluated = std::move(_onEvaluated);
    if (onEvaluated) {
        onEvaluated(moduleNamespace, error);
    }
}
