//
//  HelperScriptEngine.h
//  libraries/script-engine/src/HelperScriptEngine.h
//
//  Created by dr Karol Suprynowicz on 2024/04/28.
//  Copyright 2024 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef overte_HelperScriptEngine_h
#define overte_HelperScriptEngine_h

#include <mutex>
#include "QThread"

#include "ScriptEngine.h"

/**
 * @brief Provides a wrapper around script engine that does not have ScriptManager
 *
 * HelperScriptEngine is used for performing smaller tasks, like for example conversions between entity
 * properties and JSON data.
 * For thread safety all accesses to helper script engine need to be done either through HelperScriptEngine::run()
 * or HelperScriptEngine::runWithResult().
 *
 */


class HelperScriptEngine {
public:
    HelperScriptEngine();
    ~HelperScriptEngine();

    // A released ScriptValue only queues its V8 wrapper; the ScriptManager loop is
    // what drains the queue, and a helper engine has no manager, so drain after
    // every run or the wrappers and the objects they pin accumulate for the life of
    // the engine (the entity server persists the whole tree through one of these
    // twice a cycle: overte-org/overte#2364).
    template <typename F>
    inline void run(F&& f) {
        std::lock_guard<std::mutex> guard(_scriptEngineLock);
        auto scopeGuard = _scriptEngine->getScopeGuard();
        f();
        _scriptEngine->perManagerLoopIterationCleanup();
    }

    template <typename T, typename F>
    inline T runWithResult(F&& f) {
        T result;
        {
            std::lock_guard<std::mutex> guard(_scriptEngineLock);
            auto scopeGuard = _scriptEngine->getScopeGuard();
            result = f();
            _scriptEngine->perManagerLoopIterationCleanup();
        }
        return result;
    }

    /**
     * @brief Returns pointer to the script engine
     *
     * This function should be used only inside HelperScriptEngine::run() or HelperScriptEngine::runWithResult()
     */
    ScriptEngine* get() { return _scriptEngine.get(); };
    ScriptEnginePointer getShared() { return _scriptEngine; };
private:
    std::mutex _scriptEngineLock;
    ScriptEnginePointer _scriptEngine { nullptr };
    std::shared_ptr<QThread> _scriptEngineThread { nullptr };
};

#endif  //overte_HelperScriptEngine_h
