//
//  ScriptErrorConstructorsV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptErrorConstructorsV8.h"

#include "ScriptEngineLoggingV8.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"

// Index 0 has a special meaning for Chrome's debugger
static const int EMBEDDER_DATA_INDEX { 1 };

static const char* const ERROR_TYPES[] = { "Error", "EvalError", "RangeError", "ReferenceError", "SyntaxError",
                                           "TypeError", "URIError" };

void ScriptErrorConstructorsV8::capture(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Object> constructors = v8::Object::New(isolate, v8::Null(isolate), nullptr, nullptr, 0);
    for (const char* type : ERROR_TYPES) {
        v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, type).ToLocalChecked();
        v8::Local<v8::Value> constructor;
        if (!context->Global()->Get(context, name).ToLocal(&constructor) || !constructor->IsFunction()
            || !constructors->CreateDataProperty(context, name, constructor).FromMaybe(false)) {
            Q_ASSERT(false);
        }
    }
    context->SetEmbedderData(EMBEDDER_DATA_INDEX, constructors);
}

static v8::Local<v8::Value> captured(v8::Isolate* isolate, v8::Local<v8::Context> context, const QString& type) {
    v8::Local<v8::Object> constructors = context->GetEmbedderData(EMBEDDER_DATA_INDEX).As<v8::Object>();
    v8::Local<v8::String> name = v8::String::NewFromTwoByte(isolate, reinterpret_cast<const uint16_t*>(type.utf16()),
                                                            v8::NewStringType::kNormal, type.length()).ToLocalChecked();
    // Data properties on an object without a prototype: the lookup runs no script
    return constructors->Get(context, name).ToLocalChecked();
}

ScriptValue ScriptErrorConstructorsV8::find(ScriptEngineV8* engine, const QString& type, const ScriptValue& other) {
    v8::Isolate* isolate = engine->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Local<v8::Context> context = engine->getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Value> constructor = captured(isolate, context, type);
    if (!constructor->IsFunction() && other.isObject()) {
        constructor = captured(isolate, context, other.prototype().property("constructor").property("name").toString());
    }
    if (!constructor->IsFunction()) {
#ifdef DEBUG_JS_EXCEPTIONS
        qCDebug(scriptengine_v8) << "ScriptErrorConstructorsV8::find -- no constructor for" << type << "-- using Error";
#endif
        constructor = captured(isolate, context, "Error");
    }
    return ScriptValue(new ScriptValueV8Wrapper(engine, V8ScriptValue(engine, constructor)));
}

v8::Local<v8::Object> ScriptErrorConstructorsV8::error(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    return captured(isolate, context, "Error").As<v8::Object>();
}
