//
//  StrokeThrottle.cpp
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "StrokeThrottle.h"

#include <algorithm>
#include <numeric>

namespace {

// Tuning. Effort: the distance of the last few strokes against the mean of the recent strokes, both counts
// including the stroke itself. The console counts distance in steps nearly as long as a stroke, so one stroke's own
// distance is mostly 0 or 1 step; a few strokes together measure it.
const size_t EFFORT_WINDOW_STROKES = 3;
const size_t MEAN_WINDOW_STROKES = 8;
const float MIN_EFFORT = 0.6f;
const float MAX_EFFORT = 1.4f;

// Rate: the stroke rate maps linearly onto a factor between these two points and is clamped to them.
const float LOW_RATE_SPM = 18.0f;
const float LOW_RATE_FACTOR = 0.6f;
const float HIGH_RATE_SPM = 30.0f;
const float HIGH_RATE_FACTOR = 1.0f;

// A pulse peaks at rate factor x effort, clamped to this range.
const float MIN_PEAK = 0.3f;
const float MAX_PEAK = 1.0f;

// Shape: the drive takes this part of the stroke interval, up to a limit; the recovery eases down to this part of
// the peak by the time the next stroke is due, and holds there.
const double DRIVE_FRACTION = 1.0 / 3.0;
const double MAX_DRIVE_SECONDS = 0.8;
const float RECOVERY_FLOOR = 0.5f;

// Stopping: with no stroke for this many intervals, bounded, the throttle is 0. The interval is the longer of the
// last two, so one stroke registered early does not stop a steady rower.
const double STOP_INTERVALS = 1.5;
const double MIN_STOP_SECONDS = 2.0;
const double MAX_STOP_SECONDS = 5.0;

// The first stroke of a spell has no interval before it to measure, so it is taken to come at this one (24 spm).
const double FIRST_STROKE_INTERVAL = 2.5;

// Protocol. Distance is 15 bits; a stroke count that jumps by more than this in one reading is a reset or noise.
const uint16_t DISTANCE_MASK = 0x7FFF;
const uint16_t MAX_STROKES_PER_READING = 4;

float smoothstep(double t) {
    float x = (float)std::clamp(t, 0.0, 1.0);
    return x * x * (3.0f - 2.0f * x);
}

float computeRateFactor(double interval) {
    float spm = (float)(60.0 / interval);
    float t = (spm - LOW_RATE_SPM) / (HIGH_RATE_SPM - LOW_RATE_SPM);
    return std::clamp(LOW_RATE_FACTOR + t * (HIGH_RATE_FACTOR - LOW_RATE_FACTOR), LOW_RATE_FACTOR, HIGH_RATE_FACTOR);
}

} // namespace

void StrokeThrottle::addReading(double now, uint16_t distance, uint16_t strokes) {
    distance &= DISTANCE_MASK;
    if (!_hasBaseline) {
        setBaseline(distance, strokes);
        return;
    }
    uint16_t newStrokes = (uint16_t)(strokes - _strokes);
    if (newStrokes == 0) {
        return;
    }
    if (newStrokes > MAX_STROKES_PER_READING) {
        setBaseline(distance, strokes);
        _isPulsing = false;
        return;
    }

    float strokeDistance = (float)((uint16_t)(distance - _distanceAtStroke) & DISTANCE_MASK) / newStrokes;
    double interval;
    float effort;
    if (isRowing(now)) {
        interval = (now - _lastStrokeTime) / newStrokes;
        effort = computeEffort(strokeDistance);
    } else {
        // The distance since the last spell includes the flywheel running down, so it says nothing about this stroke
        interval = FIRST_STROKE_INTERVAL;
        effort = 1.0f;
        _lastInterval = 0.0;
    }

    float peak = std::clamp(computeRateFactor(interval) * effort, MIN_PEAK, MAX_PEAK);
    _pulseFrom = getThrottle(now);
    _pulseStart = now;
    _peak = peak;
    _driveSeconds = std::min(DRIVE_FRACTION * interval, MAX_DRIVE_SECONDS);
    _recoverySeconds = interval - _driveSeconds;
    _stopSeconds = std::clamp(STOP_INTERVALS * std::max(interval, _lastInterval), MIN_STOP_SECONDS, MAX_STOP_SECONDS);
    _isPulsing = true;
    _lastInterval = interval;

    _strokes = strokes;
    _distanceAtStroke = distance;
    _lastStrokeTime = now;
}

void StrokeThrottle::reset() {
    _hasBaseline = false;
    _isPulsing = false;
}

float StrokeThrottle::getThrottle(double now) const {
    if (!isRowing(now)) {
        return 0.0f;
    }
    double t = std::max(now - _pulseStart, 0.0);
    if (t < _driveSeconds) {
        return _pulseFrom + (_peak - _pulseFrom) * smoothstep(t / _driveSeconds);
    }
    float floor = _peak * RECOVERY_FLOOR;
    return _peak + (floor - _peak) * smoothstep((t - _driveSeconds) / _recoverySeconds);
}

void StrokeThrottle::setBaseline(uint16_t distance, uint16_t strokes) {
    _hasBaseline = true;
    _strokes = strokes;
    _distanceAtStroke = distance;
}

bool StrokeThrottle::isRowing(double now) const {
    return _isPulsing && now - _pulseStart <= _stopSeconds;
}

float StrokeThrottle::computeEffort(float strokeDistance) {
    _strokeDistances.push_back(strokeDistance);
    while (_strokeDistances.size() > MEAN_WINDOW_STROKES) {
        _strokeDistances.pop_front();
    }
    size_t numRecent = std::min(EFFORT_WINDOW_STROKES, _strokeDistances.size());
    float recent = std::accumulate(_strokeDistances.end() - numRecent, _strokeDistances.end(), 0.0f) / numRecent;
    float mean = std::accumulate(_strokeDistances.begin(), _strokeDistances.end(), 0.0f) / _strokeDistances.size();
    if (mean <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(recent / mean, MIN_EFFORT, MAX_EFFORT);
}
