//
//  ScriptCompletion.h
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_ScriptCompletion_h
#define overte_ScriptCompletion_h

#include <memory>

#include <QString>
#include <QVariantMap>

#include "ScriptEngine.h"
#include "ScriptValue.h"

/**
 * @brief Completes an asynchronous native call either through the script's callback or through a promise
 *
 * A native method that took a callback can also return a Promise: when the script passes neither a scope nor a callback
 * (both absent: omitted, <code>undefined</code> or <code>null</code>), the completion is in promise form and
 * takeReturnValue() is the promise; otherwise it is in callback form, the callback found as makeScopedHandlerObject()
 * finds it, and takeReturnValue() is <code>undefined</code>. When the scope or callback given does not name a function,
 * takeReturnValue() is a promise rejected with a <code>TypeError</code>, verify() is false and nothing else settles it;
 * a method that still requires a callback checks hasCallback() and throws instead, and never takes that promise.
 *
 * Copies share one completion. Make it, complete it and take its return value on the script thread. In promise form the
 * completion keeps neither the promise nor the engine alive once the return value is taken, so copies held by
 * handlers that never run, or are destroyed off the script thread, are harmless; a completion after the script stopped
 * settles nothing.
 */
class ScriptCompletion {
public:
    ScriptCompletion(const ScriptEnginePointer& engine, const ScriptValue& scopeOrCallback,
                     const ScriptValue& methodOrName = ScriptValue());

    // Omitted, undefined or null
    static bool isAbsent(const ScriptValue& value) { return !value.isValid(); }

    bool isPromise() const;
    // Callback form with a callback to call
    bool hasCallback() const;

    /**
     * @brief Checks an argument. When the condition fails, promise form rejects with a <code>TypeError</code> carrying
     * the message, and callback form throws an <code>Error</code> carrying it, as the callback APIs always have.
     * @return Whether to go on: false also when the callback was not a function.
     */
    bool verify(bool condition, const QString& message) const;

    /**
     * @brief The value the native method returns: the promise, or <code>undefined</code> in callback form. Call it once,
     * after setting up the handlers that hold copies of the completion.
     */
    ScriptValue takeReturnValue() const;

    /**
     * @brief Completes an error-first call: the callback gets <code>(error or null, result)</code>; the promise rejects
     * with an <code>Error</code> carrying the message and the result's own properties (but not <code>message</code>,
     * <code>name</code> or <code>stack</code>) when the error is not empty, otherwise resolves with the result.
     */
    void complete(const QString& error, const ScriptValue& result) const;
    // The result converted with ScriptEngine::toScriptValue(), as the callback APIs always have
    void complete(const QString& error, const QVariantMap& result) const;

    // For other callback shapes: the callback gets the arguments, or the promise resolves with the value
    void resolve(const ScriptValue& value, const ScriptValueList& callbackArguments) const;
    // For other callback shapes: the callback gets the arguments, or the promise rejects with an Error carrying the message
    void reject(const QString& message, const ScriptValueList& callbackArguments) const;

    // The engine while it lives, for making result values; null once it is gone
    ScriptEnginePointer engine() const;

private:
    struct State;
    void call(const ScriptValueList& arguments) const;
    void settle(bool fulfilled, const ScriptValue& value) const;

    std::shared_ptr<State> _state;
};

#endif // overte_ScriptCompletion_h

/// @}
