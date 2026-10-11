//
//  overte-server-entity-extras.d.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A stop-gap for two declarations the generated server entity typings lack (heluca/overte#40).
//  The real fix is a remoteCallerID property in the Script JSDoc in
//  libraries/script-engine/src/ScriptManagerScriptingInterface.h, where it is today only a
//  @param of callEntityScriptMethod, and an ImportMeta declaration from tools/typings/generate.js.
//  Delete this file once the generated declarations carry both.
//

declare namespace Script {
    /** The caller's session ID ("{uuid}") while a remotely callable method runs synchronously. */
    let remoteCallerID: string | undefined;
}

// The engine's module loader sets import.meta.url to the module's URL.
interface ImportMeta {
    url: string;
}
