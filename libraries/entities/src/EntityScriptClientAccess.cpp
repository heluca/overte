//
//  EntityScriptClientAccess.cpp
//  libraries/entities/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "EntityScriptClientAccess.h"

#include <DependencyManager.h>
#include <EntityScriptClient.h>
#include <ScriptEngine.h>

#include "EntitiesLogging.h"

QSharedPointer<EntityScriptClient> entityScriptClientOrRaise(ScriptEngine* engine, const QString& function) {
    auto client = DependencyManager::get<EntityScriptClient>();
    if (!client) {
        QString message = "Entities." + function + " is not available in this script context";
        if (engine) {
            engine->raiseException(message, "Error");
        } else {
            qCWarning(entities) << message;
        }
    }
    return client;
}
