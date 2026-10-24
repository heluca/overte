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

#include <AudioScriptingInterface.h>

AgentScriptingInterface::AgentScriptingInterface(Agent* agent) :
    QObject(agent),
    _agent(agent)
{
    connect(agent, &Agent::audioSinkFinished, this, &AgentScriptingInterface::audioSinkFinished);
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
