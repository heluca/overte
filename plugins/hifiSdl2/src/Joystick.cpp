//
//  Joystick.cpp
//  input-plugins/src/input-plugins
//
//  Created by Stephen Birarda on 2014-09-23.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Joystick.h"

#include <glm/glm.hpp>

#include <PathUtils.h>

const float CONTROLLER_THRESHOLD = 0.3f;

const float MAX_AXIS = 32768.0f;

Joystick::Joystick(SDL_JoystickID instanceId, SDL_GameController* sdlGameController) :
        InputDevice("GamePad"),
    _sdlGameController(sdlGameController),
    _sdlJoystick(SDL_GameControllerGetJoystick(_sdlGameController)),
    _instanceId(instanceId)
{
    if (!SDL_GameControllerHasRumble(_sdlGameController)) {
        qDebug() << "Game controller" << SDL_GameControllerName(_sdlGameController) << "has no rumble support";
    }
}

Joystick::~Joystick() {
    closeJoystick();
}

void Joystick::closeJoystick() {
    SDL_GameControllerClose(_sdlGameController);
}

void Joystick::update(float deltaTime, const controller::InputCalibrationData& inputCalibrationData) {
    for (auto axisState : _axisStateMap) {
        if (fabsf(axisState.second.value) < CONTROLLER_THRESHOLD) {
            _axisStateMap[axisState.first].value = 0.0f;
        }
    }
}

void Joystick::focusOutEvent() {
    _axisStateMap.clear();
    _buttonPressedMap.clear();
};

void Joystick::handleAxisEvent(const SDL_ControllerAxisEvent& event) {
    SDL_GameControllerAxis axis = (SDL_GameControllerAxis) event.axis;
    _axisStateMap[makeInput((controller::StandardAxisChannel)axis).getChannel()].value = (float)event.value / MAX_AXIS;
}

void Joystick::handleButtonEvent(const SDL_ControllerButtonEvent& event) {
    auto input = makeInput((controller::StandardButtonChannel)event.button);
    bool newValue = event.state == SDL_PRESSED;
    if (newValue) {
        _buttonPressedMap.insert(input.getChannel());
    } else {
        _buttonPressedMap.erase(input.getChannel());
    }
}

bool Joystick::triggerHapticPulse(float strength, float duration, uint16_t index) {
    // SDL_GameControllerRumble drives the controller's own motors through the game controller
    // driver (XInput, hidapi for DualShock/DualSense, ...). The legacy SDL_Haptic rumble it replaces
    // only worked for devices exposed through the OS force feedback API, which excludes every
    // PlayStation controller on macOS.
    const float MAX_RUMBLE = 65535.0f;
    uint16_t rumble = (uint16_t)(glm::clamp(strength, 0.0f, 1.0f) * MAX_RUMBLE);
    uint32_t durationMs = (uint32_t)glm::max(duration, 0.0f);
    return SDL_GameControllerRumble(_sdlGameController, rumble, rumble, durationMs) == 0;
}

controller::Input::NamedVector Joystick::getAvailableInputs() const {
    using namespace controller;
    if (_availableInputs.length() == 0) {
        _availableInputs = {
            makePair(A, "A"),
            makePair(B, "B"),
            makePair(X, "X"),
            makePair(Y, "Y"),
            // DPad
            makePair(DU, "DU"),
            makePair(DD, "DD"),
            makePair(DL, "DL"),
            makePair(DR, "DR"),
            // Bumpers
            makePair(LB, "LB"),
            makePair(RB, "RB"),
            // Stick press
            makePair(LS, "LS"),
            makePair(RS, "RS"),
            // Center buttons
            makePair(START, "Start"),
            makePair(BACK, "Back"),
            // Analog sticks
            makePair(LX, "LX"),
            makePair(LY, "LY"),
            makePair(RX, "RX"),
            makePair(RY, "RY"),

            // Triggers
            makePair(LT, "LT"),
            makePair(RT, "RT"),

            // Aliases, PlayStation style names
            makePair(LB, "L1"),
            makePair(RB, "R1"),
            makePair(LT, "L2"),
            makePair(RT, "R2"),
            makePair(LS, "L3"),
            makePair(RS, "R3"),
            makePair(BACK, "Select"),
            makePair(A, "Cross"),
            makePair(B, "Circle"),
            makePair(X, "Square"),
            makePair(Y, "Triangle"),
            makePair(DU, "Up"),
            makePair(DD, "Down"),
            makePair(DL, "Left"),
            makePair(DR, "Right"),
        };
    }
    return _availableInputs;
}

QString Joystick::getDefaultMappingConfig() const {
    static const QString MAPPING_JSON = PathUtils::resourcesPath() + "/controllers/xbox.json";
    return MAPPING_JSON;
}
