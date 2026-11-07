//
//  WebGlobalsCloneV8.cpp
//  libraries/script-engine/src/v8
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

// Compiled without RTTI, as libnode is (see CMakeLists.txt), so it includes nothing but V8 and the standard library

#include "WebGlobalsCloneV8.h"

#include <vector>

#include <v8.h>

namespace {

class CloneDelegate : public v8::ValueSerializer::Delegate {
public:
    explicit CloneDelegate(v8::Isolate* isolate) : _isolate(isolate) {}

    void ThrowDataCloneError(v8::Local<v8::String> message) override {
        _message = *v8::String::Utf8Value(_isolate, message);
        _isolate->ThrowException(v8::Exception::Error(message));
    }

    // Overte's QObject and variant proxies are objects with internal fields, which V8 hands to the delegate
    v8::Maybe<bool> WriteHostObject(v8::Isolate* isolate, v8::Local<v8::Object>) override {
        ThrowDataCloneError(v8::String::NewFromUtf8Literal(isolate, "An Overte native object could not be cloned."));
        return v8::Nothing<bool>();
    }

    v8::Maybe<uint32_t> GetSharedArrayBufferId(v8::Isolate* isolate, v8::Local<v8::SharedArrayBuffer>) override {
        ThrowDataCloneError(v8::String::NewFromUtf8Literal(isolate, "A SharedArrayBuffer could not be cloned."));
        return v8::Nothing<uint32_t>();
    }

    const std::string& message() const { return _message; }

private:
    v8::Isolate* _isolate;
    std::string _message;
};

}  // namespace

bool cloneValueV8(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                  v8::Local<v8::Array> transferList, v8::Local<v8::Value>& copy, std::string& dataCloneError) {
    CloneDelegate delegate(isolate);
    v8::ValueSerializer serializer(isolate, &delegate);

    std::vector<v8::Local<v8::ArrayBuffer>> transfers;
    for (uint32_t i = 0; i < transferList->Length(); i++) {
        v8::Local<v8::Value> item;
        if (!transferList->Get(context, i).ToLocal(&item)) {
            return false;
        }
        if (!item->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: the transfer list holds a value that is not an object")));
            return false;
        }
        if (!item->IsArrayBuffer()) {
            dataCloneError = "Only an ArrayBuffer can be transferred.";
            return false;
        }
        auto buffer = item.As<v8::ArrayBuffer>();
        if (buffer->WasDetached() || !buffer->IsDetachable()) {
            dataCloneError = "A detached ArrayBuffer cannot be transferred.";
            return false;
        }
        for (const auto& seen : transfers) {
            if (seen->StrictEquals(buffer)) {
                dataCloneError = "An ArrayBuffer is in the transfer list more than once.";
                return false;
            }
        }
        serializer.TransferArrayBuffer((uint32_t)transfers.size(), buffer);
        transfers.push_back(buffer);
    }

    serializer.WriteHeader();
    if (!serializer.WriteValue(context, value).FromMaybe(false)) {
        dataCloneError = delegate.message();
        return false;
    }
    // A getter of the value can detach a listed buffer while it is written
    for (const auto& buffer : transfers) {
        if (buffer->WasDetached()) {
            dataCloneError = "An ArrayBuffer in the transfer list was detached while the value was cloned.";
            return false;
        }
    }

    std::pair<uint8_t*, size_t> data = serializer.Release();
    v8::ValueDeserializer deserializer(isolate, data.first, data.second);
    for (uint32_t i = 0; i < (uint32_t)transfers.size(); i++) {
        std::shared_ptr<v8::BackingStore> store = transfers[i]->GetBackingStore();
        transfers[i]->Detach(v8::Local<v8::Value>()).Check();
        deserializer.TransferArrayBuffer(i, v8::ArrayBuffer::New(isolate, store));
    }
    const bool ok = deserializer.ReadHeader(context).FromMaybe(false) && deserializer.ReadValue(context).ToLocal(&copy);
    delegate.FreeBufferMemory(data.first);
    return ok;
}
