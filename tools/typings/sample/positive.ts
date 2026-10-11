//
//  positive.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Engine API idioms the server entity declarations must accept.
//

/// <reference path="../dist/overte-server-entity.d.ts" />

// Cloning: without desiredProperties, getEntityProperties returns every property, type included.
const cloneSource = Entities.addEntity({ type: "Box", color: { red: 255, green: 0, blue: 0 } });
Entities.addEntity(Entities.getEntityProperties(cloneSource));
Entities.getMultipleEntityProperties([cloneSource]).forEach(properties => Entities.addEntity(properties));

// Narrowing a read on type.
const readBack = Entities.getEntityProperties(cloneSource, ["type", "shape"]);
if (readBack.type === "Box") {
    print(String(readBack.shape));
}

// Edits need no type.
Entities.editEntity(cloneSource, { alpha: 0.5 });

// Script.remoteCallerID is a Uuid string while a remotely callable method runs, undefined otherwise.
function remotelyCallable(): void {
    const caller: string | undefined = Script.remoteCallerID;
    if (caller !== undefined) {
        print(caller);
    }
}
