//
//  negative.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Misuse the assignment client declarations must reject; see ../negative.ts.
//

/// <reference path="../../dist/overte-assignment-client.d.ts" />

// @ts-expect-error audioCodecs is a list of codec names, not one name.
Agent.audioCodecs = "pcm";

// @ts-expect-error audioPositionOffset is a Vec3, not a height.
Agent.audioPositionOffset = 0.6;
