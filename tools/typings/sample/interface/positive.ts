//
//  positive.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Engine API idioms the Interface declarations must accept. A separate project from
//  sample/, because each context declares the same globals.
//

/// <reference path="../../dist/overte-interface.d.ts" />

const rayPointer = Pointers.createPointer(PickType.Ray, {
    renderStates: [{ name: "example", path: { type: "PolyLine" }, end: { type: "Sphere", dimensions: Vec3.ONE } }]
});

// The Pointers.editRenderState JSDoc example: an edit of the nested entity properties needs no type.
Pointers.editRenderState(rayPointer, "example", {
    end: { dimensions: { x: 0.5, y: 0.5, z: 0.5 } }
});
