//
//  positive.ts
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Agent idioms the assignment client declarations must accept. A separate project from
//  sample/, because each context declares the same globals.
//

/// <reference path="../../dist/overte-assignment-client.d.ts" />

Agent.isAvatar = true;
Agent.isListeningToAudioStream = true;

// Offer only PCM to the audio mixer; read back the validated list.
Agent.audioCodecs = ["pcm"];
const offered: string[] = Agent.audioCodecs;
print("Offering " + offered.join(", "));

// Mouth and ears at head height, in the avatar's frame.
Agent.audioPositionOffset = { x: 0, y: 0.6, z: 0 };
const offset: Vec3 = Agent.audioPositionOffset;
print("Audio offset " + JSON.stringify(offset));
