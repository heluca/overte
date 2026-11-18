//
//  FitShowRowerPlugin.cpp
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "FitShowRowerPlugin.h"

#include <chrono>

#include <QtCore/QThread>

#include <controllers/UserInputMapper.h>
#include <PathUtils.h>
#include <Preferences.h>
#include <SettingHandle.h>

#include "FitShowLink.h"
#include "FitShowRowerLogging.h"

const char* FitShowRowerPlugin::NAME = "FitShowRower";
const char* FitShowRowerPlugin::ID_STRING = "FitShow Rower";

namespace {

// Off by default: while enabled it keeps scanning for a console.
const bool DEFAULT_ENABLED = false;
const QString ENABLED_SETTING = "enabled";

double secondsNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void FitShowRowerPlugin::init() {
    _inputDevice = std::make_shared<InputDevice>();
    loadSettings();

    auto preferences = DependencyManager::get<Preferences>();
    auto getter = [this]() -> bool { return _enabled; };
    auto setter = [this](bool value) {
        _enabled = value;
        saveSettings();
        updateRunning();
    };
    preferences->addPreference(new CheckPreference(ID_STRING, "Enabled", getter, setter));
}

bool FitShowRowerPlugin::activate() {
    InputPlugin::activate();
    loadSettings();
    updateRunning();
    return true;
}

void FitShowRowerPlugin::deactivate() {
    InputPlugin::deactivate();
    updateRunning();
}

void FitShowRowerPlugin::pluginUpdate(float deltaTime, const controller::InputCalibrationData& inputCalibrationData) {
    if (!_linkThread) {
        return;
    }
    float throttle;
    {
        std::lock_guard<std::mutex> lock(_throttleMutex);
        throttle = _throttle.getThrottle(secondsNow());
    }
    auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
    userInputMapper->withLock([&] { _inputDevice->setThrottle(throttle); });
}

void FitShowRowerPlugin::saveSettings() const {
    Settings settings;
    settings.beginGroup(getID());
    settings.setValue(ENABLED_SETTING, _enabled);
    settings.endGroup();
}

void FitShowRowerPlugin::loadSettings() {
    Settings settings;
    settings.beginGroup(getID());
    _enabled = settings.value(ENABLED_SETTING, QVariant(DEFAULT_ENABLED)).toBool();
    settings.endGroup();
}

void FitShowRowerPlugin::updateRunning() {
    bool shouldRun = isRunning();
    if (shouldRun == (_linkThread != nullptr)) {
        return;
    }
    auto userInputMapper = DependencyManager::get<controller::UserInputMapper>();
    if (shouldRun) {
        userInputMapper->registerDevice(_inputDevice);
        startLink();
    } else {
        stopLink();
        userInputMapper->removeDevice(_inputDevice->getDeviceID());
    }
    emit deviceStatusChanged(getName(), shouldRun);
}

void FitShowRowerPlugin::startLink() {
    _linkThread = new QThread();
    _linkThread->setObjectName("FitShow Rower");
    _link = new FitShowLink();
    _link->moveToThread(_linkThread);
    connect(_linkThread, &QThread::finished, _link, &QObject::deleteLater);
    // Direct, so a reading is timed when it arrives rather than when the main thread gets to it
    connect(_link, &FitShowLink::readingReceived, this, [this](quint16 distance, quint16 strokes) {
        std::lock_guard<std::mutex> lock(_throttleMutex);
        _throttle.addReading(secondsNow(), distance, strokes);
    }, Qt::DirectConnection);
    connect(_link, &FitShowLink::linkLost, this, [this] {
        std::lock_guard<std::mutex> lock(_throttleMutex);
        _throttle.reset();
    }, Qt::DirectConnection);
    _linkThread->start();
    QMetaObject::invokeMethod(_link, &FitShowLink::start);
}

void FitShowRowerPlugin::stopLink() {
    QMetaObject::invokeMethod(_link, &FitShowLink::stop, Qt::BlockingQueuedConnection);
    _linkThread->quit();
    _linkThread->wait();
    delete _linkThread;
    _linkThread = nullptr;
    _link = nullptr;
    std::lock_guard<std::mutex> lock(_throttleMutex);
    _throttle.reset();
}

controller::Input::NamedVector FitShowRowerPlugin::InputDevice::getAvailableInputs() const {
    return { makePair(static_cast<controller::StandardAxisChannel>(THROTTLE), "Throttle") };
}

QString FitShowRowerPlugin::InputDevice::getDefaultMappingConfig() const {
    static const QString MAPPING_JSON = PathUtils::resourcesPath() + "/controllers/fitshow_rower.json";
    return MAPPING_JSON;
}

void FitShowRowerPlugin::InputDevice::setThrottle(float throttle) {
    _axisStateMap[THROTTLE] = controller::AxisValue(throttle, 0);
}
