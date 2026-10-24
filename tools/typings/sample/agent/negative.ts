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

// @ts-expect-error startAudioSink takes the URL as a string.
Agent.startAudioSink({ url: "ws://127.0.0.1:9000/ears" });

// @ts-expect-error framesPerMessage is a number of frames.
Agent.startAudioSink("ws://127.0.0.1:9000/ears", { framesPerMessage: "2" });

// @ts-expect-error isAudioSinkActive is read-only.
Agent.isAudioSinkActive = true;
