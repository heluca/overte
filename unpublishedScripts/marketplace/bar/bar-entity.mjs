//
//  bar-entity.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bar's serverScripts, run by the entity script server. Visitors and Morpheus bodies order
//  from the menu in userData through the remotely callable methods; only the bartender named in
//  privateUserData.staff serves, and a served glass is set on the counter in front of the orderer. The
//  bar posts every change on its Messages channel and never listens on it, mirrors its status
//  into userData.status, and holds every caller to a rate limit (Morpheus ADRs 0003 and 0016).
//
//  The engine shares one module instance per URL between every bar that uses it, so all state
//  lives on the class instance; module scope holds constants only.
//

import { BarCore, boxCentre, counterOf, glassSpot, glassProperties } from "./bar-core.mjs";
import { parseObject, parseConfig, staffOf } from "./bar-config.mjs";
import { sameSession } from "./bar-orders.mjs";

const TICK_MS = 2000;
const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
const CENTRE = { x: 0.5, y: 0.5, z: 0.5 };
const NULL_UUID = /^\{?0{8}-0{4}-0{4}-0{4}-0{12}\}?$/;

export default class BarEntity {
    remotelyCallable = ["order", "serve", "cancel", "status", "menu"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {import("./bar-config.mjs").Config} */ config;
    /** @type {BarCore} */ core;
    /** @type {number} */ timer;
    locked = false;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = /** @type {Partial<Entities.EntityProperties_Model>} */ (
            Entities.getEntityProperties(entityID, ["userData", "locked"]));
        const { config, problems } = parseConfig(parseObject(properties.userData), entityID);
        problems.forEach(problem => this.log(problem));
        this.config = config;
        // A locked entity takes no edit but to `locked`, so the status could not be mirrored.
        this.locked = properties.locked === true;
        if (this.locked) {
            this.log("locked, so the status is not mirrored into userData; orders still work and post on " + config.channel);
        }
        this.core = new BarCore({
            now: () => Date.now(),
            publish: status => this.publish(status),
            announce: message => Messages.sendMessage(config.channel, JSON.stringify(message)),
            log: text => this.log(text),
            // privateUserData, which guests cannot read or write, so no guest can name the bartender
            staff: () => staffOf(parseObject(Entities.getEntityProperties(entityID, ["privateUserData"]).privateUserData)),
            avatars: () => AvatarList.getAvatarIdentifiers().map(String),
            sessionName: session => AvatarList.getAvatar(session).sessionDisplayName || "",
            spawnGlass: order => this.spawnGlass(order),
            deleteEntity: id => Entities.deleteEntity(id)
        }, config);
        this.core.start();
        this.timer = Script.setInterval(() => this.core.tick(), TICK_MS);
    }

    unload() {
        Script.clearInterval(this.timer);
        this.core.stop();
    }

    // Remotely callable. The engine passes (entityID, params: string[]) and stamps the caller's
    // authenticated session id in Script.remoteCallerID for the synchronous part only.
    // Entities.callEntityServerMethod returns nothing, so every answer is a channel post.

    /** @param {Uuid} _id @param {string[]} params [drink] a menu item's name */
    order(_id, params) {
        this.core.order(Script.remoteCallerID, params && params[0]);
    }

    /** @param {Uuid} _id @param {string[]} params [orderId] */
    serve(_id, params) {
        this.core.serve(Script.remoteCallerID, params && params[0]);
    }

    /** @param {Uuid} _id @param {string[]} params [orderId] */
    cancel(_id, params) {
        this.core.cancel(Script.remoteCallerID, params && params[0]);
    }

    status() {
        this.core.status(Script.remoteCallerID);
    }

    menu() {
        this.core.menu(Script.remoteCallerID);
    }

    /** @param {string} text */
    log(text) {
        print("Bar " + this.entityID + ": " + text);
    }

    /**
     * Writes the mirror into userData.status, keeping every other key, read afresh so an edit
     * made since is kept. Nothing else writes userData at run time: the staff handshake is in
     * privateUserData.
     * @param {import("./bar-core.mjs").Status} status
     */
    publish(status) {
        if (this.locked) {
            return;
        }
        const userData = parseObject(Entities.getEntityProperties(this.entityID, ["userData"]).userData);
        userData.status = status;
        Entities.editEntity(this.entityID, { userData: JSON.stringify(userData) });
    }

    /**
     * Sets the order's glass on the counter at the point nearest the orderer's avatar (the
     * entity script server has AvatarList), or mid-counter when the orderer has left.
     * @param {import("./bar-orders.mjs").Order} order
     * @returns {string | null} the glass's entity id
     */
    spawnGlass(order) {
        const bar = /** @type {Partial<Entities.EntityProperties_Model>} */ (
            Entities.getEntityProperties(this.entityID, ["position", "rotation", "dimensions", "registrationPoint"]));
        const dimensions = /** @type {Vec3} */ (bar.dimensions);
        const rotation = bar.rotation || IDENTITY;
        const position = boxCentre(/** @type {Vec3} */ (bar.position), rotation, dimensions, bar.registrationPoint || CENTRE);
        const here = AvatarList.getAvatarIdentifiers().some(id => sameSession(String(id), order.by));
        const orderer = here ? AvatarList.getAvatar(order.by).position : null;
        const spot = glassSpot({ position, rotation }, counterOf(this.config.counter, dimensions), orderer);
        const id = Entities.addEntity(/** @type {Entities.EntityProperties} */ (
            glassProperties(order, this.config.glasses, spot, this.config.glassLifetimeS, this.entityID)));
        return id && !NULL_UUID.test(id) ? id : null;
    }
}
