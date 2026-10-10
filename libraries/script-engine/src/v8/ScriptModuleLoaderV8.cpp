//
//  ScriptModuleLoaderV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleLoaderV8.h"

#include <atomic>

#include <QtCore/QPointer>
#include <QtCore/QThread>

#include <DependencyManager.h>
#include <Profile.h>

#include "../BatchLoader.h"
#include "../ScriptCache.h"
#include "../ScriptManager.h"
#include "ScriptContextV8Wrapper.h"
#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

// The module API used here is the same in V8 12.4 (libnode 22) and 13.6 (NixOS nodejs 24)
static_assert(V8_MAJOR_VERSION >= 12, "ES modules need V8 >= 12.4");

// As for Script.require: a missing module should fail the script quickly rather than hold it up
static const int MODULE_FETCH_MAX_RETRIES { 1 };

using State = ScriptModuleRecordV8::State;

QString ScriptModuleLoaderV8::toQString(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    // Converting a thrown object runs its toString(), which may throw in turn
    v8::TryCatch tryCatch(isolate);
    return QString(*v8::String::Utf8Value(isolate, value));
}

QString ScriptModuleLoaderV8::siteText(const QString& url, int line) {
    return line > 0 ? QString("%1:%2").arg(url).arg(line) : url;
}

v8::Local<v8::String> ScriptModuleLoaderV8::toV8String(v8::Isolate* isolate, const QString& text) {
    QByteArray utf8 = text.toUtf8();
    return v8::String::NewFromUtf8(isolate, utf8.constData(), v8::NewStringType::kNormal, utf8.size()).ToLocalChecked();
}

static std::atomic<quint64> nextLoaderId { 1 };

void ScriptEngineV8::loadModule(const ScriptModuleRequest& request, ScriptModuleCallback onEvaluated) {
    Q_ASSERT(QThread::currentThread() == thread());
    auto loader = new ScriptModuleLoaderV8(this, request, std::move(onEvaluated));
    QMetaObject::invokeMethod(loader, "start", Qt::QueuedConnection);
}

ScriptModuleLoaderV8::ScriptModuleLoaderV8(ScriptEngineV8* engine, const ScriptModuleRequest& request,
                                           ScriptModuleCallback onEvaluated) :
    QObject(engine), _engine(engine), _request(request), _onEvaluated(std::move(onEvaluated)), _id(nextLoaderId++) {
}

ScriptModuleLoaderV8::ScriptModuleLoaderV8(ScriptEngineV8* engine, const ScriptModuleRequest& request,
                                           ScriptPromiseResolverPointer importResolver, const ImportSite& site) :
    QObject(engine), _engine(engine), _request(request), _importResolver(std::move(importResolver)), _entrySite(site),
    _id(nextLoaderId++) {
}

void ScriptModuleLoaderV8::start() {
    if (abandonIfStopping()) {
        return;
    }
    auto scopeGuard = _engine->getScopeGuard();
    _entry = _engine->_modules->find(_request.epoch, _request.url);
    if (!_entry) {
        _entry = _engine->_modules->insert(_request.epoch, _request.url);
        if (_request.source.isNull()) {
            fetch(_engine, _entry, _request.forceDownload);
        } else {
            compile(_engine, _entry, _request.source);
        }
    }
    need(_entry, _entrySite);
}

void ScriptModuleLoaderV8::fetch(ScriptEngineV8* engine, const ScriptModuleRecordV8Pointer& record, bool forceDownload) {
    auto scriptCache = DependencyManager::get<ScriptCache>();
    // The BatchLoader pattern: the proxy outlives a loader deleted mid-fetch, and the result belongs to the record
    // (other loaders may be waiting on it), so it is delivered to the engine rather than to this loader.
    auto proxy = new ScriptCacheSignalProxy();
    QObject::connect(scriptCache.data(), &ScriptCache::destroyed, proxy, &ScriptCacheSignalProxy::deleteLater);
    // Queued even on the engine thread: ScriptCache answers a cache hit from inside getScriptContents, which would
    // otherwise compile and walk the module in the middle of walking its importer.
    QObject::connect(proxy, &ScriptCacheSignalProxy::contentAvailable, engine,
        [engine, record](const QString&, const QString& contents, bool isURL, bool success, const QString& status) {
            auto scopeGuard = engine->getScopeGuard();
            if (success && isURL) {
                compile(engine, record, contents);
            } else {
                record->state = State::Failed;
                record->fetchFailed = true;
                record->error = status;
            }
            auto waiters = std::move(record->waiters);
            record->waiters.clear();
            for (auto& waiter : waiters) {
                waiter();
            }
        }, Qt::QueuedConnection);

    scriptCache->getScriptContents(record->url.toString(),
        [proxy](const QString& url, const QString& contents, bool isURL, bool success, const QString& status) {
            proxy->receivedContent(url, contents, isURL, success, status);
            proxy->deleteLater();
        }, forceDownload, MODULE_FETCH_MAX_RETRIES);
}

void ScriptModuleLoaderV8::compile(ScriptEngineV8* engine, const ScriptModuleRecordV8Pointer& record,
                                   const QString& source) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine->_contexts.first()->toV8Value();
    v8::Context::Scope contextScope(context);
    v8::TryCatch tryCatch(isolate);

    QString url = record->url.toString();
    v8::ScriptOrigin origin(toV8String(isolate, url), 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    v8::ScriptCompiler::Source compilerSource(toV8String(isolate, source), origin);
    v8::Local<v8::Module> module;
    if (v8::ScriptCompiler::CompileModule(isolate, &compilerSource).ToLocal(&module)) {
        engine->_modules->setCompiled(isolate, record, module);
        return;
    }

    record->state = State::Failed;
    record->errorFileName = url;
    v8::Local<v8::Message> message = tryCatch.Message();
    record->errorLine = message.IsEmpty() ? -1 : message->GetLineNumber(context).FromMaybe(-1);
    record->error = QString("Error while compiling module %1: %2")
        .arg(siteText(url, record->errorLine), toQString(isolate, tryCatch.Exception()));
}

void ScriptModuleLoaderV8::need(const ScriptModuleRecordV8Pointer& record, const ImportSite& site) {
    if (_finished || abandonIfStopping()) {
        return;
    }
    if (_seen.contains(record.get())) {
        return;
    }
    _seen.insert(record.get());
    _pending++;
    if (record->state == State::Fetching) {
        QPointer<ScriptModuleLoaderV8> self(this);
        std::weak_ptr<ScriptModuleRecordV8> weakRecord(record);
        record->waiters.push_back([self, weakRecord, site] {
            auto record = weakRecord.lock();
            if (self && record) {
                self->walk(record, site);
            }
        });
    } else {
        walk(record, site);
    }
}

void ScriptModuleLoaderV8::walk(const ScriptModuleRecordV8Pointer& record, const ImportSite& site) {
    if (_finished || abandonIfStopping()) {
        return;
    }
    if (record->state == State::Failed) {
        if (!record->fetchFailed) {
            fail(record->error, record->errorFileName, record->errorLine, true);
        } else if (site.importer.isEmpty()) {
            fail(QString("Cannot load module %1 (%2)").arg(record->url.toString(), record->error),
                 record->url.toString(), -1);
        } else {
            fail(QString("Cannot find module '%1' imported from %2 (%3)")
                     .arg(site.specifier, siteText(site.importer.toString(), site.line), record->error),
                 site.importer.toString(), site.line);
        }
        return;
    }

    v8::Isolate* isolate = _engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = _engine->_contexts.first()->toV8Value();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Module> module = record->module.Get(isolate);
    v8::Local<v8::FixedArray> requests = module->GetModuleRequests();
    QString importer = record->url.toString();

    for (int i = 0; i < requests->Length(); i++) {
        v8::Local<v8::ModuleRequest> request = requests->Get(context, i).As<v8::ModuleRequest>();
        ImportSite importSite { toQString(isolate, request->GetSpecifier()), record->url,
                                module->SourceOffsetToLocation(request->GetSourceOffset()).GetLineNumber() + 1 };
        QString error;
        QUrl url = resolveImport(importSite, request->GetImportAttributes()->Length() > 0, _request.sandboxURL, &error);
        if (url.isEmpty()) {
            fail(error, importer, importSite.line);
            return;
        }

        record->resolvedImports.insert(importSite.specifier, url);
        auto dependency = _engine->_modules->find(record->epoch, url);
        if (!dependency) {
            dependency = _engine->_modules->insert(record->epoch, url);
            fetch(_engine, dependency, _request.forceDownload);
        }
        need(dependency, importSite);
        if (_finished) {
            return;
        }
    }

    if (--_pending == 0) {
        instantiateAndEvaluate();
    }
}

v8::MaybeLocal<v8::Module> ScriptModuleLoaderV8::resolveModuleCallback(v8::Local<v8::Context> context,
                                                                       v8::Local<v8::String> specifier,
                                                                       v8::Local<v8::FixedArray>,
                                                                       v8::Local<v8::Module> referrer) {
    v8::Isolate* isolate = context->GetIsolate();
    auto engine = static_cast<ScriptEngineV8*>(isolate->GetData(ENGINE_ISOLATE_DATA_SLOT));
    QString specifierText = toQString(isolate, specifier);
    auto record = engine->_modules->find(isolate, referrer);
    ScriptModuleRecordV8Pointer dependency;
    if (record && record->resolvedImports.contains(specifierText)) {
        dependency = engine->_modules->find(record->epoch, record->resolvedImports.value(specifierText));
    }
    if (!dependency || dependency->state != State::Compiled) {
        // Unreachable while instantiation only starts once the loader has compiled the whole graph
        isolate->ThrowError(toV8String(isolate, "Module '" + specifierText + "' was not loaded"));
        return v8::MaybeLocal<v8::Module>();
    }
    return dependency->module.Get(isolate);
}

void ScriptModuleLoaderV8::initializeImportMeta(v8::Local<v8::Context> context, v8::Local<v8::Module> module,
                                                v8::Local<v8::Object> meta) {
    v8::Isolate* isolate = context->GetIsolate();
    auto engine = static_cast<ScriptEngineV8*>(isolate->GetData(ENGINE_ISOLATE_DATA_SLOT));
    auto record = engine->_modules->find(isolate, module);
    if (!record) {
        qCWarning(scriptengine_v8) << "import.meta of a module that is not in the module map; import.meta.url is unset";
        return;
    }
    auto created = meta->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "url"),
                                            toV8String(isolate, record->url.toString()));
    Q_UNUSED(created);
}

void ScriptModuleLoaderV8::instantiateAndEvaluate() {
    if (abandonIfStopping()) {
        return;
    }
    if (_engine->isScriptOnStack()) {
        // Delivered by a nested event loop inside script code: evaluating now would run module code in the middle of
        // that script, between its call and the return of Script.require, Script.include or a synchronous XHR
        QPointer<ScriptModuleLoaderV8> self(this);
        _engine->deferUntilScriptReturns([self] {
            if (self && !self->_finished) {
                self->instantiateAndEvaluate();
            }
        });
        return;
    }

    PROFILE_RANGE(script, _entry->url.toString());
    v8::Isolate* isolate = _engine->getIsolate();
    MicrotaskCheckpointScopeV8 microtaskCheckpointScope(_engine);
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = _engine->_contexts.first()->toV8Value();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Module> module = _entry->module.Get(isolate);

    {
        v8::TryCatch tryCatch(isolate);
        if (module->InstantiateModule(context, resolveModuleCallback).IsNothing()) {
            QString fileName = _entry->url.toString();
            int line = -1;
            v8::Local<v8::Message> message = tryCatch.Message();
            if (!message.IsEmpty()) {
                v8::Local<v8::Value> resourceName = message->GetScriptResourceName();
                if (resourceName->IsString()) {
                    fileName = toQString(isolate, resourceName);
                }
                line = message->GetLineNumber(context).FromMaybe(-1);
            }
            fail(QString("Error while linking module %1: %2")
                     .arg(siteText(fileName, line), toQString(isolate, tryCatch.Exception())),
                 fileName, line, true);
            return;
        }
    }

    // A module that was evaluated before (a second import(), or one shared with another graph) is not evaluated again:
    // Evaluate() returns the settled promise of its first evaluation, or the pending one of a top-level await.
    v8::TryCatch tryCatch(isolate);
    _engine->_evaluatingCounter++;
    v8::Local<v8::Value> result;
    bool evaluated = module->Evaluate(context).ToLocal(&result);
    _engine->_evaluatingCounter--;
    if (!evaluated) {
        if (tryCatch.HasCaught()) {
            reportEvaluationError(context, tryCatch.Exception());
        } else {
            fail("Evaluation of module " + _entry->url.toString() + " was terminated", _entry->url.toString(), -1);
        }
        return;
    }

    // With top-level await enabled, Evaluate() reports a throw by rejecting the promise it returns
    v8::Local<v8::Promise> promise = result.As<v8::Promise>();
    switch (promise->State()) {
        case v8::Promise::kFulfilled:
            succeed(module);
            return;
        case v8::Promise::kRejected: {
            // A handler stops the rejection from being reported a second time, as an unhandled one
            v8::Local<v8::Function> ignore;
            if (v8::Function::New(context, [](const v8::FunctionCallbackInfo<v8::Value>&) {}).ToLocal(&ignore)) {
                auto handled = promise->Catch(context, ignore);
                Q_UNUSED(handled);
            }
            reportEvaluationError(context, promise->Result());
            return;
        }
        case v8::Promise::kPending:
            break;
    }

    // Suspended in a top-level await. The reactions find the loader by id rather than by pointer, as it is deleted if
    // the script stops first. Attaching them also keeps a later rejection out of the unhandled rejection report.
    v8::Local<v8::Value> id = v8::Number::New(isolate, static_cast<double>(_id));
    v8::Local<v8::Function> onFulfilled;
    v8::Local<v8::Function> onRejected;
    if (v8::Function::New(context, evaluationFulfilled, id).ToLocal(&onFulfilled)
        && v8::Function::New(context, evaluationRejected, id).ToLocal(&onRejected)) {
        auto reactions = promise->Then(context, onFulfilled, onRejected);
        Q_UNUSED(reactions);
    }
}

ScriptModuleLoaderV8* ScriptModuleLoaderV8::findPendingEvaluation(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto engine = static_cast<ScriptEngineV8*>(info.GetIsolate()->GetData(ENGINE_ISOLATE_DATA_SLOT));
    auto id = static_cast<quint64>(info.Data().As<v8::Number>()->Value());
    for (auto loader : engine->findChildren<ScriptModuleLoaderV8*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (loader->_id == id) {
            return loader->_finished || loader->abandonIfStopping() ? nullptr : loader;
        }
    }
    return nullptr;
}

void ScriptModuleLoaderV8::evaluationFulfilled(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (auto loader = findPendingEvaluation(info)) {
        v8::HandleScope handleScope(info.GetIsolate());
        loader->succeed(loader->_entry->module.Get(info.GetIsolate()));
    }
}

void ScriptModuleLoaderV8::evaluationRejected(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (auto loader = findPendingEvaluation(info)) {
        v8::HandleScope handleScope(info.GetIsolate());
        loader->reportEvaluationError(info.GetIsolate()->GetCurrentContext(), info[0]);
    }
}

void ScriptModuleLoaderV8::succeed(v8::Local<v8::Module> module) {
    ScriptValue moduleNamespace(new ScriptValueV8Wrapper(_engine, V8ScriptValue(_engine, module->GetModuleNamespace())));
    if (_importResolver) {
        _importResolver->resolve(moduleNamespace);
    }
    finish(moduleNamespace, std::shared_ptr<ScriptException>());
}

void ScriptModuleLoaderV8::reportEvaluationError(v8::Local<v8::Context> context, v8::Local<v8::Value> exception) {
    if (abandonIfStopping()) {
        return;
    }
    if (_importResolver) {
        // As in the spec: the module stays errored, and every later import() of it rejects with this same value
        rejectImport(exception);
        return;
    }

    v8::Isolate* isolate = _engine->getIsolate();
    QString fileName = _entry->url.toString();
    int line = -1;
    int column = 0;
    v8::Local<v8::Message> message = v8::Exception::CreateMessage(isolate, exception);
    if (!message.IsEmpty()) {
        v8::Local<v8::Value> resourceName = message->GetScriptResourceName();
        if (resourceName->IsString()) {
            fileName = toQString(isolate, resourceName);
        }
        line = message->GetLineNumber(context).FromMaybe(-1);
        column = message->GetStartColumn(context).FromMaybe(0);
    }

    QString text = toQString(isolate, exception);
    QStringList backtrace;
    v8::Local<v8::Value> stack;
    if (exception->IsNativeError()) {
        v8::TryCatch tryCatch(isolate);
        if (exception.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "stack")).ToLocal(&stack)
            && stack->IsString()) {
            backtrace = toQString(isolate, stack).split("\n");
        }
    }

    auto runtimeException = std::make_shared<ScriptRuntimeException>(text, "module evaluation", line, column, backtrace);
    runtimeException->thrownValue = ScriptValue(new ScriptValueV8Wrapper(_engine, V8ScriptValue(_engine, exception)));
    report(QString("Error while evaluating module %1: %2").arg(siteText(fileName, line), text), fileName, line,
           runtimeException);
}

void ScriptModuleLoaderV8::fail(const QString& message, const QString& fileName, int line, bool syntaxError) {
    if (abandonIfStopping()) {
        return;
    }
    if (_importResolver) {
        // Only the import() promise rejects. The script handles that, or it is reported as an unhandled rejection.
        v8::Isolate* isolate = _engine->getIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Context::Scope contextScope(_engine->_contexts.first()->toV8Value());
        v8::Local<v8::String> text = toV8String(isolate, message);
        rejectImport(syntaxError ? v8::Exception::SyntaxError(text) : v8::Exception::TypeError(text));
        return;
    }
    report(message, fileName, line, std::make_shared<ScriptEngineException>(message, "module loading", line));
}

void ScriptModuleLoaderV8::report(const QString& message, const QString& fileName, int line,
                                  std::shared_ptr<ScriptException> exception) {
    if (auto manager = _engine->manager()) {
        manager->scriptErrorMessage(message, fileName, line);
    } else {
        qCWarning(scriptengine_v8) << message;
    }
    _engine->setUncaughtException(exception);
    finish(_engine->undefinedValue(), exception);
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
    _finished = true;
    _onEvaluated = ScriptModuleCallback();
    _importResolver.reset();
    deleteLater();
    return true;
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
