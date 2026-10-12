//
//  ScriptCompletion.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptCompletion.h"

#include "ScriptContext.h"
#include "ScriptEngineCast.h"
#include "ScriptValueIterator.h"
#include "Scriptable.h"

struct ScriptCompletion::State {
    std::weak_ptr<ScriptEngine> engine;
    // Callback form: the engine lives as long as the handler, as it did before promise forms
    ScriptEnginePointer callbackEngine;
    ScriptValue handler;
    // Promise form, or a callback that is not a function
    ScriptPromiseResolverPointer resolver;
    ScriptValue promise;
    bool badCallback { false };
};

ScriptCompletion::ScriptCompletion(const ScriptEnginePointer& engine, const ScriptValue& scopeOrCallback,
                                   const ScriptValue& methodOrName) :
    _state(std::make_shared<State>()) {
    _state->engine = engine;
    if (!isAbsent(scopeOrCallback) || !isAbsent(methodOrName)) {
        ScriptValue handler = makeScopedHandlerObject(scopeOrCallback, methodOrName);
        if (handler.isObject() && handler.property("callback").isFunction()) {
            _state->callbackEngine = engine;
            _state->handler = handler;
            return;
        }
        _state->badCallback = true;
    }
    ScriptPromise promise = engine->newPromise();
    _state->resolver = promise.resolver;
    _state->promise = promise.promise;
}

bool ScriptCompletion::isPromise() const {
    return _state->resolver != nullptr;
}

bool ScriptCompletion::hasCallback() const {
    return _state->callbackEngine != nullptr;
}

bool ScriptCompletion::verify(bool condition, const QString& message) const {
    if (_state->badCallback) {
        return false;
    }
    if (condition) {
        return true;
    }
    if (isPromise()) {
        auto engine = _state->engine.lock();
        _state->resolver->reject(engine->makeError(engine->newValue(message), "TypeError"));
    } else if (Scriptable::context()) {
        Scriptable::context()->throwError(message);
    }
    return false;
}

ScriptValue ScriptCompletion::takeReturnValue() const {
    if (_state->badCallback) {
        // Rejected only here, so that a method still requiring a callback can throw instead without a second report
        auto engine = _state->engine.lock();
        _state->resolver->reject(engine->makeError(engine->newValue("callback is not a function"), "TypeError"));
    }
    if (isPromise()) {
        ScriptValue promise = _state->promise;
        _state->promise = ScriptValue();
        return promise;
    }
    return _state->callbackEngine->undefinedValue();
}

ScriptEnginePointer ScriptCompletion::engine() const {
    return _state->engine.lock();
}

void ScriptCompletion::complete(const QString& error, const ScriptValue& result) const {
    auto engine = _state->engine.lock();
    if (!engine) {
        return;
    }
    if (!isPromise()) {
        call({ error.isEmpty() ? engine->nullValue() : engine->newValue(error), result });
    } else if (error.isEmpty()) {
        settle(true, result);
    } else {
        ScriptValue reason = engine->makeError(engine->newValue(error));
        if (result.isObject()) {
            auto properties = result.newIterator();
            while (properties->hasNext()) {
                properties->next();
                QString name = properties->name();
                if (name != "message" && name != "name" && name != "stack") {
                    reason.setProperty(name, properties->value());
                }
            }
        }
        settle(false, reason);
    }
}

void ScriptCompletion::complete(const QString& error, const QVariantMap& result) const {
    if (auto engine = _state->engine.lock()) {
        complete(error, engine->toScriptValue(result));
    }
}

void ScriptCompletion::resolve(const ScriptValue& value, const ScriptValueList& callbackArguments) const {
    if (isPromise()) {
        settle(true, value);
    } else {
        call(callbackArguments);
    }
}

void ScriptCompletion::reject(const QString& message, const ScriptValueList& callbackArguments) const {
    auto engine = _state->engine.lock();
    if (!engine) {
        return;
    }
    if (isPromise()) {
        settle(false, engine->makeError(engine->newValue(message)));
    } else {
        call(callbackArguments);
    }
}

void ScriptCompletion::call(const ScriptValueList& arguments) const {
    const ScriptValue& handler = _state->handler;
    ScriptContextGuard scriptContextGuard(_state->callbackEngine->currentContext());
    handler.property("callback").call(handler.property("scope"), arguments);
}

void ScriptCompletion::settle(bool fulfilled, const ScriptValue& value) const {
    if (_state->badCallback) {
        return;
    }
    if (fulfilled) {
        _state->resolver->resolve(value);
    } else {
        _state->resolver->reject(value);
    }
}
