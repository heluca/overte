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

// Web globals.
const dataUrl = new URL("./data.json", Script.resolvePath(""));
print(dataUrl.href, dataUrl.searchParams.get("v") ?? "", String(URL.canParse("x", dataUrl)));
const encoded: Uint8Array = new TextEncoder().encode("h\u00e9llo");
print(new TextDecoder("utf-8", { fatal: true }).decode(encoded, { stream: false }), crypto.randomUUID());
const copied = structuredClone({ list: [1, 2] }, { transfer: [] }) as { list: number[] };
print(String(copied.list.length), btoa(atob("aGk=")), String(performance.now() - performance.timeOrigin));
crypto.getRandomValues(new Uint32Array(4));
crypto.subtle.digest({ name: "SHA-256" }, encoded).then((hash: ArrayBuffer) => print(String(hash.byteLength)));
async function hashText(text: string): Promise<number> {
    return (await crypto.subtle.digest("SHA-1", new TextEncoder().encode(text))).byteLength;
}
hashText("abc").catch((error: DOMException) => print(error.name, String(error.code)));

// Assets without a callback returns a promise; with one, the callback gets the result.
async function readSaved(): Promise<string> {
    const saved = await Assets.getAsset({ url: "/games/saved.json", responseType: "text" });
    const hash: string = await Assets.getMapping("/games/saved.json");
    return String(saved.response) + hash;
}
readSaved().then(print, (error: Error) => print(error.message));
Assets.getAsset("/games/saved.json", (error, result) => print(error === null ? String(result.response) : error));

// Script.remoteCallerID is a Uuid string while a remotely callable method runs, undefined otherwise.
function remotelyCallable(): void {
    const caller: string | undefined = Script.remoteCallerID;
    if (caller !== undefined) {
        print(caller);
    }
}
