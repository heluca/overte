//
//  ScriptPromiseV8.h
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

#ifndef hifi_ScriptPromiseV8_h
#define hifi_ScriptPromiseV8_h

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QtCore/QMutex>

#include "v8.h"

#include "../ScriptEngine.h"

class ScriptEngineV8;

/// [V8] Applies the settlements of one engine's native promises on its script thread
///
/// The engine owns the bridge and closes it as it is destroyed; resolvers hold it only weakly. Once closed it holds no
/// V8 handles, so it is safe for the last reference to go away on any thread.
class ScriptPromiseBridgeV8 : public std::enable_shared_from_this<ScriptPromiseBridgeV8> {
public:
    using MakeValue = std::function<v8::Local<v8::Value>(ScriptEngineV8*)>;
    enum class Outcome { Fulfil, Reject, Forget };
    struct Settlement {
        quint64 id;
        Outcome outcome;
        // Called on the script thread inside a handle scope; empty for Outcome::Forget
        MakeValue makeValue;
    };

    explicit ScriptPromiseBridgeV8(ScriptEngineV8* engine) : _engine(engine) {}

    ScriptPromise newPromise();

    // Applies the settlement now on the script thread, otherwise queues it there. Does nothing once closed.
    void submit(Settlement settlement);

    void close();

    // Script thread only
    size_t resolverCount() const { return _resolvers.size(); }

private:
    void drain();
    void apply(ScriptEngineV8* engine, const Settlement& settlement);

    QMutex _mutex;
    ScriptEngineV8* _engine;                // guarded by _mutex, null once closed
    std::vector<Settlement> _queued;        // guarded by _mutex

    // Script thread only, or with the isolate held while the engine is destroyed
    std::unordered_map<quint64, v8::Global<v8::Promise::Resolver>> _resolvers;
    quint64 _nextId { 1 };
};

/// [V8] Implements ScriptPromiseResolver on top of ScriptPromiseBridgeV8
class ScriptPromiseResolverV8 final : public ScriptPromiseResolver {
public:
    ScriptPromiseResolverV8(std::weak_ptr<ScriptPromiseBridgeV8> bridge, quint64 id) : _bridge(std::move(bridge)), _id(id) {}
    ~ScriptPromiseResolverV8() override;

    void resolve(const ScriptValue& value) override;
    void resolve(const QVariant& value) override;
    void reject(const ScriptValue& reason) override;
    void reject(const QString& message) override;

private:
    void settle(ScriptPromiseBridgeV8::Outcome outcome, ScriptPromiseBridgeV8::MakeValue makeValue);

    std::weak_ptr<ScriptPromiseBridgeV8> _bridge;
    const quint64 _id;
    std::atomic<bool> _settled { false };
};

#endif  // hifi_ScriptPromiseV8_h

/// @}
