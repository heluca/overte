//
//  MixedAudioStream.h
//  libraries/audio/src
//
//  Created by Yixin Wang on 8/4/14.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MixedAudioStream_h
#define hifi_MixedAudioStream_h

#include "InboundAudioStream.h"

class MixedAudioStream : public InboundAudioStream {
public:
    MixedAudioStream(int numFramesCapacity, int numStaticJitterFrames = -1);

    float getNextOutputFrameLoudness() const { return _ringBuffer.getNextOutputFrameLoudness(); }

    // Reads up to maxSamples of what is in the ring, without popSamples' jitter buffer and starve bookkeeping, for a
    // reader that takes every frame as it arrives. Returns the number of samples read.
    int readAvailableSamples(int16_t* destination, int maxSamples);
};

#endif // hifi_MixedAudioStream_h
