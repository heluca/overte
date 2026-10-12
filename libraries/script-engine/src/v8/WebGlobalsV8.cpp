//
//  WebGlobalsV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "WebGlobalsV8.h"

#include <climits>

#include <QByteArray>
#include <QCryptographicHash>
#include <vector>

#include "../ScriptContext.h"
#include "../ScriptEngine.h"
#include "ScriptEngineV8.h"
#include "ScriptValueV8Wrapper.h"
#include "V8Types.h"
#include "WebGlobalsCloneV8.h"

namespace {

ScriptValue wrap(ScriptEngineV8* engine, v8::Local<v8::Value> value) {
    return ScriptValue(new ScriptValueV8Wrapper(engine, V8ScriptValue(engine, value)));
}

v8::Local<v8::Value> argument(ScriptContext* context, int index) {
    return ScriptValueV8Wrapper::unwrap(context->argument(index))->toV8Value().constGet();
}

struct Utf8Scan {
    bool valid { true };
    // Bytes at the end that start a valid sequence the input does not finish
    size_t pending { 0 };
};

// The WHATWG UTF-8 decoder's byte ranges, without decoding: V8 does the decoding, with the same U+FFFD replacement
Utf8Scan scanUtf8(const uint8_t* bytes, size_t length) {
    Utf8Scan result;
    size_t i = 0;
    while (i < length) {
        const uint8_t lead = bytes[i];
        if (lead < 0x80) {
            i++;
            continue;
        }
        int needed;
        uint8_t lower = 0x80;
        uint8_t upper = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            needed = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            needed = 2;
            lower = lead == 0xE0 ? 0xA0 : lower;
            upper = lead == 0xED ? 0x9F : upper;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            needed = 3;
            lower = lead == 0xF0 ? 0x90 : lower;
            upper = lead == 0xF4 ? 0x8F : upper;
        } else {
            result.valid = false;
            i++;
            continue;
        }
        size_t next = i + 1;
        int seen = 0;
        while (seen < needed && next < length && bytes[next] >= lower && bytes[next] <= upper) {
            lower = 0x80;
            upper = 0xBF;
            next++;
            seen++;
        }
        if (seen == needed) {
            i = next;
        } else if (next == length) {
            result.pending = length - i;
            return result;
        } else {
            result.valid = false;
            i = next;
        }
    }
    return result;
}

ScriptValue throwTypeError(ScriptContext* context, ScriptEngine* engine, const QString& message) {
    return context->throwValue(engine->makeError(engine->newValue(message), "TypeError"));
}

ScriptValue cloneResult(ScriptEngine* engine, bool ok, const ScriptValue& valueOrMessage) {
    ScriptValue result = engine->newArray(2);
    result.setProperty(0u, engine->newValue(ok));
    result.setProperty(1u, valueOrMessage);
    return result;
}

}  // namespace

ScriptValue webGlobalsEncodeUtf8(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engineV8->getContext());

    v8::Local<v8::Value> input = argument(context, 0);
    if (!input->IsString()) {
        return throwTypeError(context, engine, "encodeUtf8: expected a string");
    }
    auto text = input.As<v8::String>();
    const int length = text->Utf8Length(isolate);
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, length);
    text->WriteUtf8(isolate, static_cast<char*>(buffer->Data()), length, nullptr,
                    v8::String::NO_NULL_TERMINATION | v8::String::REPLACE_INVALID_UTF8);
    return wrap(engineV8, buffer);
}

ScriptValue webGlobalsBase64Encode(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engineV8->getContext());

    v8::Local<v8::Value> input = argument(context, 0);
    if (!input->IsString() || !input.As<v8::String>()->ContainsOnlyOneByte()) {
        return throwTypeError(context, engine, "base64Encode: expected a string of U+0000 - U+00FF");
    }
    auto text = input.As<v8::String>();
    QByteArray bytes(text->Length(), Qt::Uninitialized);
    text->WriteOneByte(isolate, reinterpret_cast<uint8_t*>(bytes.data()), 0, bytes.size(), v8::String::NO_NULL_TERMINATION);
    return engine->newValue(QString::fromLatin1(bytes.toBase64()));
}

ScriptValue webGlobalsBase64Decode(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engineV8->getContext());

    if (!context->argument(0).isString()) {
        return throwTypeError(context, engine, "base64Decode: expected a string");
    }
    const QByteArray bytes = QByteArray::fromBase64(context->argument(0).toString().toLatin1());
    v8::Local<v8::String> text;
    if (!v8::String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(bytes.constData()), v8::NewStringType::kNormal,
                                    bytes.size()).ToLocal(&text)) {
        return context->throwValue(engine->makeError(engine->newValue("atob: the text is too long"), "RangeError"));
    }
    return wrap(engineV8, text);
}

ScriptValue webGlobalsDigest(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engineV8->getContext());

    const QString name = context->argument(0).isString() ? context->argument(0).toString() : QString();
    QCryptographicHash::Algorithm algorithm;
    if (name == "SHA-1") {
        algorithm = QCryptographicHash::Sha1;
    } else if (name == "SHA-256") {
        algorithm = QCryptographicHash::Sha256;
    } else if (name == "SHA-384") {
        algorithm = QCryptographicHash::Sha384;
    } else if (name == "SHA-512") {
        algorithm = QCryptographicHash::Sha512;
    } else {
        return throwTypeError(context, engine, "digest: expected SHA-1, SHA-256, SHA-384 or SHA-512");
    }
    v8::Local<v8::Value> input = argument(context, 1);
    if (!input->IsArrayBuffer()) {
        return throwTypeError(context, engine, "digest: expected an ArrayBuffer");
    }
    auto buffer = input.As<v8::ArrayBuffer>();
    const size_t length = buffer->ByteLength();
    if (length > MAX_DIGEST_BYTES) {
        return context->throwValue(engine->makeError(
            engine->newValue("crypto.subtle.digest: the data is over the limit of 256 MiB"), "RangeError"));
    }
    QCryptographicHash hash(algorithm);
    hash.addData(static_cast<const char*>(buffer->Data()), (int)length);
    ScriptPromise promise = engine->newPromise();
    promise.resolver->resolve(engine->newArrayBuffer(hash.result()));
    return promise.promise;
}

ScriptValue webGlobalsDecodeUtf8(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(engineV8->getContext());

    v8::Local<v8::Value> input = argument(context, 0);
    if (!input->IsArrayBufferView()) {
        return throwTypeError(context, engine, "decodeUtf8: expected an ArrayBufferView");
    }
    auto view = input.As<v8::ArrayBufferView>();
    const bool fatal = context->argument(1).toBool();
    const bool stream = context->argument(2).toBool();
    std::vector<char> bytes(view->ByteLength());
    view->CopyContents(bytes.data(), bytes.size());

    const Utf8Scan scan = scanUtf8(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (fatal && (!scan.valid || (!stream && scan.pending > 0))) {
        return engine->nullValue();
    }
    const size_t pending = stream ? scan.pending : 0;
    const size_t complete = bytes.size() - pending;
    v8::Local<v8::String> text;
    if (complete > (size_t)INT_MAX ||
        !v8::String::NewFromUtf8(isolate, bytes.data(), v8::NewStringType::kNormal, (int)complete).ToLocal(&text)) {
        return context->throwValue(engine->makeError(engine->newValue("TextDecoder: the text is too long"), "RangeError"));
    }
    v8::Local<v8::Value> parts[] = { text, v8::Integer::NewFromUnsigned(isolate, (uint32_t)pending) };
    return wrap(engineV8, v8::Array::New(isolate, parts, 2));
}

ScriptValue webGlobalsClone(ScriptContext* context, ScriptEngine* engine) {
    auto engineV8 = static_cast<ScriptEngineV8*>(engine);
    auto isolate = engineV8->getIsolate();
    v8::HandleScope handleScope(isolate);
    auto v8Context = engineV8->getContext();
    v8::Context::Scope contextScope(v8Context);

    v8::Local<v8::Value> transferList = argument(context, 1);
    if (!transferList->IsArray()) {
        return throwTypeError(context, engine, "clone: expected a transfer list array");
    }
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::Value> copy;
    std::string dataCloneError;
    if (cloneValueV8(isolate, v8Context, argument(context, 0), transferList.As<v8::Array>(), copy, dataCloneError)) {
        return cloneResult(engine, true, wrap(engineV8, copy));
    }
    if (!dataCloneError.empty()) {
        return cloneResult(engine, false, engine->newValue(QString::fromStdString(dataCloneError)));
    }
    tryCatch.ReThrow();
    return engine->undefinedValue();
}
