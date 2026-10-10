//
//  negative.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Misuse the Interface declarations must reject; see ../negative.ts.
//

/// <reference path="../../dist/overte-interface.d.ts" />

Pointers.createPointer(PickType.Ray, {
    // @ts-expect-error A render state's entity needs a type when the pointer is created.
    renderStates: [{ name: "example", end: { dimensions: Vec3.ONE } }]
});
