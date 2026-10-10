//
//  ScriptModuleLoaderV8.h
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_ScriptModuleLoaderV8_h
#define overte_ScriptModuleLoaderV8_h

#include <memory>

#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include "v8.h"

#include "../ScriptEngine.h"
#include "ScriptModuleMapV8.h"

class ScriptEngineV8;

/// [V8] Loads one ES module graph for ScriptEngine::loadModule.
///
/// Fetch phase: each module is fetched through ScriptCache and compiled on the engine thread as its source arrives;
/// its static imports are then resolved and fetched in turn. Nothing waits in a nested event loop, the engine thread
/// keeps running its own loop. Records already in the module map are reused, including ones another loader is still
/// fetching, so a module shared by several importers or by a cycle is fetched and compiled once.
///
/// When every module of the graph is compiled, the entry is linked and evaluated in the engine's main context, from
/// the event loop with no script code on the stack. The loader deletes itself after calling back.
class ScriptModuleLoaderV8 : public QObject {
    Q_OBJECT

public:
    ScriptModuleLoaderV8(ScriptEngineV8* engine, const ScriptModuleRequest& request, ScriptModuleCallback onEvaluated);

    Q_INVOKABLE void start();

    /// For Isolate::SetHostInitializeImportMetaObjectCallback
    static void initializeImportMeta(v8::Local<v8::Context> context, v8::Local<v8::Module> module,
                                     v8::Local<v8::Object> meta);

private:
    /// Where a module is imported, for error messages: an empty importer is the entry
    struct ImportSite {
        QString specifier;
        QUrl importer;
        int line { -1 };
    };

    static void fetch(ScriptEngineV8* engine, const ScriptModuleRecordV8Pointer& record, bool forceDownload);
    static void compile(ScriptEngineV8* engine, const ScriptModuleRecordV8Pointer& record, const QString& source);
    static v8::MaybeLocal<v8::Module> resolveModuleCallback(v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
                                                            v8::Local<v8::FixedArray> importAttributes,
                                                            v8::Local<v8::Module> referrer);

    void need(const ScriptModuleRecordV8Pointer& record, const ImportSite& site);
    void walk(const ScriptModuleRecordV8Pointer& record, const ImportSite& site);
    void instantiateAndEvaluate();
    void reportEvaluationError(v8::Local<v8::Context> context, v8::Local<v8::Value> exception);
    void fail(const QString& message, const QString& fileName, int line,
              std::shared_ptr<ScriptException> exception = std::shared_ptr<ScriptException>());
    void finish(const ScriptValue& moduleNamespace, std::shared_ptr<ScriptException> error);

    ScriptEngineV8* _engine;
    ScriptModuleRequest _request;
    ScriptModuleCallback _onEvaluated;
    ScriptModuleRecordV8Pointer _entry;
    QSet<ScriptModuleRecordV8*> _seen;
    // Records seen but not yet walked; the graph is complete when this drops to zero
    int _pending { 0 };
    bool _finished { false };
};

#endif  // overte_ScriptModuleLoaderV8_h

/// @}
