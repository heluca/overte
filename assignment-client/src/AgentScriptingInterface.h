//
//  AgentScriptingInterface.h
//  assignment-client/src
//
//  Created by Thijs Wenker on 7/23/18.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#pragma once

#ifndef hifi_AgentScriptingInterface_h
#define hifi_AgentScriptingInterface_h

#include <QObject>
#include <QVariantMap>

#include "Agent.h"

/*@jsdoc
 * The <code>Agent</code> API enables an assignment client to emulate an avatar. Setting <code>isAvatar = true</code> connects 
 * the assignment client to the avatar and audio mixers, and enables the {@link Avatar} API to be used.
 *
 * @namespace Agent
 *
 * @hifi-assignment-client
 *
 * @property {boolean} isAvatar - <code>true</code> if the assignment client script is emulating an avatar, otherwise 
 *     <code>false</code>.
 * @property {boolean} isPlayingAvatarSound - <code>true</code> if the script has a sound to play, otherwise <code>false</code>. 
 *     Sounds are played when <code>isAvatar</code> is <code>true</code>, from the position and with the orientation of the 
 *     scripted avatar's head. <em>Read-only.</em>
 * @property {boolean} isListeningToAudioStream - <code>true</code> if the agent is "listening" to the audio stream from the 
 *     domain, otherwise <code>false</code>.
 * @property {boolean} isNoiseGateEnabled - <code>true</code> if the noise gate is enabled, otherwise <code>false</code>. When 
 * enabled, the input audio stream is blocked (fully attenuated) if it falls below an adaptive threshold.
 * @property {number} lastReceivedAudioLoudness - The current loudness of the audio input. Nominal range [<code>0.0</code> (no 
 *     sound) &ndash; <code>1.0</code> (the onset of clipping)]. <em>Read-only.</em>
 * @property {Uuid} sessionUUID - The unique ID associated with the agent's current session in the domain. <em>Read-only.</em>
 * @property {string[]} audioCodecs - The audio codecs the agent offers the audio mixer, which picks one by the domain's
 *     codec preference order. Defaults to every codec the assignment client has, e.g., <code>["opus", "pcm", "zlib"]</code>.
 *     Names the assignment client does not have are dropped with a warning. An empty array, or one with no name the
 *     assignment client has, offers every codec.
 *     Setting the property negotiates the codec again if the agent is connected to an audio mixer. Set it to
 *     <code>["pcm"]</code> for uncompressed audio from one agent while Interface users keep Opus.
 * @property {Vec3} audioPositionOffset - Where the agent's audio comes from and is heard, relative to the avatar's
 *     position and in the avatar's frame. The offset is rotated by the avatar's orientation and added to
 *     <code>Avatar.position</code> in the position the agent sends the audio mixer, both for the audio it plays and for
 *     where it listens. Defaults to <code>Vec3.ZERO</code>, which is the avatar's position (its hips). Set it to, e.g.,
 *     <code>{ x: 0, y: 0.6, z: 0 }</code> to put the mouth and ears at head height.
 * @property {boolean} isAudioSinkActive - <code>true</code> from a successful {@link Agent.startAudioSink|startAudioSink}
 *     call until {@link Agent.audioSinkFinished|audioSinkFinished} is triggered, including while the WebSocket
 *     connects, otherwise <code>false</code>. <em>Read-only.</em>
 */
class AgentScriptingInterface : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isAvatar READ getIsAvatar WRITE setIsAvatar)
    Q_PROPERTY(bool isPlayingAvatarSound READ isPlayingAvatarSound)
    Q_PROPERTY(bool isListeningToAudioStream READ isListeningToAudioStream WRITE setIsListeningToAudioStream)
    Q_PROPERTY(bool isNoiseGateEnabled READ isNoiseGateEnabled WRITE setIsNoiseGateEnabled)
    Q_PROPERTY(float lastReceivedAudioLoudness READ getLastReceivedAudioLoudness)
    Q_PROPERTY(QUuid sessionUUID READ getSessionUUID)
    Q_PROPERTY(QStringList audioCodecs READ getAudioCodecs WRITE setAudioCodecs)
    Q_PROPERTY(glm::vec3 audioPositionOffset READ getAudioPositionOffset WRITE setAudioPositionOffset)
    Q_PROPERTY(bool isAudioSinkActive READ isAudioSinkActive)

public:
    AgentScriptingInterface(Agent* agent);

    bool isPlayingAvatarSound() const { return _agent->isPlayingAvatarSound(); }

    bool isListeningToAudioStream() const { return _agent->isListeningToAudioStream(); }
    void setIsListeningToAudioStream(bool isListeningToAudioStream) const { _agent->setIsListeningToAudioStream(isListeningToAudioStream); }

    bool isNoiseGateEnabled() const { return _agent->isNoiseGateEnabled(); }
    void setIsNoiseGateEnabled(bool isNoiseGateEnabled) const { _agent->setIsNoiseGateEnabled(isNoiseGateEnabled); }

    float getLastReceivedAudioLoudness() const { return _agent->getLastReceivedAudioLoudness(); }
    QUuid getSessionUUID() const { return _agent->getSessionUUID(); }

    QStringList getAudioCodecs() const { return _agent->getAudioCodecs(); }
    void setAudioCodecs(const QStringList& audioCodecs) const { _agent->setAudioCodecs(audioCodecs); }

    glm::vec3 getAudioPositionOffset() const { return _agent->getAudioPositionOffset(); }
    void setAudioPositionOffset(const glm::vec3& offset) const { _agent->setAudioPositionOffset(offset); }

    bool isAudioSinkActive() const { return _agent->isAudioSinkActive(); }

public slots:
    /*@jsdoc
     * Sets whether the script should emulate an avatar.
     * @function Agent.setIsAvatar
     * @param {boolean} isAvatar - <code>true</code> if the script emulates an avatar, otherwise <code>false</code>.
     * @example <caption>Make an assignment client script emulate an avatar.</caption>
     * (function () {
     *     Agent.setIsAvatar(true);
     *     Avatar.displayName = "AC avatar";
     *     print("Position: " + JSON.stringify(Avatar.position));  // 0, 0, 0
     * }());
     */
    void setIsAvatar(bool isAvatar) const { _agent->setIsAvatar(isAvatar); }

    /*@jsdoc
     * Checks whether the script is emulating an avatar.
     * @function Agent.getIsAvatar
     * @returns {boolean} <code>true</code> if the script is emulating an avatar, otherwise <code>false</code>.
     * @example <caption>Check whether the agent is emulating an avatar.</caption>
     * (function () {
     *     print("Agent is avatar: " + Agent.getIsAvatar());
     *     print("Agent is avatar: " + Agent.isAvatar); // Same result.
     * }());
     */
    bool getIsAvatar() const { return _agent->isAvatar(); }

    /*@jsdoc
     * Plays a sound from the position and with the orientation of the emulated avatar's head. No sound is played unless 
     * <code>isAvatar == true</code>.
     * @function Agent.playAvatarSound
     * @param {SoundObject} avatarSound - The sound played.
     * @example <caption>Play a sound from an emulated avatar.</caption>
     * (function () {
     *     Agent.isAvatar = true;
     *     var sound = SoundCache.getSound(Script.resourcesPath() + "sounds/sample.wav");
     *     Script.setTimeout(function () { // Give the sound time to load.
     *         Agent.playAvatarSound(sound);
     *     }, 1000);
     * }());
     */
    void playAvatarSound(SharedSoundPointer avatarSound) const { _agent->playAvatarSound(avatarSound); }

    /*@jsdoc
     * Sends the audio the agent hears to a WebSocket server, as binary messages, until the server closes the connection
     * or the script calls {@link Agent.stopAudioSink|stopAudioSink}. This is the agent's own spatial mix from the audio
     * mixer, so its other listeners' voices come from where their avatars are relative to this one.
     * <p>Each message is <code>framesPerMessage</code> frames of 10 ms of 24000 Hz stereo audio: interleaved signed
     * 16-bit little-endian samples, left first, with no header. At the default of 2 frames a message is 1920 bytes.</p>
     * <p>The mixer sends a mix only while both <code>isAvatar</code> and <code>isListeningToAudioStream</code> are
     * <code>true</code>; at other times nothing is sent. The mix leaves out the agent's own sound, such as
     * {@link Agent.playAvatarSound|playAvatarSound}, as the mixer does for every listener. Silence from the mixer is
     * sent as frames of zeros, so the messages keep real time.</p>
     * <p>If the server reads more slowly than the audio arrives, the connection's operating-system buffers fill first;
     * after that up to about 200 ms of audio waits in the sink and the oldest frames are dropped. Writing to the sink
     * never delays the agent.</p>
     * <p>The upgrade request carries an <code>Authorization</code> header when a script credential of the domain
     * covers the URL with its scheme read as http (for ws) or https (for wss). Script credentials only accept http
     * prefixes on a loopback host, so use wss for a server on another host.</p>
     * <p>Starting a sink while one is active stops the active one first. Reconnecting is up to the script: call
     * <code>startAudioSink</code> again from {@link Agent.audioSinkFinished|audioSinkFinished}, with a back-off.</p>
     * @function Agent.startAudioSink
     * @param {string} url - The WebSocket URL, <code>ws://</code> or <code>wss://</code>.
     * @param {object} [options] - Options.
     * @param {number} [options.framesPerMessage=2] - The 10 ms frames in each message, <code>1</code> &ndash;
     *     <code>10</code>.
     * @returns {boolean} <code>true</code> if the sink is connecting, <code>false</code> if the URL is not ws or wss or
     *     <code>framesPerMessage</code> is out of range.
     * @example <caption>Send what the agent hears to a local recorder, reconnecting after a second.</caption>
     * Agent.isAvatar = true;
     * Agent.isListeningToAudioStream = true;
     * function connect() {
     *     Agent.startAudioSink("ws://127.0.0.1:9000/ears", { framesPerMessage: 2 });
     * }
     * Agent.audioSinkFinished.connect(function (reason) {
     *     print("Audio sink finished: " + reason);
     *     if (reason !== "stopped") {
     *         Script.setTimeout(connect, 1000);
     *     }
     * });
     * connect();
     */
    bool startAudioSink(const QString& url, const QVariantMap& options = QVariantMap()) const;

    /*@jsdoc
     * Stops sending the audio the agent hears, if {@link Agent.startAudioSink|startAudioSink} is active, closing the
     * WebSocket. {@link Agent.audioSinkFinished|audioSinkFinished} is triggered with <code>"stopped"</code>.
     * @function Agent.stopAudioSink
     */
    void stopAudioSink() const { _agent->stopAudioSink(); }

signals:
    /*@jsdoc
     * Triggered once when the audio sink started by {@link Agent.startAudioSink|startAudioSink} ends. It is not
     * triggered after the script has stopped.
     * @function Agent.audioSinkFinished
     * @param {string} reason - <code>"closed"</code> if the server closed the connection, <code>"http &lt;status&gt;"</code>
     *     (e.g., <code>"http 401"</code>) if the server refused the WebSocket upgrade, <code>"error: &lt;description&gt;"</code>
     *     if the connection failed otherwise, or <code>"stopped"</code> if the script stopped it.
     * @returns {Signal}
     */
    void audioSinkFinished(const QString& reason);

private:
    Agent* _agent;

};


#endif // hifi_AgentScriptingInterface_h
