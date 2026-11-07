//
//  EntityScriptClientAccess.h
//  libraries/entities/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_EntityScriptClientAccess_h
#define overte_EntityScriptClientAccess_h

#include <QtCore/QSharedPointer>
#include <QtCore/QString>

class EntityScriptClient;
class ScriptEngine;

// The registered EntityScriptClient. Without one (an entity script server, or any process that does not create one) it
// raises "Entities.<function> is not available in this script context" in the engine, or logs it when there is no engine,
// and returns null.
QSharedPointer<EntityScriptClient> entityScriptClientOrRaise(ScriptEngine* engine, const QString& function);

#endif // overte_EntityScriptClientAccess_h
