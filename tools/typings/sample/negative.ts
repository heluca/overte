//
//  negative.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Misuse the declarations must reject. Each line below is marked as an expected error, which
//  fails the check if the line type-checks, so a declaration that regresses to `any` or
//  `unknown` is caught.
//

/// <reference path="../dist/overte-server-entity.d.ts" />

// @ts-expect-error "Modle" is not an Entities.EntityType.
Entities.addEntity({ type: "Modle" });

// @ts-expect-error "positon" is not an Entities.EntityProperties key.
Entities.addEntity({ type: "Box", positon: Vec3.ZERO });

// @ts-expect-error addEntity needs a type to pick the entity's properties.
Entities.addEntity({});

// @ts-expect-error modelURL is a Model property; a Box takes Shape's properties.
Entities.addEntity({ type: "Box", modelURL: "x" });

// @ts-expect-error "Nope" is not an Entities.EntityType.
Entities.addEntity({ type: "Nope" });

// @ts-expect-error MyAvatar is not available to server entity scripts.
print(MyAvatar.position);

// @ts-expect-error messageReceived passes the channel as a string.
Messages.messageReceived.connect((channel: number) => print(channel));

// @ts-expect-error Timer handles are numbers, not objects.
Script.clearTimeout({});

// @ts-expect-error Vec3 needs all three components.
Vec3.distance({ x: 1, y: 2 }, Vec3.ZERO);

// @ts-expect-error A URL needs an input.
new URL();

// @ts-expect-error encode returns a Uint8Array, not a string.
const notText: string = new TextEncoder().encode("x");

// @ts-expect-error randomUUID returns a string.
const notNumber: number = crypto.randomUUID();

// @ts-expect-error structuredClone needs a value.
structuredClone();

// @ts-expect-error digest takes bytes; encode a string with TextEncoder first.
crypto.subtle.digest("SHA-256", "x");

// @ts-expect-error canParse is static, not a member of a URL.
new URL("http://example.com/").canParse("x");

// @ts-expect-error Given a callback, getAsset returns nothing to await on.
Assets.getAsset("/games/saved.json", () => {}).then(print);

// @ts-expect-error Script.remoteCallerID is read-only; only the engine sets it.
Script.remoteCallerID = "caller-id";

// @ts-expect-error playStream takes the stream's URL as a string.
Audio.playStream(new URL("http://127.0.0.1:8000/pcm"));

// @ts-expect-error stereo is a boolean, not a channel count.
Audio.playStream("http://127.0.0.1:8000/pcm", { stereo: 2 });
