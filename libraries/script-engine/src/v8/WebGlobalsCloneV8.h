//
//  WebGlobalsCloneV8.h
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

#ifndef overte_WebGlobalsCloneV8_h
#define overte_WebGlobalsCloneV8_h

#include <string>

#include <v8-forward.h>

/// Copies a value through V8's serializer into the context, moving the ArrayBuffers of the transfer list. False with the
/// message when the value or the list cannot be cloned; false with an empty message and an exception pending when script
/// code (a getter of the value or the list) threw, or the list holds a primitive (a TypeError).
bool cloneValueV8(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                  v8::Local<v8::Array> transferList, v8::Local<v8::Value>& copy, std::string& dataCloneError);

#endif // overte_WebGlobalsCloneV8_h

/// @}
