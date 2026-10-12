//
//  piano-bench-client.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano bench's client entity script, run by Interface for each visitor. A click on the
//  bench asks its server script (piano-bench-entity.mjs) for the seat; when granted, the avatar
//  sits with the engine's own sit (MyAvatar.beginSit) with its hips over the bench's `seat`
//  joint, facing the keyboard, and the piano's client script opens the desk display. Clicking
//  again, or holding a movement key, stands the avatar up at the bench's `stand_up` joint.
//
//  The bench is a child of the piano (parentID). Its joints exist only once Interface has
//  loaded the bench's model, so every pose is read here, never on the entity script server.
//

import { parseBenchConfig, sitPose, standPose, StandGesture } from "./piano-bench-core.mjs";

const SEAT_JOINT = "seat";
const STAND_JOINT = "stand_up";
const MAX_SIT_DISTANCE = 5;
const REPLY_TIMEOUT_MS = 3000;
const WATCH_MS = 100;
// beginSit is queued to Interface's main thread, so the seated state and the avatar's position
// lag the call; the checks that end the sit wait this long
const SETTLE_MS = 1000;
// The hips are pinned; an avatar this far from the pin was moved away (a teleport, a go-to)
const MOVED_AWAY_DISTANCE = 0.5;
// The Create app's flag: a click while editing selects the bench, it does not sit
const EDIT_SETTING = "io.highfidelity.isEditing";
const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
const ORIGIN = { x: 0, y: 0, z: 0 };
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

export default class PianoBenchClient {
    remotelyCallable = ["seatGranted", "seatRefused"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Uuid} */ pianoID;
    /** @type {import("./piano-bench-core.mjs").BenchConfig} */ config;
    seated = false;
    pending = false;
    feetToPosition = 0;
    settleUntil = 0;
    /** @type {Vec3 | null} */ hipsPin = null;
    /** @type {StandGesture | null} */ gesture = null;
    /** @type {number | null} */ replyTimer = null;
    /** @type {number | null} */ renewTimer = null;
    /** @type {number | null} */ watchTimer = null;
    standUpHandler = () => this.standUp();

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["parentID", "userData"]);
        this.pianoID = properties.parentID || NULL_UUID;
        if (this.pianoID === NULL_UUID) {
            print("Piano bench " + entityID + ": not parented to a piano, so sitting opens no desk display");
        }
        const { config, problems } = parseBenchConfig(properties.userData);
        this.config = config;
        problems.forEach(problem => print("Piano bench " + entityID + ": " + problem));
    }

    unload() {
        this.standUp();
        this.clearReplyTimer();
    }

    /** @param {Uuid} _id @param {PointerEvent} event */
    clickDownOnEntity(_id, event) {
        if (!event.isPrimaryButton || Settings.getValue(EDIT_SETTING, false)) {
            return;
        }
        if (this.seated) {
            this.standUp();
        } else {
            this.requestSeat();
        }
    }

    requestSeat() {
        if (this.pending || MyAvatar.isSeated()) {
            return;
        }
        const seat = this.joint(SEAT_JOINT);
        if (seat === null || this.joint(STAND_JOINT) === null) {
            print("Piano bench " + this.entityID + ": the bench model has no " + SEAT_JOINT + " and " + STAND_JOINT
                + " joints (or is not loaded yet); not sitting");
            return;
        }
        if (Vec3.distance(MyAvatar.position, seat.position) > MAX_SIT_DISTANCE) {
            return;
        }
        this.pending = true;
        Entities.callEntityServerMethod(this.entityID, "claim", []);
        this.replyTimer = Script.setTimeout(() => {
            this.replyTimer = null;
            this.pending = false;
            print("Piano bench " + this.entityID + ": the bench's server script did not answer; is serverScripts set?");
        }, REPLY_TIMEOUT_MS);
    }

    // Called by the bench's server script in answer to `claim`.

    seatGranted() {
        if (this.seated) {
            return; // a renewal
        }
        if (!this.pending) {
            // A grant that arrived after the reply timeout: give the seat back
            Entities.callEntityServerMethod(this.entityID, "release", []);
            return;
        }
        this.pending = false;
        this.clearReplyTimer();
        this.sit();
    }

    seatRefused() {
        if (this.seated) {
            print("Piano bench " + this.entityID + ": the seat lease was lost, standing up");
            this.standUp();
            return;
        }
        this.pending = false;
        this.clearReplyTimer();
        print("Piano bench " + this.entityID + ": someone is already sitting here");
    }

    sit() {
        const seat = this.joint(SEAT_JOINT);
        if (seat === null) {
            Entities.callEntityServerMethod(this.entityID, "release", []);
            return;
        }
        // Read while standing: endSit places the avatar's position, which is this far above its feet
        this.feetToPosition = MyAvatar.position.y - MyAvatar.feetPosition.y;
        const pose = sitPose(seat.position, seat.rotation, MyAvatar.getHeight(), this.config.hipsAboveSeat);
        MyAvatar.beginSit(pose.position, pose.rotation);
        this.seated = true;
        this.hipsPin = pose.position;
        this.settleUntil = Date.now() + SETTLE_MS;
        this.gesture = new StandGesture(this.config.standHoldMs);
        this.renewTimer = Script.setInterval(() => Entities.callEntityServerMethod(this.entityID, "claim", []),
            this.config.leaseMs / 3);
        this.watchTimer = Script.setInterval(() => this.watch(), WATCH_MS);
        // A new avatar model or scale would leave the hips pinned at the old avatar's height
        MyAvatar.onLoadComplete.connect(this.standUpHandler);
        MyAvatar.scaleChanged.connect(this.standUpHandler);
        Entities.callEntityMethod(this.pianoID, "openDisplay", []);
    }

    standUp() {
        if (!this.seated) {
            return;
        }
        this.seated = false;
        for (const timer of [this.renewTimer, this.watchTimer]) {
            if (timer !== null) {
                Script.clearInterval(timer);
            }
        }
        this.renewTimer = this.watchTimer = null;
        MyAvatar.onLoadComplete.disconnect(this.standUpHandler);
        MyAvatar.scaleChanged.disconnect(this.standUpHandler);
        Entities.callEntityMethod(this.pianoID, "closeDisplay", []);
        Entities.callEntityServerMethod(this.entityID, "release", []);
        const seat = this.joint(SEAT_JOINT);
        const stand = this.joint(STAND_JOINT);
        if (seat !== null && stand !== null && this.hipsPin !== null
            && Vec3.distance(MyAvatar.position, this.hipsPin) <= MOVED_AWAY_DISTANCE) {
            const pose = standPose(stand.position, seat.rotation, this.feetToPosition);
            MyAvatar.endSit(pose.position, pose.rotation);
        } else {
            // Moved away, or the bench went: stand where the avatar is
            MyAvatar.endSit(MyAvatar.position, MyAvatar.orientation);
        }
        this.hipsPin = null;
        this.gesture = null;
    }

    watch() {
        if (Date.now() < this.settleUntil) {
            return;
        }
        if (!MyAvatar.isSeated() || (this.hipsPin !== null
            && Vec3.distance(MyAvatar.position, this.hipsPin) > MOVED_AWAY_DISTANCE)) {
            // Something else ended the sit or moved the avatar off the bench
            this.standUp();
            return;
        }
        // beginSit disables these keys for walking, but their raw values still read
        // (MyAvatar::getDriveKey / getRawDriveKey). Sideways turns the seated avatar, so it is not one.
        const pressed = [DriveKeys.TRANSLATE_Z, DriveKeys.TRANSLATE_Y, DriveKeys.STEP_TRANSLATE_Z]
            .some(key => MyAvatar.getRawDriveKey(key) !== 0);
        if (this.gesture !== null && this.gesture.update(Date.now(), pressed)) {
            this.standUp();
        }
    }

    /**
     * @param {string} name
     * @returns {{ position: Vec3, rotation: Quat } | null} the bench joint's world pose
     */
    joint(name) {
        const index = Entities.getJointIndex(this.entityID, name);
        if (index < 0) {
            return null;
        }
        // The same joint transform the engine uses for a child entity parented to that joint
        return {
            position: Entities.localToWorldPosition(ORIGIN, this.entityID, index),
            rotation: Entities.localToWorldRotation(IDENTITY, this.entityID, index)
        };
    }

    clearReplyTimer() {
        if (this.replyTimer !== null) {
            Script.clearTimeout(this.replyTimer);
            this.replyTimer = null;
        }
    }
}
