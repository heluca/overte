//
//  AgentScriptingInterface.cpp
//  assignment-client/src
//
//  Created by Thijs Wenker on 7/23/18.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AgentScriptingInterface.h"

#include <cmath>

#include <AudioScriptingInterface.h>
#include <ResourceManager.h>

AgentScriptingInterface::AgentScriptingInterface(Agent* agent) :
    QObject(agent),
    _agent(agent)
{
    connect(agent, &Agent::audioSinkFinished, this, &AgentScriptingInterface::audioSinkFinished);
    connect(agent, &Agent::voiceStreamFinished, this, &AgentScriptingInterface::voiceStreamFinished);
}

bool AgentScriptingInterface::startAudioSink(const QString& url, const QVariantMap& options) const {
    const int DEFAULT_FRAMES_PER_MESSAGE = 2;
    int framesPerMessage = DEFAULT_FRAMES_PER_MESSAGE;
    if (options.contains("framesPerMessage")) {
        bool isNumber = false;
        framesPerMessage = options.value("framesPerMessage").toInt(&isNumber);
        if (!isNumber) {
            framesPerMessage = 0;  // rejected by AudioStreamSink::create
        }
    }

    // Script credentials are kept for http and https prefixes; a WebSocket upgrade is an http or https request
    const QUrl sinkURL(url);
    QUrl credentialURL(sinkURL);
    if (sinkURL.scheme() == "ws") {
        credentialURL.setScheme("http");
    } else if (sinkURL.scheme() == "wss") {
        credentialURL.setScheme("https");
    }
    // Looked up here, on the script thread
    const AudioStreamCredential credential = AudioScriptingInterface::scriptCredentialFor(credentialURL);
    return _agent->startAudioSink(sinkURL, framesPerMessage, credential);
}

bool AgentScriptingInterface::playVoiceStream(const QString& url, const QVariantMap& options) const {
    // Seconds, read as whole 10 ms frames
    auto framesOption = [&options](const QString& name, double defaultSeconds, double maxSeconds, int& frames) {
        double seconds = defaultSeconds;
        if (options.contains(name)) {
            bool isNumber = false;
            seconds = options.value(name).toDouble(&isNumber);
            if (!isNumber || !(seconds >= 0.0 && seconds <= maxSeconds)) {
                qWarning() << "Agent.playVoiceStream:" << name << "must be a number of seconds from 0 to" << maxSeconds
                           << "- got" << options.value(name);
                return false;
            }
        }
        frames = (int)std::lround(seconds * 1000.0 / (double)AudioConstants::NETWORK_FRAME_MSECS);
        return true;
    };
    const double DEFAULT_PREBUFFER_SECONDS = 0.02;
    const double MAX_PREBUFFER_SECONDS = AudioStreamSource::MAX_BUFFERED_FRAMES * (double)AudioConstants::NETWORK_FRAME_MSECS / 1000.0;
    const double DEFAULT_IDLE_TIMEOUT_SECONDS = 10.0;
    const double MAX_IDLE_TIMEOUT_SECONDS = 24.0 * 60.0 * 60.0;
    int prebufferFrames = 0;
    int idleTimeoutFrames = 0;
    if (!framesOption("prebuffer", DEFAULT_PREBUFFER_SECONDS, MAX_PREBUFFER_SECONDS, prebufferFrames) ||
        !framesOption("idleTimeout", DEFAULT_IDLE_TIMEOUT_SECONDS, MAX_IDLE_TIMEOUT_SECONDS, idleTimeoutFrames)) {
        return false;
    }

    // Looked up here, on the script thread, for the URL the stream will request
    const QUrl streamURL = DependencyManager::get<ResourceManager>()->normalizeURL(QUrl(url));
    const AudioStreamCredential credential = AudioScriptingInterface::scriptCredentialFor(streamURL);
    return _agent->playVoiceStream(streamURL, prebufferFrames, idleTimeoutFrames, credential);
}
