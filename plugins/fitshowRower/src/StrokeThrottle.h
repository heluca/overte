//
//  StrokeThrottle.h
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef hifi_StrokeThrottle_h
#define hifi_StrokeThrottle_h

#include <cstdint>
#include <deque>

// Turns a rowing console's cumulative stroke and distance counters into a forward throttle, 0 to 1.
// Each stroke is a pulse: the throttle rises to a peak during the drive and eases off through the recovery, so steady
// rowing holds it above 0, and it drops to 0 when the strokes stop. The peak follows the stroke rate and the distance
// the last few strokes gained against the recent ones.
// Times are in seconds from any monotonic origin.
class StrokeThrottle {
public:
    // A reading of the console's counters. The first one after a reset is only a baseline.
    void addReading(double now, uint16_t distance, uint16_t strokes);

    // The counters are no longer being read: the throttle is 0 until a new baseline and a stroke after it.
    void reset();

    float getThrottle(double now) const;

private:
    void setBaseline(uint16_t distance, uint16_t strokes);
    bool isRowing(double now) const;
    float computeEffort(float strokeDistance);

    bool _hasBaseline { false };
    uint16_t _strokes { 0 };
    uint16_t _distanceAtStroke { 0 };
    double _lastStrokeTime { 0.0 };
    double _lastInterval { 0.0 };
    std::deque<float> _strokeDistances;

    bool _isPulsing { false };
    double _pulseStart { 0.0 };
    float _pulseFrom { 0.0f };
    float _peak { 0.0f };
    double _driveSeconds { 0.0 };
    double _recoverySeconds { 0.0 };
    double _stopSeconds { 0.0 };
};

#endif // hifi_StrokeThrottle_h
