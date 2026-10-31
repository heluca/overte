//
//  seat-client.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's client entity script, run by Interface for each visitor. A click on the model asks
//  its server script (seat-server.js) for a seat, naming the model's seats nearest first; when
//  one is granted, the avatar sits with the engine's own sit (MyAvatar.beginSit) with its hips
//  over that seat's `seat_N` joint, facing the way the joint does. Clicking again, or holding a
//  movement key, stands the avatar up at the matching `stand_N` joint.
//
//  The joints exist only once Interface has loaded the model, so every pose is read here, never
//  on the entity script server.
//

import {
    parseSeatConfig, seatJoint, standJointOf, nearestFirst, sitPose, standPose, StandGesture, MAX_SEATS
} from "./seat-core.mjs";

const MAX_SIT_DISTANCE = 5;
const REPLY_TIMEOUT_MS = 3000;
const WATCH_MS = 100;
// beginSit is queued to Interface's main thread, so the seated state and the avatar's position
// lag the call; the checks that end the sit wait this long
const SETTLE_MS = 1000;
// Used only if a grant carries no lease; seat-server.js always sends one
const FALLBACK_LEASE_MS = 15000;
// The hips are pinned; an avatar this far from the pin was moved away (a teleport, a go-to)
const MOVED_AWAY_DISTANCE = 0.5;
// The Create app's flag: a click while editing selects the model, it does not sit
const EDIT_SETTING = "io.highfidelity.isEditing";
const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
const ORIGIN = { x: 0, y: 0, z: 0 };
/** @type {Record<string, string>} */
const REFUSALS = {
    taken: "every seat is taken",
    "no-seat": "the server took none of the seats asked for"
};

export default class SeatClient {
    remotelyCallable = ["seatGranted", "seatRefused"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {import("./seat-core.mjs").SeatConfig} */ config;
    // Seats already reported as having no stand joint
    /** @type {Set<string>} */ reported;
    // The `seat_N` this avatar sits on
    /** @type {string | null} */ heldSeat = null;
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
        this.reported = new Set();
        const { config, problems } = parseSeatConfig(Entities.getEntityProperties(entityID, ["userData"]).userData);
        this.config = config;
        problems.forEach(problem => print("Seat " + entityID + ": " + problem));
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
        if (this.heldSeat !== null) {
            this.standUp();
        } else {
            this.requestSeat();
        }
    }

    requestSeat() {
        if (this.pending || MyAvatar.isSeated()) {
            return;
        }
        const seats = this.seats();
        if (seats.length === 0) {
            print("Seat " + this.entityID + ": the model has no seat_1/stand_1 joints (or is not loaded yet); not sitting");
            return;
        }
        const ordered = nearestFirst(seats, MyAvatar.position);
        if (Vec3.distance(MyAvatar.position, ordered[0].position) > MAX_SIT_DISTANCE) {
            return;
        }
        this.pending = true;
        Entities.callEntityServerMethod(this.entityID, "claim", ordered.map(seat => seat.name));
        this.replyTimer = Script.setTimeout(() => {
            this.replyTimer = null;
            this.pending = false;
            print("Seat " + this.entityID + ": the seat's server script did not answer; is serverScripts set?");
        }, REPLY_TIMEOUT_MS);
    }

    // Called by the seat's server script in answer to `claim`.

    /** @param {Uuid} _id @param {string[]} params [seatName, leaseMs] */
    seatGranted(_id, params) {
        if (this.heldSeat !== null) {
            return; // a renewal: the server always answers a holder with the seat it holds
        }
        if (!this.pending) {
            // A grant that arrived after the reply timeout: give the seat back
            Entities.callEntityServerMethod(this.entityID, "release", []);
            return;
        }
        this.pending = false;
        this.clearReplyTimer();
        const leaseMs = Number(params && params[1]);
        this.sit(String(params && params[0]), leaseMs > 0 ? leaseMs : FALLBACK_LEASE_MS);
    }

    /** @param {Uuid} _id @param {string[]} params [reason] */
    seatRefused(_id, params) {
        if (this.heldSeat !== null) {
            print("Seat " + this.entityID + ": the seat lease was lost, standing up");
            this.standUp();
            return;
        }
        if (!this.pending) {
            return;
        }
        this.pending = false;
        this.clearReplyTimer();
        const reason = String(params && params[0]);
        print("Seat " + this.entityID + ": not sitting: " + (REFUSALS[reason] || reason));
    }

    /** @param {string} seatName @param {number} leaseMs */
    sit(seatName, leaseMs) {
        const seat = this.joint(seatName);
        if (seat === null) {
            Entities.callEntityServerMethod(this.entityID, "release", []);
            return;
        }
        // Read while standing: endSit places the avatar's position, which is this far above its feet
        this.feetToPosition = MyAvatar.position.y - MyAvatar.feetPosition.y;
        const pose = sitPose(seat.position, seat.rotation, MyAvatar.getHeight(), this.config.hipsAboveSeat);
        MyAvatar.beginSit(pose.position, pose.rotation);
        this.heldSeat = seatName;
        this.hipsPin = pose.position;
        this.settleUntil = Date.now() + SETTLE_MS;
        this.gesture = new StandGesture(this.config.standHoldMs);
        // Renewed a third of the way through, so one lost renewal does not lose the seat
        this.renewTimer = Script.setInterval(
            () => Entities.callEntityServerMethod(this.entityID, "claim", [String(this.heldSeat)]), leaseMs / 3);
        this.watchTimer = Script.setInterval(() => this.watch(), WATCH_MS);
        // A new avatar model or scale would leave the hips pinned at the old avatar's height
        MyAvatar.onLoadComplete.connect(this.standUpHandler);
        MyAvatar.scaleChanged.connect(this.standUpHandler);
    }

    standUp() {
        const seatName = this.heldSeat;
        if (seatName === null) {
            return;
        }
        this.heldSeat = null;
        for (const timer of [this.renewTimer, this.watchTimer]) {
            if (timer !== null) {
                Script.clearInterval(timer);
            }
        }
        this.renewTimer = this.watchTimer = null;
        MyAvatar.onLoadComplete.disconnect(this.standUpHandler);
        MyAvatar.scaleChanged.disconnect(this.standUpHandler);
        Entities.callEntityServerMethod(this.entityID, "release", []);
        const seat = this.joint(seatName);
        const stand = this.joint(standJointOf(seatName));
        if (seat !== null && stand !== null && this.hipsPin !== null
            && Vec3.distance(MyAvatar.position, this.hipsPin) <= MOVED_AWAY_DISTANCE) {
            const pose = standPose(stand.position, seat.rotation, this.feetToPosition);
            MyAvatar.endSit(pose.position, pose.rotation);
        } else {
            // Moved away, or the model went: stand where the avatar is
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
            // Something else ended the sit or moved the avatar off the seat
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
     * The model's seats, `seat_1` upwards while the joint exists. A seat without its `stand_N`
     * is left out, since there would be nowhere to stand up.
     * @returns {{ name: string, position: Vec3 }[]}
     */
    seats() {
        /** @type {{ name: string, position: Vec3 }[]} */
        const seats = [];
        for (let index = 1; index <= MAX_SEATS; index++) {
            const name = seatJoint(index);
            const seat = this.joint(name);
            if (seat === null) {
                break;
            }
            const stand = standJointOf(name);
            if (Entities.getJointIndex(this.entityID, stand) < 0) {
                if (!this.reported.has(name)) {
                    this.reported.add(name);
                    print("Seat " + this.entityID + ": " + name + " has no " + stand + " joint; leaving it out");
                }
                continue;
            }
            seats.push({ name, position: seat.position });
        }
        return seats;
    }

    /**
     * @param {string} name
     * @returns {{ position: Vec3, rotation: Quat } | null} the model joint's world pose
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
