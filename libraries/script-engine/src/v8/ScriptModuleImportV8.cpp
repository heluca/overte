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

// Import resolution, dynamic import(), and deferring module evaluation out of nested event loops

#include "ScriptModuleLoaderV8.h"

#include <utility>

#include <QtCore/QThread>

#include "../ScriptEnvironment.h"
#include "../ScriptManager.h"
#include "../ScriptModuleResolver.h"
#include "ScriptContextV8Wrapper.h"
#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

ScriptModuleLoaderV8::ImportAttributes ScriptModuleLoaderV8::importAttributes(v8::Local<v8::Context> context,
                                                                             v8::Local<v8::FixedArray> attributes,
                                                                             int entrySize) {
    v8::Isolate* isolate = context->GetIsolate();
    ImportAttributes pairs;
    for (int i = 0; i + 1 < attributes->Length(); i += entrySize) {
        pairs.append({ toQString(isolate, attributes->Get(context, i).As<v8::Value>()),
                       toQString(isolate, attributes->Get(context, i + 1).As<v8::Value>()) });
    }
    return pairs;
}

QUrl ScriptModuleLoaderV8::resolveImport(const ImportSite& site, const ImportAttributes& attributes,
                                         const QUrl& sandboxURL, QString* error) {
    QString where = siteText(site.importer.toString(), site.line);
    auto refuse = [&](const QString& why) {
        *error = QString("Cannot import '%1' from %2: %3").arg(site.specifier, where, why);
        return QUrl();
    };
    // Hosts must reject attributes they do not support rather than ignore them
    bool json = false;
    for (const auto& attribute : attributes) {
        if (attribute.first != "type") {
            return refuse(QString("unsupported import attribute '%1'; only type: 'json' is supported").arg(attribute.first));
        }
        if (attribute.second != "json") {
            return refuse(QString("unsupported import type '%1'; only type: 'json' is supported").arg(attribute.second));
        }
        json = true;
    }

    QString resolveError;
    QUrl url = resolveModuleSpecifier(site.specifier, site.importer, sandboxURL, &resolveError);
    if (url.isEmpty()) {
        *error = QString("Cannot find module '%1' imported from %2 (%3)").arg(site.specifier, where, resolveError);
        return QUrl();
    }
    switch (moduleKindOf(url)) {
        case ScriptModuleKind::SourceText:
        case ScriptModuleKind::CommonJS:
            return json ? refuse("with { type: 'json' } is only for .json files") : url;
        case ScriptModuleKind::Json:
            return json ? url : refuse("a .json file can only be imported with { type: 'json' }");
        case ScriptModuleKind::Unsupported:
            break;
    }
    return refuse("only .mjs (ES module), .js (CommonJS) and .json files can be imported");
}

v8::MaybeLocal<v8::Promise> ScriptModuleLoaderV8::importModuleDynamically(v8::Local<v8::Context> context,
                                                                          v8::Local<v8::Data>,
                                                                          v8::Local<v8::Value> resourceName,
                                                                          v8::Local<v8::String> specifier,
                                                                          v8::Local<v8::FixedArray> attributes) {
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
    QString error = MODULES_UNAVAILABLE;
    QUrl url = engine->isEnvironmentCarriedAcrossAwait()
        ? resolveImport(site, importAttributes(context, attributes, 2), sandboxURL, &error) : QUrl();
    if (url.isEmpty()) {
        V8ScriptValue reason(engine, v8::Exception::TypeError(toV8String(isolate, error)));
        promise.resolver->reject(ScriptValue(new ScriptValueV8Wrapper(engine, std::move(reason))));
    } else {
        // The start is queued so that nothing is evaluated inside this callback, even when the whole graph is loaded
        // already.
        ScriptModuleRequest request { url, QString(), sandboxURL, environment->epoch, false, environment };
        engine->applyModuleEpochDownload(request);
        auto loader = new ScriptModuleLoaderV8(engine, request, promise.resolver, site);
        QMetaObject::invokeMethod(loader, "start", Qt::QueuedConnection);
    }
    return ScriptValueV8Wrapper::fullUnwrap(engine, promise.promise).get().As<v8::Promise>();
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
