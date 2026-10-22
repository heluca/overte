//
//  salon-grand-manifest.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

// The entries of phantasos props/salon-grand/manifest.json (1.0.0) that the script reads.
export const MANIFEST = {
    overte: { joint_count: 109 },
    joints: {
        salon_grand: { index: 0, kind: "static" },
        fallboard: { index: 9, kind: "rotation", axis: [1.0, 0.0, 0.0], travel: 90.0, unit: "deg" },
        lid: { index: 100, kind: "rotation", axis: [0.0, 0.0, 1.0], travel: 40.0, unit: "deg" },
        lid_prop: { index: 101, kind: "rotation", axis: [-0.734583, -0.616388, 0.283643], travel: 103.3876, unit: "deg" }
    }
};
