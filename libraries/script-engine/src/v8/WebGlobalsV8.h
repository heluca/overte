//
//  WebGlobalsV8.h
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

#ifndef overte_WebGlobalsV8_h
#define overte_WebGlobalsV8_h

#include "../ScriptValue.h"

class ScriptContext;
class ScriptEngine;

/// The web-globals natives that need V8 itself, registered by registerWebGlobals() the same way the Qt ones are.

// ScriptValue::toString() and ScriptEngine::newValue(QString) stop at a NUL, so the natives that take or return arbitrary
// strings work on the V8 strings directly.

/// encodeUtf8(string) -> ArrayBuffer of its UTF-8, with a lone surrogate as U+FFFD
ScriptValue webGlobalsEncodeUtf8(ScriptContext* context, ScriptEngine* engine);

/// base64Encode(binaryString) -> base64, for a string the prelude has checked is all U+0000 - U+00FF
ScriptValue webGlobalsBase64Encode(ScriptContext* context, ScriptEngine* engine);

/// base64Decode(base64) -> binary string, for base64 the prelude has checked and stripped of padding and whitespace
ScriptValue webGlobalsBase64Decode(ScriptContext* context, ScriptEngine* engine);

/// decodeUtf8(view, fatal, stream) -> [text, pendingByteCount], or null if fatal is set and the bytes are not valid
/// UTF-8. With stream, a valid but incomplete sequence at the end is not decoded; its length is the pending count.
ScriptValue webGlobalsDecodeUtf8(ScriptContext* context, ScriptEngine* engine);

/// clone(value, transferList) -> [true, copy], or [false, message] when the value cannot be cloned. The copy is made
/// in the engine's current context.
ScriptValue webGlobalsClone(ScriptContext* context, ScriptEngine* engine);

#endif // overte_WebGlobalsV8_h

/// @}
