//
//  FitShowRowerPlugin.h
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef hifi_FitShowRowerPlugin_h
#define hifi_FitShowRowerPlugin_h

#include <memory>
#include <mutex>

#include <controllers/InputDevice.h>
#include <plugins/InputPlugin.h>

#include "StrokeThrottle.h"

class FitShowLink;
class QThread;

// A rowing machine whose console speaks the FitShow Bluetooth LE protocol, as one Throttle axis: each stroke pushes it
// up and it falls to 0 when the rowing stops. The default mapping drives the avatar forward with it.
class FitShowRowerPlugin : public InputPlugin {
    Q_OBJECT
public:
    bool isSupported() const override { return true; }
    const QString getName() const override { return NAME; }
    const QString getID() const override { return ID_STRING; }
    bool isRunning() const override { return _active && _enabled; }

    void init() override;
    bool activate() override;
    void deactivate() override;

    void pluginFocusOutEvent() override {}
    void pluginUpdate(float deltaTime, const controller::InputCalibrationData& inputCalibrationData) override;

    void saveSettings() const override;
    void loadSettings() override;

private:
    class InputDevice : public controller::InputDevice {
    public:
        enum Channel { THROTTLE = 0 };

        InputDevice() : controller::InputDevice("FitShowRower") {}

        controller::Input::NamedVector getAvailableInputs() const override;
        QString getDefaultMappingConfig() const override;
        void setThrottle(float throttle);
    };

    void updateRunning();
    void startLink();
    void stopLink();

    static const char* NAME;
    static const char* ID_STRING;

    std::shared_ptr<InputDevice> _inputDevice;
    QThread* _linkThread { nullptr };
    FitShowLink* _link { nullptr };
    std::mutex _throttleMutex;
    StrokeThrottle _throttle;
};

#endif // hifi_FitShowRowerPlugin_h
