//
//  ScriptableAvatar.cpp
//  assignment-client/src/avatars
//
//  Created by Clement on 7/22/14.
//  Copyright 2014 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptableAvatar.h"

#include <QDebug>
#include <QThread>
#include <glm/gtx/transform.hpp>

#include <shared/QtHelpers.h>
#include <AnimUtil.h>
#include <AvatarHashMap.h>
#include <ClientTraitsHandler.h>
#include <GLMHelpers.h>
#include <ResourceRequestObserver.h>
#include <AvatarLogging.h>
#include <EntityItem.h>
#include <EntityItemProperties.h>
#include <NetworkAccessManager.h>
#include <NetworkingConstants.h>


ScriptableAvatar::ScriptableAvatar() {
    _clientTraitsHandler.reset(new ClientTraitsHandler(this));
    static std::once_flag once;
    std::call_once(once, [] {
        qRegisterMetaType<HFMModel::Pointer>("HFMModel::Pointer");
    });
}

QByteArray ScriptableAvatar::toByteArrayStateful(AvatarDataDetail dataDetail, bool dropFaceTracking) {
    _globalPosition = getWorldPosition();
    return AvatarData::toByteArrayStateful(dataDetail, dropFaceTracking);
}


// hold and priority unused but kept so that client side JS can run.
void ScriptableAvatar::startAnimation(const QString& url, float fps, float priority,
                              bool loop, bool hold, float firstFrame, float lastFrame, const QStringList& maskedJoints) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "startAnimation", Q_ARG(const QString&, url), Q_ARG(float, fps),
                                  Q_ARG(float, priority), Q_ARG(bool, loop), Q_ARG(bool, hold), Q_ARG(float, firstFrame),
                                  Q_ARG(float, lastFrame), Q_ARG(const QStringList&, maskedJoints));
        return;
    }
    _animation = DependencyManager::get<AnimationCache>()->getAnimation(url);
    _animationDetails = AnimationDetails("", QUrl(url), fps, 0, loop, hold, false, firstFrame, lastFrame, true, firstFrame, false);
    _maskedJoints = maskedJoints;
    _animationAnimSkeleton.reset();
}

void ScriptableAvatar::stopAnimation() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "stopAnimation");
        return;
    }
    _animation.clear();
}

AnimationDetails ScriptableAvatar::getAnimationDetails() {
    if (QThread::currentThread() != thread()) {
        AnimationDetails result;
        BLOCKING_INVOKE_METHOD(this, "getAnimationDetails", 
                                  Q_RETURN_ARG(AnimationDetails, result));
        return result;
    }
    return _animationDetails;
}

int ScriptableAvatar::getJointIndex(const QString& name) const {
    // Faux joints:
    int result = AvatarData::getJointIndex(name);
    if (result != -1) {
        return result;
    }
    QReadLocker readLock(&_jointDataLock);
    return _fstJointIndices.value(name) - 1;
}

QStringList ScriptableAvatar::getJointNames() const {
    QReadLocker readLock(&_jointDataLock);
    return _fstJointNames;
    return QStringList();
}

void ScriptableAvatar::setSkeletonModelURL(const QUrl& skeletonModelURL) {
    _avatarAnimSkeleton.reset();
    _geometryResource.reset();

    AvatarData::setSkeletonModelURL(skeletonModelURL);
    updateJointMappings();
}

int ScriptableAvatar::sendAvatarDataPacket(bool sendAll) {
    using namespace std::chrono;
    auto now = Clock::now();

    int MAX_DATA_RATE_MBPS = 3;
    int maxDataRateBytesPerSeconds = MAX_DATA_RATE_MBPS * BYTES_PER_KILOBYTE * KILO_PER_MEGA / BITS_IN_BYTE;
    int maxDataRateBytesPerMilliseconds = maxDataRateBytesPerSeconds / MSECS_PER_SECOND;

    auto bytesSent = 0;

    if (now > _nextTraitsSendWindow) {
        if (getIdentityDataChanged()) {
            bytesSent += sendIdentityPacket();
        }

        bytesSent += _clientTraitsHandler->sendChangedTraitsToMixer();

        // Compute the next send window based on how much data we sent and what
        // data rate we're trying to max at.
        milliseconds timeUntilNextSend { bytesSent / maxDataRateBytesPerMilliseconds };
        _nextTraitsSendWindow += timeUntilNextSend;

        // Don't let the next send window lag behind if we're not sending a lot of data.
        if (_nextTraitsSendWindow < now) {
            _nextTraitsSendWindow = now;
        }
    }

    bytesSent += AvatarData::sendAvatarDataPacket(sendAll);

    return bytesSent;
}

// Mirrors copyAndRetargetFromNetworkAnim() in AnimClip.cpp, which is how Interface plays the same clips.
static float computeBoneLengthScale(const AnimSkeleton& avatarSkeleton, const AnimSkeleton& animSkeleton,
                                    const HFMModel& animModel) {
    float boneLengthScale = 1.0f;
    const int avatarHipsIndex = avatarSkeleton.nameToJointIndex("Hips");
    const int animHipsIndex = animSkeleton.nameToJointIndex("Hips");
    if (avatarHipsIndex != -1 && animHipsIndex != -1) {
        const int avatarHipsParentIndex = avatarSkeleton.getParentIndex(avatarHipsIndex);
        const int animHipsParentIndex = animSkeleton.getParentIndex(animHipsIndex);

        const float avatarUnitScale = extractScale(avatarSkeleton.getGeometryOffset()).y;
        const float animationUnitScale = extractScale(animModel.offset).y;
        const float avatarHeightInMeters = avatarUnitScale * avatarSkeleton.getAbsoluteDefaultPose(avatarHipsIndex).trans().y;
        const float animHeightInMeters = animationUnitScale * animSkeleton.getAbsoluteDefaultPose(animHipsIndex).trans().y;

        float avatarHipsParentScale = 1.0f;
        if (avatarHipsParentIndex != -1) {
            avatarHipsParentScale = avatarSkeleton.getAbsoluteDefaultPose(avatarHipsParentIndex).scale().y;
        }
        float animHipsParentScale = 1.0f;
        if (animHipsParentIndex != -1) {
            animHipsParentScale = animSkeleton.getAbsoluteDefaultPose(animHipsParentIndex).scale().y;
        }

        const float EPSILON = 0.0001f;
        if ((fabsf(animHeightInMeters) > EPSILON) && (animationUnitScale > EPSILON) && (animHipsParentScale > EPSILON)) {
            const float avatarToAnimationHeightRatio = avatarHeightInMeters / animHeightInMeters;
            const float unitsRatio = 1.0f / (avatarUnitScale / animationUnitScale);
            const float parentScaleRatio = 1.0f / (avatarHipsParentScale / animHipsParentScale);
            boneLengthScale = avatarToAnimationHeightRatio * unitsRatio * parentScaleRatio;
        }
    }
    return boneLengthScale;
}

// The clip's local rotations only mean something on the clip's own skeleton, so they are made absolute there (with the
// clip's own pre/post rotations), copied to avatar joints by name, and made relative again on the avatar skeleton.
// Translations stay the avatar's own bone lengths; only motion away from the clip's first frame (the hips) carries
// over, scaled by boneLengthScale. Same scheme as copyAndRetargetFromNetworkAnim() in AnimClip.cpp.
static AnimPoseVec retargetAnimationFrame(const HFMModel& animModel, const AnimSkeleton& animSkeleton,
                                          const AnimSkeleton& avatarSkeleton, float boneLengthScale,
                                          const HFMAnimationFrame& animFrame) {
    const int animJointCount = animSkeleton.getNumJoints();
    const int avatarJointCount = avatarSkeleton.getNumJoints();

    std::vector<glm::quat> animRotations;
    animRotations.reserve(animJointCount);
    for (int i = 0; i < animJointCount; i++) {
        animRotations.push_back(animModel.joints[i].preRotation * animFrame.rotations[i] * animModel.joints[i].postRotation);
    }
    animSkeleton.convertRelativeRotationsToAbsolute(animRotations);

    std::vector<int> avatarToAnimJointIndexMap;
    avatarToAnimJointIndexMap.reserve(avatarJointCount);
    std::vector<glm::quat> avatarRotations;
    avatarRotations.reserve(avatarJointCount);
    for (int avatarJointIndex = 0; avatarJointIndex < avatarJointCount; avatarJointIndex++) {
        int animJointIndex = animSkeleton.nameToJointIndex(avatarSkeleton.getJointName(avatarJointIndex));
        avatarToAnimJointIndexMap.push_back(animJointIndex);
        if (animJointIndex >= 0) {
            avatarRotations.push_back(animRotations[animJointIndex]);
        } else {
            // Not in the clip: keep the avatar's default rotation relative to its parent.
            glm::quat avatarParentAbsoluteRot;
            int avatarParentJointIndex = avatarSkeleton.getParentIndex(avatarJointIndex);
            if (avatarParentJointIndex >= 0) {
                avatarParentAbsoluteRot = avatarRotations[avatarParentJointIndex];
            }
            avatarRotations.push_back(avatarParentAbsoluteRot * avatarSkeleton.getRelativeDefaultPose(avatarJointIndex).rot());
        }
    }
    avatarSkeleton.convertAbsoluteRotationsToRelative(avatarRotations);

    AnimPoseVec poses;
    poses.reserve(avatarJointCount);
    for (int avatarJointIndex = 0; avatarJointIndex < avatarJointCount; avatarJointIndex++) {
        const AnimPose& avatarDefaultPose = avatarSkeleton.getRelativeDefaultPose(avatarJointIndex);
        glm::vec3 relativeTranslation = avatarDefaultPose.trans();
        int animJointIndex = avatarToAnimJointIndexMap[avatarJointIndex];
        if (animJointIndex >= 0) {
            const glm::vec3& animZeroTrans = animModel.animationFrames[0].translations[animJointIndex];
            relativeTranslation += boneLengthScale * (animFrame.translations[animJointIndex] - animZeroTrans);
        }
        poses.push_back(AnimPose(avatarDefaultPose.scale(), avatarRotations[avatarJointIndex], relativeTranslation));
    }
    return poses;
}

void ScriptableAvatar::update(float deltatime) {
    if (!_geometryResource && !_skeletonModelFilenameURL.isEmpty()) { // AvatarData will parse the .fst, but not get the .fbx skeleton.
        _geometryResource = DependencyManager::get<ModelCache>()->getGeometryResource(_skeletonModelFilenameURL);
    }

    // Run animation
    Q_ASSERT(QThread::currentThread() == thread());
    if (_animation && _animation->isLoaded()) {
        Q_ASSERT(thread() == _animation->thread());
        auto frames = _animation->getFramesReference();
        if (frames.size() > 0 && _geometryResource && _geometryResource->isHFMModelLoaded()) {
            if (!_animationAnimSkeleton) {
                _animationAnimSkeleton = std::make_shared<AnimSkeleton>(_animation->getHFMModel());
            }
            if (!_avatarAnimSkeleton) {
                _avatarAnimSkeleton = std::make_shared<AnimSkeleton>(_geometryResource->getHFMModel());
            }
            float currentFrame = _animationDetails.currentFrame + deltatime * _animationDetails.fps;
            if (_animationDetails.loop || currentFrame < _animationDetails.lastFrame) {
                while (currentFrame >= _animationDetails.lastFrame) {
                    currentFrame -= (_animationDetails.lastFrame - _animationDetails.firstFrame);
                }
                _animationDetails.currentFrame = currentFrame;

                const int nJoints = _avatarAnimSkeleton->getNumJoints();
                if (_jointData.size() != nJoints) {
                    _jointData.resize(nJoints);
                }

                const HFMModel& animModel = _animation->getHFMModel();
                const float boneLengthScale = computeBoneLengthScale(*_avatarAnimSkeleton, *_animationAnimSkeleton, animModel);

                const int frameCount = frames.size();
                const HFMAnimationFrame& floorFrame = frames.at((int)glm::floor(currentFrame) % frameCount);
                const HFMAnimationFrame& ceilFrame = frames.at((int)glm::ceil(currentFrame) % frameCount);
                const float frameFraction = glm::fract(currentFrame);
                AnimPoseVec floorPoses = retargetAnimationFrame(animModel, *_animationAnimSkeleton, *_avatarAnimSkeleton,
                                                                boneLengthScale, floorFrame);
                AnimPoseVec ceilPoses = retargetAnimationFrame(animModel, *_animationAnimSkeleton, *_avatarAnimSkeleton,
                                                               boneLengthScale, ceilFrame);
                std::vector<AnimPose> poses(nJoints);
                blend(nJoints, floorPoses.data(), ceilPoses.data(), frameFraction, poses.data());

                for (const QString& name : _maskedJoints) {
                    int jointIndex = _avatarAnimSkeleton->nameToJointIndex(name);
                    if (jointIndex != -1) {
                        poses[jointIndex] = _avatarAnimSkeleton->getRelativeDefaultPose(jointIndex);
                    }
                }

                std::vector<AnimPose> absPoses = poses;
                Q_ASSERT(_avatarAnimSkeleton != nullptr);
                _avatarAnimSkeleton->convertRelativePosesToAbsolute(absPoses);
                for (int i = 0; i < nJoints; i++) {
                    JointData& data = _jointData[i];
                    AnimPose& absPose = absPoses[i];
                    if (data.rotation != absPose.rot()) {
                        data.rotation = absPose.rot();
                        data.rotationIsDefaultPose = false;
                    }
                    AnimPose& relPose = poses[i];
                    if (data.translation != relPose.trans()) {
                        data.translation = relPose.trans();
                        data.translationIsDefaultPose = false;
                    }
                }

            } else {
                _animation.clear();
            }
        }
    }

    quint64 now = usecTimestampNow();
    quint64 dt = now - _lastSendAvatarDataTime;

    if (dt > MIN_TIME_BETWEEN_MY_AVATAR_DATA_SENDS) {
        sendAvatarDataPacket();
        _lastSendAvatarDataTime = now;
    }
}

void ScriptableAvatar::updateJointMappings() {
    {
        QWriteLocker writeLock(&_jointDataLock);
        _fstJointIndices.clear();
        _fstJointNames.clear();
        _jointData.clear();
    }

    if (_skeletonModelURL.fileName().toLower().endsWith(".fst")) {
        ////
        // TODO: Should we rely upon HTTPResourceRequest for ResourceRequestObserver instead?
        // HTTPResourceRequest::doSend() covers all of the following and
        // then some. It doesn't cover the connect() call, so we may
        // want to add a HTTPResourceRequest::doSend() method that does
        // connects.
        QNetworkAccessManager& networkAccessManager = NetworkAccessManager::getInstance();
        QNetworkRequest networkRequest = QNetworkRequest(_skeletonModelURL);
        networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        networkRequest.setHeader(QNetworkRequest::UserAgentHeader, NetworkingConstants::OVERTE_USER_AGENT);
        DependencyManager::get<ResourceRequestObserver>()->update(
            _skeletonModelURL, -1, "AvatarData::updateJointMappings");
        QNetworkReply* networkReply = networkAccessManager.get(networkRequest);
        //
        ////
        connect(networkReply, &QNetworkReply::finished, this, &ScriptableAvatar::setJointMappingsFromNetworkReply);
    }
}

void ScriptableAvatar::setJointMappingsFromNetworkReply() {
    QNetworkReply* networkReply = static_cast<QNetworkReply*>(sender());
    // before we process this update, make sure that the skeleton model URL hasn't changed
    // since we made the FST request
    if (networkReply->url() != _skeletonModelURL) {
        qCDebug(avatars) << "Refusing to set joint mappings for FST URL that does not match the current URL";
        networkReply->deleteLater();
        return;
    }
    // TODO: this works only with .fst files currently, not directly with FBX and GLB models
    {
        QWriteLocker writeLock(&_jointDataLock);
        QByteArray line;
        while (!(line = networkReply->readLine()).isEmpty()) {
            line = line.trimmed();
            if (line.startsWith("filename")) {
                int filenameIndex = line.indexOf('=') + 1;
                if (filenameIndex > 0) {
                    _skeletonModelFilenameURL = _skeletonModelURL.resolved(QString(line.mid(filenameIndex).trimmed()));
                }
            }
            if (!line.startsWith("jointIndex")) {
                continue;
            }
            int jointNameIndex = line.indexOf('=') + 1;
            if (jointNameIndex == 0) {
                continue;
            }
            int secondSeparatorIndex = line.indexOf('=', jointNameIndex);
            if (secondSeparatorIndex == -1) {
                continue;
            }
            QString jointName = line.mid(jointNameIndex, secondSeparatorIndex - jointNameIndex).trimmed();
            bool ok;
            int jointIndex = line.mid(secondSeparatorIndex + 1).trimmed().toInt(&ok);
            if (ok) {
                while (_fstJointNames.size() < jointIndex + 1) {
                    _fstJointNames.append(QString());
                }
                _fstJointNames[jointIndex] = jointName;
            }
        }
        for (int i = 0; i < _fstJointNames.size(); i++) {
            _fstJointIndices.insert(_fstJointNames.at(i), i + 1);
        }
    }
    networkReply->deleteLater();
}

AvatarEntityMap ScriptableAvatar::getAvatarEntityData() const {
    auto data = getAvatarEntityDataInternal(true);
    return data;
}

AvatarEntityMap ScriptableAvatar::getAvatarEntityDataNonDefault() const {
    auto data = getAvatarEntityDataInternal(false);
    return data;

}

AvatarEntityMap ScriptableAvatar::getAvatarEntityDataInternal(bool allProperties) const {
    // DANGER: Now that we store the AvatarEntityData in packed format this call is potentially Very Expensive!
    // Avoid calling this method if possible.
    AvatarEntityMap data;
    QUuid sessionID = getID();
    _avatarEntitiesLock.withReadLock([&] {
        for (const auto& itr : _entities) {
            QUuid id = itr.first;
            EntityItemPointer entity = itr.second;

            EncodeBitstreamParams params;
            auto desiredProperties = entity->getEntityProperties(params);
            desiredProperties += PROP_LOCAL_POSITION;
            desiredProperties += PROP_LOCAL_ROTATION;
            desiredProperties += PROP_LOCAL_VELOCITY;
            desiredProperties += PROP_LOCAL_ANGULAR_VELOCITY;
            desiredProperties += PROP_LOCAL_DIMENSIONS;
            EntityItemProperties properties = entity->getProperties(desiredProperties);

            QByteArray blob;
            _helperScriptEngine.run( [&] {
                EntityItemProperties::propertiesToBlob(*_helperScriptEngine.get(), sessionID, properties, blob, allProperties);
            });
            data[id] = blob;
        }
    });
    return data;
}

void ScriptableAvatar::setAvatarEntityData(const AvatarEntityMap& avatarEntityData) {
    // Note: this is an invokable Script call
    // avatarEntityData is expected to be a map of QByteArrays that represent EntityItemProperties objects from JavaScript
    //
    if (avatarEntityData.size() > MAX_NUM_AVATAR_ENTITIES) {
        // the data is suspect
        qCDebug(avatars) << "discard suspect avatarEntityData with size =" << avatarEntityData.size();
        return;
    }

    // convert binary data to EntityItemProperties
    // NOTE: this operation is NOT efficient
    std::map<QUuid, EntityItemProperties> newProperties;
    AvatarEntityMap::const_iterator dataItr = avatarEntityData.begin();
    while (dataItr != avatarEntityData.end()) {
        EntityItemProperties properties;
        const QByteArray& blob = dataItr.value();
        if (!blob.isNull()) {
            _helperScriptEngine.run([&] {
                if (EntityItemProperties::blobToProperties(*_helperScriptEngine.get(), blob, properties)) {
                    newProperties[dataItr.key()] = properties;
                }
            });
        }
        ++dataItr;
    }

    // delete existing entities not found in avatarEntityData
    std::vector<QUuid> idsToClear;
    _avatarEntitiesLock.withWriteLock([&] {
        std::map<QUuid, EntityItemPointer>::iterator entityItr = _entities.begin();
        while (entityItr != _entities.end()) {
            QUuid id = entityItr->first;
            std::map<QUuid, EntityItemProperties>::const_iterator propertiesItr = newProperties.find(id);
            if (propertiesItr == newProperties.end()) {
                idsToClear.push_back(id);
                entityItr = _entities.erase(entityItr);
            } else {
                ++entityItr;
            }
        }
    });

    // add or update entities
    _avatarEntitiesLock.withWriteLock([&] {
        std::map<QUuid, EntityItemProperties>::const_iterator propertiesItr = newProperties.begin();
        while (propertiesItr != newProperties.end()) {
            QUuid id = propertiesItr->first;
            const EntityItemProperties& properties = propertiesItr->second;
            std::map<QUuid, EntityItemPointer>::iterator entityItr = _entities.find(id);
            EntityItemPointer entity;
            if (entityItr != _entities.end()) {
                entity = entityItr->second;
                entity->setProperties(properties);
            } else {
                entity = EntityTypes::constructEntityItem(id, properties);
            }
            if (entity) {
                // build outgoing payload
                OctreePacketData packetData(false, AvatarTraits::MAXIMUM_TRAIT_SIZE);
                EncodeBitstreamParams params;
                EntityTreeElementExtraEncodeDataPointer extra { nullptr };
                OctreeElement::AppendState appendState = entity->appendEntityData(&packetData, params, extra);

                if (appendState == OctreeElement::COMPLETED) {
                    _entities[id] = entity;
                    QByteArray tempArray((const char*)packetData.getUncompressedData(), packetData.getUncompressedSize());
                    storeAvatarEntityDataPayload(id, tempArray);
                } else {
                    // payload doesn't fit
                    entityItr = _entities.find(id);
                    if (entityItr != _entities.end()) {
                        _entities.erase(entityItr);
                        idsToClear.push_back(id);
                    }

                }
            }
            ++propertiesItr;
        }
    });

    // clear deleted traits
    for (const auto& id : idsToClear) {
        clearAvatarEntityInternal(id);
    }
}

void ScriptableAvatar::updateAvatarEntity(const QUuid& entityID, const QByteArray& entityData) {
    if (entityData.isNull()) {
        // interpret this as a DELETE
        std::map<QUuid, EntityItemPointer>::iterator itr = _entities.find(entityID);
        if (itr != _entities.end()) {
            _entities.erase(itr);
            clearAvatarEntityInternal(entityID);
        }
        return;
    }

    EntityItemPointer entity;
    EntityItemProperties properties;
    {
        // TODO: checking how often this happens and what is the performance impact of having the script engine on separate thread
        // If it's happening often, a method to move HelperScriptEngine into the current thread would be a good idea
        bool result = _helperScriptEngine.runWithResult<bool> ( [&]() {
            return EntityItemProperties::blobToProperties(*_helperScriptEngine.get(), entityData, properties);
        });
        if (!result) {
            // entityData is corrupt
            return;
        }
    }

    std::map<QUuid, EntityItemPointer>::iterator itr = _entities.find(entityID);
    if (itr == _entities.end()) {
        // this is an ADD
        entity = EntityTypes::constructEntityItem(entityID, properties);
        if (entity) {
            OctreePacketData packetData(false, AvatarTraits::MAXIMUM_TRAIT_SIZE);
            EncodeBitstreamParams params;
            EntityTreeElementExtraEncodeDataPointer extra { nullptr };
            OctreeElement::AppendState appendState = entity->appendEntityData(&packetData, params, extra);

            if (appendState == OctreeElement::COMPLETED) {
                _entities[entityID] = entity;
                QByteArray tempArray((const char*)packetData.getUncompressedData(), packetData.getUncompressedSize());
                storeAvatarEntityDataPayload(entityID, tempArray);
            }
        }
    } else {
        // this is an UPDATE
        entity = itr->second;
        bool somethingChanged = entity->setProperties(properties);
        if (somethingChanged) {
            OctreePacketData packetData(false, AvatarTraits::MAXIMUM_TRAIT_SIZE);
            EncodeBitstreamParams params;
            EntityTreeElementExtraEncodeDataPointer extra { nullptr };
            OctreeElement::AppendState appendState = entity->appendEntityData(&packetData, params, extra);

            if (appendState == OctreeElement::COMPLETED) {
                QByteArray tempArray((const char*)packetData.getUncompressedData(), packetData.getUncompressedSize());
                storeAvatarEntityDataPayload(entityID, tempArray);
            }
        }
    }
}
