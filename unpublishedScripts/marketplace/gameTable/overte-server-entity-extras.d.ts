//
//  overte-server-entity-extras.d.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  What the generated server entity declarations miss: the engine sets Script.remoteCallerID
//  while a remotely callable method runs, and the JSDoc documents it only as a parameter.
//

declare namespace Script {
    /** The session ID of the caller of the running remotely callable method, as "{uuid}". */
    let remoteCallerID: string;
}

// The engine's module loader sets import.meta.url to the module's URL.
interface ImportMeta {
    url: string;
}
