//
//  ScriptErrorConstructorsV8.h
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptErrorConstructorsV8_h
#define overte_ScriptErrorConstructorsV8_h

#include <QtCore/QString>

#include <v8.h>

#include "../ScriptValue.h"

class ScriptEngineV8;

// The built-in error constructors of a context, captured when the context is created, so that the errors the engine
// makes do not depend on globals a script may have replaced
namespace ScriptErrorConstructorsV8 {

// Called on a new context, before any script runs in it
void capture(v8::Isolate* isolate, v8::Local<v8::Context> context);

// The captured constructor for an error of the given type in the engine's current context. For a type that is not a
// built-in error, the one named by other's constructor if that is a built-in error, else Error.
ScriptValue find(ScriptEngineV8* engine, const QString& type, const ScriptValue& other);

// The captured Error constructor of the context
v8::Local<v8::Object> error(v8::Isolate* isolate, v8::Local<v8::Context> context);

}

#endif // overte_ScriptErrorConstructorsV8_h
