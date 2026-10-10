//
//  ScriptModuleImportV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// Import resolution, dynamic import(), the stalled top-level await report, and deferring module evaluation out of
// nested event loops

#include "ScriptModuleLoaderV8.h"

#include <utility>

#include <QtCore/QStringList>
#include <QtCore/QThread>

#include "../ScriptEnvironment.h"
#include "../ScriptManager.h"
#include "../ScriptModuleResolver.h"
#include "ScriptContextV8Wrapper.h"
#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

QUrl ScriptModuleLoaderV8::resolveImport(const ImportSite& site, bool hasImportAttributes, const QUrl& sandboxURL,
                                         QString* error) {
    QString where = siteText(site.importer.toString(), site.line);
    // Hosts must reject attributes they do not support rather than ignore them
    if (hasImportAttributes) {
        // Slice 4 of #10 accepts { type: 'json' } here
        *error = QString("Cannot import '%1' from %2: import attributes are not supported yet").arg(site.specifier, where);
        return QUrl();
    }

    QString resolveError;
    QUrl url = resolveModuleSpecifier(site.specifier, site.importer, sandboxURL, &resolveError);
    if (url.isEmpty()) {
        *error = QString("Cannot find module '%1' imported from %2 (%3)").arg(site.specifier, where, resolveError);
        return QUrl();
    }
    if (!isModuleURL(url)) {
        // Slice 4 of #10 replaces the .js refusal with a CommonJS synthetic module
        QString why = url.path().endsWith(".js", Qt::CaseInsensitive)
            ? "importing a classic .js script from a module is not supported yet; use a .mjs module"
            : "only .mjs files are ES modules";
        *error = QString("Cannot import '%1' from %2: %3").arg(site.specifier, where, why);
        return QUrl();
    }
    return url;
}

v8::MaybeLocal<v8::Promise> ScriptModuleLoaderV8::importModuleDynamically(v8::Local<v8::Context> context,
                                                                          v8::Local<v8::Data>,
                                                                          v8::Local<v8::Value> resourceName,
                                                                          v8::Local<v8::String> specifier,
                                                                          v8::Local<v8::FixedArray> importAttributes) {
    v8::Isolate* isolate = context->GetIsolate();
    auto engine = static_cast<ScriptEngineV8*>(isolate->GetData(ENGINE_ISOLATE_DATA_SLOT));
    ScriptManager* manager = engine->manager();

    // The referrer is the calling module or classic script. Code without a resource name (eval, new Function) counts
    // as the manager's script.
    ImportSite site;
    site.specifier = toQString(isolate, specifier);
    if (resourceName->IsString()) {
        site.importer = QUrl(toQString(isolate, resourceName));
    }
    if (site.importer.isEmpty() && manager) {
        site.importer = QUrl(manager->getAbsoluteFilename());
    }
    v8::Local<v8::StackTrace> stack = v8::StackTrace::CurrentStackTrace(isolate, 1);
    if (stack->GetFrameCount() > 0) {
        site.line = stack->GetFrame(isolate, 0)->GetLineNumber();
    }
    // From module code, the caller's environment: its sandbox and epoch, and it records what the import loads. From
    // classic code, a new one for the imported graph, with the script's entity and sandbox (an entity script's URL, or
    // the manager's own as for a top-level module's imports), recording the importer too.
    ScriptEnvironmentPointer environment = engine->currentEnvironment();
    if (!environment) {
        EntityItemID entityID = manager ? manager->currentEntityIdentifier() : EntityItemID();
        QUrl classicSandboxURL = manager ? manager->currentSandboxURL() : QUrl();
        if (classicSandboxURL.isEmpty() && manager) {
            classicSandboxURL = QUrl(manager->getAbsoluteFilename());
        }
        // Epoch 0 until entity scripts (#11) carry theirs in the ScriptOrigin's host-defined options
        environment = std::make_shared<ScriptEnvironment>(entityID, classicSandboxURL, 0);
        environment->noteLoaded(site.importer);
    }
    QUrl sandboxURL = environment->sandboxURL;

    ScriptPromise promise = engine->newPromise();
    QString error;
    QUrl url = resolveImport(site, importAttributes->Length() > 0, sandboxURL, &error);
    if (url.isEmpty()) {
        V8ScriptValue reason(engine, v8::Exception::TypeError(toV8String(isolate, error)));
        promise.resolver->reject(ScriptValue(new ScriptValueV8Wrapper(engine, std::move(reason))));
    } else {
        // The start is queued so that nothing is evaluated inside this callback, even when the whole graph is loaded
        // already.
        auto loader = new ScriptModuleLoaderV8(engine, { url, QString(), sandboxURL, environment->epoch, false, environment },
                                               promise.resolver, site);
        QMetaObject::invokeMethod(loader, "start", Qt::QueuedConnection);
    }
    return ScriptValueV8Wrapper::fullUnwrap(engine, promise.promise).get().As<v8::Promise>();
}

void ScriptEngineV8::reportStalledTopLevelAwaits() {
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
        if (record->state != ScriptModuleRecordV8::State::Compiled) {
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
    QString message = "Script stopped while a top-level await was still pending at " + sites.join(", ");
    if (_manager) {
        _manager->scriptWarningMessage(message, fileName, line);
    } else {
        qCWarning(scriptengine_v8) << message;
    }
}

void ScriptEngineV8::deferUntilScriptReturns(std::function<void()> task) {
    Q_ASSERT(QThread::currentThread() == thread());
    _deferredUntilScriptReturns.push_back(std::move(task));
}

void ScriptEngineV8::postDeferredUntilScriptReturns() {
    // Posted rather than run: the outermost call into script has returned, but the C++ that made it has not
    auto tasks = std::exchange(_deferredUntilScriptReturns, std::vector<std::function<void()>>());
    for (auto& task : tasks) {
        QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
    }
}
