//
//  negative.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Misuse the declarations must reject. Each @ts-expect-error fails the check if its line
//  type-checks, so a declaration that regresses to `any` or `unknown` is caught.
//

/// <reference path="../dist/overte-server-entity.d.ts" />

// @ts-expect-error "Modle" is not an Entities.EntityType.
Entities.addEntity({ type: "Modle" });

// @ts-expect-error "positon" is not an Entities.EntityProperties key.
Entities.addEntity({ type: "Box", positon: Vec3.ZERO });

// @ts-expect-error MyAvatar is not available to server entity scripts.
print(MyAvatar.position);

// @ts-expect-error messageReceived passes the channel as a string.
Messages.messageReceived.connect((channel: number) => print(channel));

// @ts-expect-error Timer handles are numbers, not objects.
Script.clearTimeout({});

// @ts-expect-error Vec3 needs all three components.
Vec3.distance({ x: 1, y: 2 }, Vec3.ZERO);
