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

// Ears: send the agent's mix to a WebSocket and reconnect when it ends, unless stopped.
function startEars(): void {
    const started: boolean = Agent.startAudioSink("ws://127.0.0.1:9000/ears", { framesPerMessage: 2 });
    print("Audio sink " + (started ? "connecting" : "refused"));
}
Agent.audioSinkFinished.connect(function (reason: string) {
    if (reason !== "stopped") {
        Script.setTimeout(startEars, 1000);
    }
});
startEars();
Agent.startAudioSink("wss://127.0.0.1:9443/ears");
const sinkActive: boolean = Agent.isAudioSinkActive;
if (sinkActive) {
    Agent.stopAudioSink();
}
