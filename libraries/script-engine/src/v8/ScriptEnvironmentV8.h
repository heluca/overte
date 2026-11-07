//
//  ScriptEnvironmentV8.h
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

#ifndef overte_ScriptEnvironmentV8_h
#define overte_ScriptEnvironmentV8_h

#include <memory>
#include <unordered_map>

#include "v8.h"

#include "../ScriptEnvironment.h"

class ScriptEngineV8;

/// [V8] Holds the current ScriptEnvironment in the isolate's continuation-preserved embedder data (CPED), which V8
/// stores with every promise reaction and microtask when it is created and restores while it runs: what an
/// <code>await</code> resumes in is the environment it suspended in.
///
/// CPED holds a JS object per environment, whose internal field points to the environment's entry here. The object is
/// held weakly; once no continuation refers to it, it is collected and the entry dropped, and a later use of the same
/// environment makes a new object.
class ScriptEnvironmentTableV8 {
public:
    explicit ScriptEnvironmentTableV8(ScriptEngineV8* engine);
    ~ScriptEnvironmentTableV8();
    ScriptEnvironmentTableV8(const ScriptEnvironmentTableV8&) = delete;
    ScriptEnvironmentTableV8& operator=(const ScriptEnvironmentTableV8&) = delete;

    ScriptEnvironmentPointer current();
    void setCurrent(const ScriptEnvironmentPointer& environment);

    /// Whether CPED survives an await in this V8 build. A build without V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    /// accepts and ignores it, and every continuation of module code would silently run as classic code. Run once,
    /// with the engine's main context entered and no microtask queued.
    static bool selfTest(v8::Isolate* isolate, v8::Local<v8::Context> context);

private:
    struct Entry {
        ScriptEnvironmentTableV8* table;
        ScriptEnvironmentPointer environment;
        v8::Global<v8::Object> handle;
    };
    static void collected(const v8::WeakCallbackInfo<Entry>& info);

    ScriptEngineV8* _engine;
    v8::Global<v8::ObjectTemplate> _template;
    std::unordered_map<const ScriptEnvironment*, std::unique_ptr<Entry>> _entries;
};

#endif  // overte_ScriptEnvironmentV8_h

/// @}
