//
//  AutoExposure.h
//  libraries/render-utils/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_render_utils_AutoExposure_h
#define hifi_render_utils_AutoExposure_h

#include <chrono>

#include <render/Engine.h>
#include <gpu/Framebuffer.h>

#include "LightingModel.h"

// What the tone mapping pass needs from auto exposure: the 1x1 texture holding the adapted average log2 luminance of
// the lighting buffer (measured before any exposure is applied), and the limits the viewer put on the adjustment.
struct AutoExposureResult {
    gpu::TexturePointer adaptation;
    float keyLog2 { 0.0f };
    float rangeUp { 0.0f };
    float rangeDown { 0.0f };
    bool debug { false };
};
using AutoExposureResultPointer = std::shared_ptr<AutoExposureResult>;

/*@jsdoc
 * Auto exposure settings: the tone mapping exposure of the current zone is adjusted, within a range, so that the
 * average luminance of what is on screen tends towards a key value, the way the eye adapts.
 * @typedef {object} Render.AutoExposureConfig
 * @property {number} key=0.125 - The average scene luminance, after the zone's exposure, at which no adjustment is made.
 *     Calibrated so that a sunlit outdoor view keeps the look its zone's exposure gives it.
 * @property {number} rangeUp=1.0 - How many EV the exposure may be raised above the zone's value, for dark views.
 * @property {number} rangeDown=1.0 - How many EV the exposure may be lowered below the zone's value, for bright views.
 * @property {number} speedUp=3.0 - Adaptation rate per second when the view gets brighter.
 * @property {number} speedDown=1.0 - Adaptation rate per second when the view gets darker.
 * @property {boolean} debug=false - Paint the adapted log2 luminance as a grey patch in a corner of the view.
 */
class AutoExposureConfig : public render::Job::Config {
    Q_OBJECT
    Q_PROPERTY(float key MEMBER key WRITE setKey NOTIFY dirty)
    Q_PROPERTY(float rangeUp MEMBER rangeUp WRITE setRangeUp NOTIFY dirty)
    Q_PROPERTY(float rangeDown MEMBER rangeDown WRITE setRangeDown NOTIFY dirty)
    Q_PROPERTY(float speedUp MEMBER speedUp WRITE setSpeedUp NOTIFY dirty)
    Q_PROPERTY(float speedDown MEMBER speedDown WRITE setSpeedDown NOTIFY dirty)
    Q_PROPERTY(bool debug MEMBER debug WRITE setDebug NOTIFY dirty)

public:
    // The job stays enabled and follows the lighting model's switch instead: a disabled job would leave its last
    // output in place for the tone mapping pass.
    AutoExposureConfig() : render::Job::Config(true) {}

    void setKey(float value) { key = value; emit dirty(); }
    void setRangeUp(float value) { rangeUp = value; emit dirty(); }
    void setRangeDown(float value) { rangeDown = value; emit dirty(); }
    void setSpeedUp(float value) { speedUp = value; emit dirty(); }
    void setSpeedDown(float value) { speedDown = value; emit dirty(); }
    void setDebug(bool value) { debug = value; emit dirty(); }

    float key { 0.125f };
    float rangeUp { 1.0f };
    float rangeDown { 1.0f };
    float speedUp { 3.0f };
    float speedDown { 1.0f };
    bool debug { false };

signals:
    void dirty();
};

// Measures the lighting buffer every frame, on the GPU and without any readback: a small log2 luminance texture of the
// viewport, folded into one value that follows the measurement at the eye's pace. Output is null when auto exposure is
// off (lighting model) or inside a mirror, and the tone mapping pass then uses the zone's exposure alone.
class AutoExposure {
public:
    using Inputs = render::VaryingSet2<gpu::FramebufferPointer, LightingModelPointer>;
    using Outputs = AutoExposureResultPointer;
    using Config = AutoExposureConfig;
    using JobModel = render::Job::ModelIO<AutoExposure, Inputs, Outputs, Config>;

    AutoExposure();

    void configure(const Config& config);
    void run(const render::RenderContextPointer& renderContext, const Inputs& inputs, Outputs& outputs);

private:

#include "AutoExposure.shared.slh"

    static const int LUMINANCE_SIZE;
    static gpu::PipelinePointer _luminancePipeline;
    static gpu::PipelinePointer _adaptPipeline;

    gpu::FramebufferPointer _luminanceFramebuffer;
    // Two 1x1 framebuffers: one frame reads the previous adapted value from one and writes the next into the other
    gpu::FramebufferPointer _adaptation[2];
    int _current { 0 };
    bool _hasHistory { false };
    std::chrono::steady_clock::time_point _lastRun;

    gpu::StructBuffer<Parameters> _parameters;
    AutoExposureResultPointer _result;
};

#endif // hifi_render_utils_AutoExposure_h
