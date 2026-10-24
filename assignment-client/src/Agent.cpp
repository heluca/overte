//
//  Agent.cpp
//  assignment-client/src
//
//  Created by Stephen Birarda on 7/1/13.
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "Agent.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QStandardPaths>
#include <QtNetwork/QNetworkDiskCache>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QThread>

#include <AnimationCacheScriptingInterface.h>
#include <AssetClient.h>
#include <AvatarHashMap.h>
#include <AudioInjectorManager.h>
#include <AssetClient.h>
#include <DebugDraw.h>
#include <EntityScriptClient.h>
#include <EntityScriptingInterface.h>
#include <LocationScriptingInterface.h>
#include <AudioScriptingInterface.h>
#include <MessagesClient.h>
#include <NetworkAccessManager.h>
#include <NodeList.h>
#include <udt/PacketHeaders.h>
#include <ResourceCache.h>
#include <ResourceScriptingInterface.h>
#include <ScriptCache.h>
#include <ScriptCredentials.h>
#include <ScriptEngine.h>
#include <ScriptEngines.h>
#include <ScriptManager.h>
#include <SoundCacheScriptingInterface.h>
#include <SoundCache.h>
#include <UserActivityLoggerScriptingInterface.h>
#include <UsersScriptingInterface.h>
#include <UUID.h>

#include <recording/ClipCache.h>
#include <recording/Deck.h>
#include <recording/Recorder.h>
#include <recording/Frame.h>

#include <plugins/PluginManager.h>

#include <EntityScriptingInterface.h> // TODO: consider moving to scriptengine.h

#include <hfm/ModelFormatRegistry.h>

#include "entities/AssignmentParentFinder.h"
#include "AssignmentDynamicFactory.h"
#include <recording/RecordingScriptingInterface.h>
#include "AgentAvatarAudio.h"
#include "AgentScriptingInterface.h"

static const int RECEIVED_AUDIO_STREAM_CAPACITY_FRAMES = 10;

Agent::Agent(ReceivedMessage& message) :
    ThreadedAssignment(message),
    _receivedAudioStream(RECEIVED_AUDIO_STREAM_CAPACITY_FRAMES, RECEIVED_AUDIO_STREAM_CAPACITY_FRAMES)
{
    DependencyManager::set<ScriptableAvatar>();

    DependencyManager::registerInheritance<EntityDynamicFactoryInterface, AssignmentDynamicFactory>();
    DependencyManager::set<AssignmentDynamicFactory>();

    DependencyManager::set<AnimationCache>();
    DependencyManager::set<AnimationCacheScriptingInterface>();
    DependencyManager::set<EntityScriptingInterface>(false);
    DependencyManager::set<EntityScriptClient>();

    DependencyManager::set<ResourceScriptingInterface>();
    DependencyManager::set<UserActivityLoggerScriptingInterface>();

    _entityEditSender.setPacketsPerSecond(DEFAULT_ENTITY_PPS_PER_SCRIPT);
    DependencyManager::get<EntityScriptingInterface>()->setPacketSender(&_entityEditSender);

    DependencyManager::set<ResourceManager>();
    DependencyManager::set<PluginManager>()->instantiate();
    _avatarAudio = std::make_unique<AgentAvatarAudio>(*this, _receivedAudioStream);

    DependencyManager::registerInheritance<SpatialParentFinder, AssignmentParentFinder>();

    DependencyManager::set<ResourceCacheSharedItems>();
    DependencyManager::set<SoundCache>();
    DependencyManager::set<SoundCacheScriptingInterface>();
    DependencyManager::set<AudioScriptingInterface>();
    DependencyManager::set<AudioInjectorManager>();

    DependencyManager::set<recording::Deck>();
    DependencyManager::set<recording::Recorder>();
    DependencyManager::set<recording::ClipCache>();

    DependencyManager::set<RecordingScriptingInterface>();
    DependencyManager::set<UsersScriptingInterface>();

    DependencyManager::set<ModelFormatRegistry>();
    DependencyManager::set<ModelCache>();

    DependencyManager::set<ScriptCredentials>();

    // Needed to ensure the creation of the DebugDraw instance on the main thread
    DebugDraw::getInstance();


    auto& packetReceiver = DependencyManager::get<NodeList>()->getPacketReceiver();

    packetReceiver.registerListenerForTypes(
        { PacketType::MixedAudio, PacketType::SilentAudioFrame },
        PacketReceiver::makeUnsourcedListenerReference<Agent>(this, &Agent::handleAudioPacket));
    packetReceiver.registerListenerForTypes(
        { PacketType::OctreeStats, PacketType::EntityData, PacketType::EntityErase },
        PacketReceiver::makeSourcedListenerReference<Agent>(this, &Agent::handleOctreePacket));
}

void Agent::playAvatarSound(SharedSoundPointer sound) {
    // this must happen on Agent's main thread
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "playAvatarSound", Q_ARG(SharedSoundPointer, sound));
        return;
    } else {
        // TODO: seems to add occasional artifact in tests.  I believe it is
        // correct to do this, but need to figure out for sure, so commenting this
        // out until I verify.
        // _numAvatarSoundSentBytes = 0;
        if (_avatarAudio) {
            _avatarAudio->setAvatarSound(sound);
        }
    }
}

void Agent::handleOctreePacket(QSharedPointer<ReceivedMessage> message, SharedNodePointer senderNode) {
    auto packetType = message->getType();

    if (packetType == PacketType::OctreeStats) {

        int statsMessageLength = OctreeHeadlessViewer::parseOctreeStats(message, senderNode);
        if (message->getSize() > statsMessageLength) {
            // pull out the piggybacked packet and create a new QSharedPointer<NLPacket> for it
            int piggyBackedSizeWithHeader = message->getSize() - statsMessageLength;

            auto buffer = std::unique_ptr<char[]>(new char[piggyBackedSizeWithHeader]);
            memcpy(buffer.get(), message->getRawMessage() + statsMessageLength, piggyBackedSizeWithHeader);

            auto newPacket = NLPacket::fromReceivedPacket(std::move(buffer), piggyBackedSizeWithHeader, message->getSenderSockAddr());
            message = QSharedPointer<ReceivedMessage>::create(*newPacket);
        } else {
            return; // bail since no piggyback data
        }

        packetType = message->getType();
    } // fall through to piggyback message

    if (packetType == PacketType::EntityData) {
        _entityViewer.processDatagram(*message, senderNode);
    } else if (packetType == PacketType::EntityErase) {
        _entityViewer.processEraseMessage(*message, senderNode);
    }
}

void Agent::handleAudioPacket(QSharedPointer<ReceivedMessage> message) {
    _receivedAudioStream.parseData(*message);
    _lastReceivedAudioLoudness = _receivedAudioStream.getNextOutputFrameLoudness();
    _receivedAudioStream.clearBuffer();
}

static const QString AGENT_LOGGING_NAME = "agent";

void Agent::run() {
    // Create ScriptEngines on threaded-assignment thread then move to main thread.
    DependencyManager::set<ScriptEngines>(ScriptManager::AGENT_SCRIPT)->moveToThread(qApp->thread());

    DependencyManager::set<ScriptCache>();

    // make sure we request our script once the agent connects to the domain
    auto nodeList = DependencyManager::get<NodeList>();

    connect(&nodeList->getDomainHandler(), &DomainHandler::connectedToDomain, this, &Agent::requestScript);
    connect(&nodeList->getDomainHandler(), &DomainHandler::settingsReceived, this, &Agent::handleSettings);

    ThreadedAssignment::commonInit(AGENT_LOGGING_NAME, NodeType::Agent);

    // Setup MessagesClient
    auto messagesClient = DependencyManager::set<MessagesClient>();
    messagesClient->startThread();

    // make sure we hear about connected nodes so we can grab an ATP script if a request is pending
    connect(nodeList.data(), &LimitedNodeList::nodeActivated, this, &Agent::nodeActivated);

    // make sure we hear about dissappearing nodes so we can clear the entity tree if an entity server goes away
    connect(nodeList.data(), &LimitedNodeList::nodeKilled, this,  &Agent::nodeKilled);

    nodeList->addSetOfNodeTypesToNodeInterestSet({
        NodeType::AudioMixer, NodeType::AvatarMixer, NodeType::EntityServer,
        NodeType::MessagesMixer, NodeType::AssetServer, NodeType::EntityScriptServer
    });
}

void Agent::handleSettings(const QJsonObject& settingsObject) {
    ScriptCredentials::loadDomainSettings(settingsObject);
}

void Agent::requestScript() {
    auto nodeList = DependencyManager::get<NodeList>();
    disconnect(&nodeList->getDomainHandler(), &DomainHandler::connectedToDomain, this, &Agent::requestScript);

    // figure out the URL for the script for this agent assignment
    QUrl scriptURL;
    if (_payload.isEmpty())  {
        scriptURL = QUrl(QString("http://%1:%2/assignment/%3/")
                         .arg(nodeList->getDomainHandler().getIP().toString())
                         .arg(DOMAIN_SERVER_HTTP_PORT)
                         .arg(uuidStringWithoutCurlyBraces(nodeList->getSessionUUID())));
    } else {
        scriptURL = QUrl(_payload);
    }

    // make sure this is not a script request for the file scheme
    if (scriptURL.scheme() == HIFI_URL_SCHEME_FILE) {
        qWarning() << "Cannot load script for Agent from local filesystem.";
        scriptRequestFinished();
        return;
    }

    auto request = DependencyManager::get<ResourceManager>()->createResourceRequest(
        this, scriptURL, true, -1, "Agent::requestScript");

    if (!request) {
        qWarning() << "Could not create ResourceRequest for Agent script at" << scriptURL.toString();
        scriptRequestFinished();
        return;
    }

    // setup a timeout for script request
    static const int SCRIPT_TIMEOUT_MS = 10000;
    _scriptRequestTimeout = new QTimer;
    connect(_scriptRequestTimeout, &QTimer::timeout, this, &Agent::scriptRequestFinished);
    _scriptRequestTimeout->start(SCRIPT_TIMEOUT_MS);

    connect(request, &ResourceRequest::finished, this, &Agent::scriptRequestFinished);

    if (scriptURL.scheme() == URL_SCHEME_ATP) {
        // we have an ATP URL for the script - if we're not currently connected to the AssetServer
        // then wait for the nodeConnected signal to fire off the request

        auto assetServer = nodeList->soloNodeOfType(NodeType::AssetServer);

        if (!assetServer || !assetServer->getActiveSocket()) {
            qDebug() << "Waiting to connect to Asset Server for ATP script download.";
            _pendingScriptRequest = request;

            return;
        }
    }

    qInfo() << "Requesting script at URL" << qPrintable(request->getUrl().toString());

    request->send();
}

void Agent::nodeActivated(SharedNodePointer activatedNode) {
    if (_pendingScriptRequest && activatedNode->getType() == NodeType::AssetServer) {
        qInfo() << "Requesting script at URL" << qPrintable(_pendingScriptRequest->getUrl().toString());

        _pendingScriptRequest->send();

        _pendingScriptRequest = nullptr;
    }
    if (activatedNode->getType() == NodeType::AudioMixer && _avatarAudio) {
        _avatarAudio->negotiateAudioFormat();
    }
}

void Agent::nodeKilled(SharedNodePointer killedNode) {
    if (killedNode->getType() == NodeType::EntityServer) {
        // an entity server has gone away, ask the headless viewer to clear its tree
        _entityViewer.clear();
    }
}

QStringList Agent::getAudioCodecs() const {
    return _avatarAudio->getAudioCodecs();
}

void Agent::setAudioCodecs(const QStringList& audioCodecs) {
    _avatarAudio->setAudioCodecs(audioCodecs);
}

glm::vec3 Agent::getAudioPositionOffset() const {
    return _avatarAudio->getAudioPositionOffset();
}

void Agent::setAudioPositionOffset(const glm::vec3& audioPositionOffset) {
    _avatarAudio->setAudioPositionOffset(audioPositionOffset);
}

void Agent::scriptRequestFinished() {
    auto request = qobject_cast<ResourceRequest*>(sender());

    // stop the script request timeout, if it's running
    if (_scriptRequestTimeout) {
        QMetaObject::invokeMethod(_scriptRequestTimeout, "stop");
        _scriptRequestTimeout->deleteLater();
    }

    if (request && request->getResult() == ResourceRequest::Success) {
        _scriptContents = request->getData();
        qInfo() << "Downloaded script:" << _scriptContents;

        // we could just call executeScript directly - we use a QueuedConnection to allow scriptRequestFinished
        // to return before calling executeScript
        QMetaObject::invokeMethod(this, "executeScript", Qt::QueuedConnection);
    } else {
        if (request) {
            qWarning() << "Failed to download script at" << request->getUrl().toString() << " - bailing on assignment.";
            qWarning() << "ResourceRequest error was" << request->getResult();
        } else {
            qWarning() << "Failed to download script - request timed out. Bailing on assignment.";
        }

        setFinished(true);
    }

    request->deleteLater();
}

void Agent::executeScript() {
    // the following block is scoped so that any shared pointers we take here
    // are cleared before we call setFinished at the end of the function
    {
        _scriptManager = scriptManagerFactory(ScriptManager::AGENT_SCRIPT, _scriptContents, _payload);

        // setup an Avatar for the script to use
        auto scriptedAvatar = DependencyManager::get<ScriptableAvatar>();
        scriptedAvatar->setID(getSessionUUID());

        // call model URL setters with empty URLs so our avatar, if user, will have the default models
        scriptedAvatar->setSkeletonModelURL(QUrl());

        // force lazy initialization of the head data for the scripted avatar
        // since it is referenced by AgentAvatarAudio::computeLoudness and getAudioLoudness
        scriptedAvatar->getHeadOrientation();

        auto scriptEngine = _scriptManager->engine();
        auto scopeGuard = scriptEngine->getScopeGuard();
        auto* sgp = scopeGuard.get();

        // give this AvatarData object to the script engine
        scriptEngine->registerGlobalObject(sgp, "Avatar", scriptedAvatar.data());

        // give scripts access to the Users object
        scriptEngine->registerGlobalObject(sgp, "Users", DependencyManager::get<UsersScriptingInterface>().data());

        auto player = DependencyManager::get<recording::Deck>();
        connect(player.data(), &recording::Deck::playbackStateChanged, [&player, &scriptedAvatar] {
            if (player->isPlaying()) {
                auto recordingInterface = DependencyManager::get<RecordingScriptingInterface>();
                if (recordingInterface->getPlayFromCurrentLocation()) {
                    scriptedAvatar->setRecordingBasis();
                }

                // these procedural movements are included in the recordings
                scriptedAvatar->setHasScriptedBlendshapes(true);
                scriptedAvatar->setHasProceduralEyeFaceMovement(false);
                scriptedAvatar->setHasProceduralBlinkFaceMovement(false);
                scriptedAvatar->setHasAudioEnabledFaceMovement(false);
            } else {
                scriptedAvatar->clearRecordingBasis();

                // restore procedural blendshape movement
                scriptedAvatar->setHasScriptedBlendshapes(false);
                scriptedAvatar->setHasProceduralEyeFaceMovement(true);
                scriptedAvatar->setHasProceduralBlinkFaceMovement(true);
                scriptedAvatar->setHasAudioEnabledFaceMovement(true);
            }
        });

        using namespace recording;
        static const FrameType AVATAR_FRAME_TYPE = Frame::registerFrameType(AvatarData::FRAME_NAME);
        Frame::registerFrameHandler(AVATAR_FRAME_TYPE, [scriptedAvatar](Frame::ConstPointer frame) {

            auto recordingInterface = DependencyManager::get<RecordingScriptingInterface>();
            bool useFrameSkeleton = recordingInterface->getPlayerUseSkeletonModel();

            // FIXME - the ability to switch the avatar URL is not actually supported when playing back from a recording
            if (!useFrameSkeleton) {
                static std::once_flag warning;
                std::call_once(warning, [] {
                    qWarning() << "Recording.setPlayerUseSkeletonModel(false) is not currently supported.";
                });
            }

            AvatarData::fromFrame(frame->data, *scriptedAvatar);
        });

        using namespace recording;
        static const FrameType AUDIO_FRAME_TYPE = Frame::registerFrameType(AudioConstants::getAudioFrameName());
        Frame::registerFrameHandler(AUDIO_FRAME_TYPE, [this, &player, &scriptedAvatar](Frame::ConstPointer frame) {
            _avatarAudio->handleRecordingAudioFrame(frame->data, player->getVolume(), scriptedAvatar);
        });

        auto avatarHashMap = DependencyManager::set<AvatarHashMap>();
        scriptEngine->registerGlobalObject(sgp, "AvatarList", avatarHashMap.data());

        // register ourselves to the script engine
        scriptEngine->registerGlobalObject(sgp, "Agent", new AgentScriptingInterface(this));

        scriptEngine->registerGlobalObject(sgp, "AnimationCache", DependencyManager::get<AnimationCacheScriptingInterface>().data());
        scriptEngine->registerGlobalObject(sgp, "SoundCache", DependencyManager::get<SoundCacheScriptingInterface>().data());

        auto entityScriptingInterface = DependencyManager::get<EntityScriptingInterface>();

        scriptEngine->registerGlobalObject(sgp, "EntityViewer", &_entityViewer);

        scriptEngine->registerGetterSetter(sgp, "location", LocationScriptingInterface::locationGetter,
                                            LocationScriptingInterface::locationSetter);

        auto recordingInterface = DependencyManager::get<RecordingScriptingInterface>();
        scriptEngine->registerGlobalObject(sgp, "Recording", recordingInterface.data());

        entityScriptingInterface->init();

        _entityViewer.init();

        entityScriptingInterface->setEntityTree(_entityViewer.getTree());

        DependencyManager::set<AssignmentParentFinder>(_entityViewer.getTree());

        DependencyManager::get<ScriptEngines>()->runScriptInitializers(_scriptManager);
        _scriptManager->run();

        Frame::clearFrameHandler(AUDIO_FRAME_TYPE);
        Frame::clearFrameHandler(AVATAR_FRAME_TYPE);

        if (recordingInterface->isPlaying()) {
            recordingInterface->stopPlaying();
        }

        if (recordingInterface->isRecording()) {
            recordingInterface->stopRecording();
        }

        setIsAvatar(false); // will stop timers for sending identity packets
    }

    setFinished(true);
}

QUuid Agent::getSessionUUID() const {
    return DependencyManager::get<NodeList>()->getSessionUUID();
}

void Agent::setIsListeningToAudioStream(bool isListeningToAudioStream) {
    // this must happen on Agent's main thread
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "setIsListeningToAudioStream", Q_ARG(bool, isListeningToAudioStream));
        return;
    }
    if (_isListeningToAudioStream && !isListeningToAudioStream) {
        // have to tell just the audio mixer to KillAvatar.

        auto nodeList = DependencyManager::get<NodeList>();
        nodeList->eachMatchingNode(
            [](const SharedNodePointer& node)->bool {
            return (node->getType() == NodeType::AudioMixer) && node->getActiveSocket();
        },
            [&](const SharedNodePointer& node) {
            qDebug() << "sending KillAvatar message to Audio Mixers";
            auto packet = NLPacket::create(PacketType::KillAvatar, NUM_BYTES_RFC4122_UUID + sizeof(KillAvatarReason), true);
            packet->write(getSessionUUID().toRfc4122());
            packet->writePrimitive(KillAvatarReason::NoReason);
            nodeList->sendPacket(std::move(packet), *node);
        });

    }
    _isListeningToAudioStream = isListeningToAudioStream;
}

void Agent::setIsNoiseGateEnabled(bool isNoiseGateEnabled) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "setIsNoiseGateEnabled", Q_ARG(bool, isNoiseGateEnabled));
        return;
    }
    _isNoiseGateEnabled = isNoiseGateEnabled;
}

void Agent::setIsAvatar(bool isAvatar) {
    // this must happen on Agent's main thread
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "setIsAvatar", Q_ARG(bool, isAvatar));
        return;
    }
    _isAvatar = isAvatar;

    auto scriptableAvatar = DependencyManager::get<ScriptableAvatar>();
    if (_isAvatar) {
        if (!_avatarQueryTimer) {
            // set up the avatar timers
            _avatarQueryTimer = new QTimer(this);

            // connect our slot
            connect(_avatarQueryTimer, &QTimer::timeout, this, &Agent::queryAvatars);

            static const int AVATAR_VIEW_PACKET_SEND_INTERVAL_MSECS = 1000;

            // start the timer
            _avatarQueryTimer->start(AVATAR_VIEW_PACKET_SEND_INTERVAL_MSECS);

            connect(_scriptManager.get(), &ScriptManager::update,
                    scriptableAvatar.data(), &ScriptableAvatar::update, Qt::QueuedConnection);

            // tell the avatarAudioTimer to start ticking
            if (_avatarAudio) {
                _avatarAudio->startSending();
            }
        }

        _entityEditSender.setMyAvatar(scriptableAvatar.data());
    } else {
        if (_avatarQueryTimer) {
            _avatarQueryTimer->stop();
            delete _avatarQueryTimer;
            _avatarQueryTimer = nullptr;

            // Clear the skeleton model so that if agent is set to an avatar again the skeleton model is (re)loaded.
            auto scriptedAvatar = DependencyManager::get<ScriptableAvatar>();
            scriptedAvatar->setSkeletonModelURL(QUrl());

            // The avatar mixer never times out a connection (e.g., based on identity or data packets)
            // but rather keeps avatars in its list as long as "connected". As a result, clients timeout
            // when we stop sending identity, but then get woken up again by the mixer itself, which sends
            // identity packets to everyone. Here we explicitly tell the mixer to kill the entry for us.
            auto nodeList = DependencyManager::get<NodeList>();
            nodeList->eachMatchingNode(
                [&](const SharedNodePointer& node)->bool {
                return (node->getType() == NodeType::AvatarMixer || node->getType() == NodeType::AudioMixer)
                        && node->getActiveSocket();
            },
                [&](const SharedNodePointer& node) {
                qDebug() << "sending KillAvatar message to Avatar and Audio Mixers";
                auto packet = NLPacket::create(PacketType::KillAvatar, NUM_BYTES_RFC4122_UUID + sizeof(KillAvatarReason), true);
                packet->write(getSessionUUID().toRfc4122());
                packet->writePrimitive(KillAvatarReason::NoReason);
                nodeList->sendPacket(std::move(packet), *node);
            });

            disconnect(_scriptManager.get(), &ScriptManager::update,
                       scriptableAvatar.data(), &ScriptableAvatar::update);

            if (_avatarAudio) {
                _avatarAudio->stopSending();
            }
        }

        _entityEditSender.setMyAvatar(nullptr);
    }
}

void Agent::queryAvatars() {
    auto scriptedAvatar = DependencyManager::get<ScriptableAvatar>();

    ViewFrustum view;
    view.setPosition(scriptedAvatar->getWorldPosition());
    view.setOrientation(scriptedAvatar->getHeadOrientation());
    view.setProjection(DEFAULT_FIELD_OF_VIEW_DEGREES, DEFAULT_ASPECT_RATIO,
                       DEFAULT_NEAR_CLIP, DEFAULT_FAR_CLIP);
    view.calculate();
    ConicalViewFrustum conicalView { view };

    auto avatarPacket = NLPacket::create(PacketType::AvatarQuery);
    auto destinationBuffer = reinterpret_cast<unsigned char*>(avatarPacket->getPayload());
    auto bufferStart = destinationBuffer;

    uint8_t numFrustums = 1;
    memcpy(destinationBuffer, &numFrustums, sizeof(numFrustums));
    destinationBuffer += sizeof(numFrustums);

    destinationBuffer += conicalView.serialize(destinationBuffer);

    avatarPacket->setPayloadSize(destinationBuffer - bufferStart);

    DependencyManager::get<NodeList>()->broadcastToNodes(std::move(avatarPacket),
                                                         { NodeType::AvatarMixer });
}

void Agent::aboutToFinish() {
    // our entity tree is going to go away so tell that to the EntityScriptingInterface
    DependencyManager::get<EntityScriptingInterface>()->setEntityTree(nullptr);

    DependencyManager::get<ResourceManager>()->cleanup();

    DependencyManager::destroy<ModelFormatRegistry>();
    DependencyManager::destroy<ModelCache>();

    DependencyManager::destroy<PluginManager>();

    // cleanup the AudioInjectorManager (and any still running injectors)
    DependencyManager::destroy<AudioInjectorManager>();

    // destroy all other created dependencies

    DependencyManager::destroy<SoundCacheScriptingInterface>();
    DependencyManager::destroy<AudioScriptingInterface>();
    DependencyManager::destroy<RecordingScriptingInterface>();
    DependencyManager::destroy<AnimationCacheScriptingInterface>();
    DependencyManager::destroy<EntityScriptingInterface>();
    DependencyManager::destroy<EntityScriptClient>();
    DependencyManager::destroy<ResourceScriptingInterface>();
    DependencyManager::destroy<UserActivityLoggerScriptingInterface>();

    DependencyManager::destroy<ScriptCache>();
    DependencyManager::destroy<SoundCache>();
    DependencyManager::destroy<AnimationCache>();
    DependencyManager::destroy<ScriptCredentials>();

    DependencyManager::destroy<recording::Deck>();
    DependencyManager::destroy<recording::Recorder>();
    DependencyManager::destroy<recording::ClipCache>();

    DependencyManager::destroy<AvatarHashMap>();
    DependencyManager::destroy<AssignmentParentFinder>();
    DependencyManager::destroy<MessagesClient>();
    DependencyManager::destroy<ResourceManager>();

    DependencyManager::destroy<ResourceCacheSharedItems>();

    // drop our shared pointer to the script engine, then ask ScriptEngines to shutdown scripting
    // this ensures that the ScriptEngine goes down before ScriptEngines
    _scriptManager.reset();

    {
        DependencyManager::get<ScriptEngines>()->shutdownScripting();
    }
    
    DependencyManager::destroy<ScriptEngines>();

    DependencyManager::destroy<AssignmentDynamicFactory>();

    DependencyManager::destroy<ScriptableAvatar>();

    // stops the audio timers, then releases the codec & encoder
    _avatarAudio.reset();
}

void Agent::stop() {
    if (_scriptManager) {
        _scriptManager->stop();
    } else {
        setFinished(true);
    }
}
