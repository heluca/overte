//
//  ScriptManagerEntityScriptStatus.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptManager.h"

bool ScriptManager::getEntityScriptStatus(const EntityItemID& entityID, const QString& scriptURL, EntityScriptStatus& status,
                                          QString& errorInfo) const {
    QReadLocker locker { &_entityScriptsLock };
    auto it = _entityScripts.constFind(entityID);
    if (it == _entityScripts.constEnd()) {
        return false;
    }
    auto it2 = it.value().constFind(scriptURL);
    if (it2 == it.value().constEnd()) {
        return false;
    }
    status = it2.value().status;
    errorInfo = it2.value().errorInfo;
    return true;
}
