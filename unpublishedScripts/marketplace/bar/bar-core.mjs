//
//  bar-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bar without the engine: what each remote call does, the status mirror and where a served
//  glass stands. The configuration is bar-config.mjs's; the order book, the staff check and the
//  rate limiter are bar-orders.mjs's. Every side effect goes through an injected world adapter,
//  so the core runs under Node with a fake world and a fake clock:
//
//      now()               -> epoch milliseconds
//      publish(status)     -> the status mirror changed: store it in the bar's userData.status
//      announce(message)   -> send it on the bar's Messages channel; nothing is stored
//      log(text)           -> the script log; the entry names the bar
//      staff()             -> userData.staff as it stands now (Morpheus edits it), or null
//      avatars()           -> the session ids of the avatars in the domain
//      namesOf(session)    -> that avatar's [displayName, sessionDisplayName], "" when unknown
//      spawnGlass(order)   -> creates the served glass; its entity id, or null
//      deleteEntity(id)
//
//  Callers are identified only by the `caller` argument, which the serverScripts entry takes
//  from Script.remoteCallerID (stamped by the entity script server from the authenticated
//  node). Nothing in a method's string arguments, and nothing on the channel, can name or
//  impersonate a session.
//

import { PARAM_LIMIT, cleanText, findItem } from "./bar-config.mjs";
import { OrderBook, RateLimiter, isCaller, isSession, isStaff, sameSession, sessionKey } from "./bar-orders.mjs";

export const PROTOCOL_VERSION = 1;
export const PLAYER_NAME_LIMIT = 32;
export const PLACEHOLDER_DIMENSIONS = { x: 0.4, y: 0.1, z: 0.01 };

/**
 * @typedef {import("./bar-config.mjs").Config} Config
 * @typedef {import("./bar-config.mjs").Counter} Counter
 * @typedef {import("./bar-config.mjs").Glass} Glass
 * @typedef {import("./bar-config.mjs").Vec3Like} Vec3Like
 * @typedef {import("./bar-orders.mjs").Order} Order
 * @typedef {{ x: number, y: number, z: number, w: number }} QuatLike
 * @typedef {{ v: number, state: "open" | "offline", channel: string, open: number, oldestAgeS: number | null,
 *     staffPresent: boolean, lastServed: { id: string, drink: string, by: string, at: number } | null,
 *     at: number }} Status
 * @typedef {{ type: string } & Record<string, unknown>} Message
 * @typedef {{ now(): number, publish(status: Status): void, announce(message: Message): void,
 *     log(text: string): void, staff(): string | null, avatars(): string[], namesOf(session: string): string[],
 *     spawnGlass(order: Order): string | null, deleteEntity(id: string): void }} World
 */


/**
 * @param {QuatLike} q
 * @param {Vec3Like} v
 * @returns {Vec3Like} v rotated by q
 */
export function rotate(q, v) {
    // v + 2w(u x v) + 2u x (u x v), u = q's vector part
    const tx = 2 * (q.y * v.z - q.z * v.y);
    const ty = 2 * (q.z * v.x - q.x * v.z);
    const tz = 2 * (q.x * v.y - q.y * v.x);
    return {
        x: v.x + q.w * tx + (q.y * tz - q.z * ty),
        y: v.y + q.w * ty + (q.z * tx - q.x * tz),
        z: v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

/**
 * The counter in the bar's frame (relative to its position, which Overte puts at the centre of
 * its box): userData.counter, else the top of the box along its x axis.
 * @param {Counter | null} counter
 * @param {Vec3Like} dimensions the bar's
 * @returns {Counter}
 */
export function counterOf(counter, dimensions) {
    return counter || { y: dimensions.y / 2, z: 0, halfLength: dimensions.x / 2 };
}

/**
 * Where a served glass stands: on the counter's line, at the point nearest the orderer, so it
 * lands in front of them; the middle of the counter when the orderer is not in AvatarList.
 * @param {{ position: Vec3Like, rotation: QuatLike }} bar
 * @param {Counter} counter
 * @param {Vec3Like | null} orderer the orderer's avatar position
 * @returns {Vec3Like} the world point on the counter's surface
 */
export function glassSpot(bar, counter, orderer) {
    let x = 0;
    if (orderer) {
        const { x: qx, y: qy, z: qz, w: qw } = bar.rotation;
        const local = rotate({ x: -qx, y: -qy, z: -qz, w: qw }, {
            x: orderer.x - bar.position.x, y: orderer.y - bar.position.y, z: orderer.z - bar.position.z
        });
        x = Math.max(-counter.halfLength, Math.min(counter.halfLength, local.x));
    }
    const offset = rotate(bar.rotation, { x, y: counter.y, z: counter.z });
    return { x: bar.position.x + offset.x, y: bar.position.y + offset.y, z: bar.position.z + offset.z };
}

/**
 * The served glass: a Model from userData.glasses for the item's glass, else a Text with the
 * drink's name until the glassware lands. Standing on `spot`, not parented, gone after `lifetimeS`.
 * @param {Order} order
 * @param {Record<string, Glass>} glasses
 * @param {Vec3Like} spot from glassSpot
 * @param {number} lifetimeS
 * @param {string} barID
 * @returns {Record<string, any>} the properties for Entities.addEntity
 */
export function glassProperties(order, glasses, spot, lifetimeS, barID) {
    const glass = order.glass ? glasses[order.glass] : undefined;
    const common = {
        name: "Bar.Glass " + order.drink,
        lifetime: lifetimeS,
        collisionless: true,
        grab: { grabbable: true },
        userData: JSON.stringify({ barGlass: { bar: barID, order: order.id, drink: order.drink, for: order.by } })
    };
    if (glass) {
        return {
            type: "Model",
            modelURL: glass.url,
            dimensions: { ...glass.dimensions },
            position: { x: spot.x, y: spot.y + glass.dimensions.y / 2, z: spot.z },
            ...common
        };
    }
    return {
        type: "Text",
        text: order.drink,
        dimensions: { ...PLACEHOLDER_DIMENSIONS },
        position: { x: spot.x, y: spot.y + PLACEHOLDER_DIMENSIONS.y / 2 + 0.02, z: spot.z },
        lineHeight: 0.06,
        billboardMode: "full",
        unlit: true,
        textColor: { red: 255, green: 236, blue: 200 },
        backgroundColor: { red: 40, green: 24, blue: 16 },
        backgroundAlpha: 0.7,
        ...common
    };
}

export class BarCore {
    orders = new OrderBook();
    /** @type {{ id: string, expiresAt: number }[]} the glasses out, oldest first */ glasses = [];
    /** @type {Status["lastServed"]} */ lastServed = null;
    staffPresent = false;
    seq = 0;

    /**
     * @param {World} world
     * @param {Config} config
     */
    constructor(world, config) {
        this.world = world;
        this.config = config;
        this.limiter = new RateLimiter(config.callsPerWindow, config.callWindowS * 1000);
        // Order ids stay unique across reloads
        this.idBase = world.now().toString(36);
    }

    /** First publication after the script (re)loads. Orders do not survive a reload. */
    start() {
        this.staffPresent = this.findStaff();
        this.publish();
    }

    /**
     * @param {string | undefined} caller
     * @param {unknown} drink a menu item's name, any case
     * @returns {boolean} whether the order was placed
     */
    order(caller, drink) {
        if (!this.admit(caller, "order")) {
            return false;
        }
        this.expire();
        const item = findItem(this.config.menu, drink);
        if (!item) {
            return this.reject(caller, "order", "not on the menu");
        }
        const open = this.orders.ofSession(caller);
        if (open) {
            return this.reject(caller, "order", "you already have an open order", open.id);
        }
        if (this.orders.size >= this.config.maxOpenOrders) {
            return this.reject(caller, "order", "the bar is full: " + this.orders.size + " orders open");
        }
        const now = this.world.now();
        /** @type {Order} */
        const order = {
            id: this.idBase + "-" + ++this.seq,
            by: caller,
            name: this.nameOf(caller),
            drink: item.name,
            kind: item.kind,
            glass: item.glass,
            at: now,
            expiresAt: now + this.config.orderTtlS * 1000
        };
        this.orders.place(order);
        this.world.announce({ type: "order", id: order.id, by: order.by, name: order.name, drink: order.drink,
            kind: order.kind, glass: order.glass });
        this.publish();
        return true;
    }

    /**
     * Only the bartender in userData.staff serves. The order leaves the book and its glass is
     * set on the counter in front of the orderer.
     * @param {string | undefined} caller
     * @param {unknown} orderId
     * @returns {boolean}
     */
    serve(caller, orderId) {
        if (!this.admit(caller, "serve")) {
            return false;
        }
        this.expire();
        if (!isStaff(caller, this.world.staff(), this.config.staffByName, session => this.world.namesOf(session))) {
            return this.reject(caller, "serve", "only the bartender serves");
        }
        const order = this.orders.get(cleanText(orderId, PARAM_LIMIT));
        if (!order) {
            return this.reject(caller, "serve", "no such order", cleanText(orderId, PARAM_LIMIT));
        }
        this.orders.take(order.id);
        const now = this.world.now();
        const glass = this.world.spawnGlass(order);
        if (glass) {
            this.glasses.push({ id: glass, expiresAt: now + this.config.glassLifetimeS * 1000 });
            // A bar collects its empties: past the cap the oldest glass goes
            while (this.glasses.length > this.config.maxGlasses) {
                this.world.deleteEntity(/** @type {{ id: string }} */ (this.glasses.shift()).id);
            }
        } else {
            this.world.log("no glass for order " + order.id + " (" + order.drink + ")");
        }
        this.lastServed = { id: order.id, drink: order.drink, by: order.by, at: now };
        this.world.announce({ type: "served", id: order.id, by: order.by, name: order.name, drink: order.drink,
            servedBy: caller, glass });
        this.publish();
        return true;
    }

    /**
     * The orderer or the bartender takes an order back.
     * @param {string | undefined} caller
     * @param {unknown} orderId
     * @returns {boolean}
     */
    cancel(caller, orderId) {
        if (!this.admit(caller, "cancel")) {
            return false;
        }
        this.expire();
        const order = this.orders.get(cleanText(orderId, PARAM_LIMIT));
        if (!order) {
            return this.reject(caller, "cancel", "no such order", cleanText(orderId, PARAM_LIMIT));
        }
        if (!sameSession(order.by, caller)
            && !isStaff(caller, this.world.staff(), this.config.staffByName, session => this.world.namesOf(session))) {
            return this.reject(caller, "cancel", "not your order", order.id);
        }
        this.orders.take(order.id);
        this.world.announce({ type: "cancelled", id: order.id, by: order.by, drink: order.drink, cancelledBy: caller });
        this.publish();
        return true;
    }

    /**
     * Sends the status and the open orders on the channel, addressed to the caller. Nothing is
     * stored.
     * @param {string | undefined} caller
     * @returns {boolean}
     */
    status(caller) {
        if (!this.admit(caller, "status")) {
            return false;
        }
        this.expire();
        const now = this.world.now();
        this.world.announce({
            type: "status",
            to: caller,
            ...this.mirror("open"),
            orders: this.orders.list().map(order => ({
                id: order.id, by: order.by, name: order.name, drink: order.drink, ageS: Math.floor((now - order.at) / 1000)
            }))
        });
        return true;
    }

    /**
     * Sends the menu on the channel, addressed to the caller.
     * @param {string | undefined} caller
     * @returns {boolean}
     */
    menu(caller) {
        if (!this.admit(caller, "menu")) {
            return false;
        }
        this.world.announce({ type: "menu", to: caller, items: this.config.menu.map(item => ({ ...item })) });
        return true;
    }

    /** Called every few seconds: expired orders, departed sessions, the bartender coming and going. */
    tick() {
        const now = this.world.now();
        const expired = this.expire(false);
        this.glasses = this.glasses.filter(glass => glass.expiresAt > now);
        this.limiter.prune(now);
        const present = this.findStaff();
        if (expired || present !== this.staffPresent) {
            this.staffPresent = present;
            this.publish();
        }
    }

    /** The script is unloading: the open orders die with it, and the mirror says so. */
    stop() {
        const ids = this.orders.clear();
        this.world.publish(this.mirror("offline"));
        this.world.announce({ type: "closed", ids });
    }

    /**
     * @private
     * @param {"open" | "offline"} state
     * @returns {Status}
     */
    mirror(state) {
        const now = this.world.now();
        const oldest = this.orders.oldest();
        return {
            v: PROTOCOL_VERSION,
            state,
            channel: this.config.channel,
            open: this.orders.size,
            oldestAgeS: oldest ? Math.floor((now - oldest.at) / 1000) : null,
            staffPresent: this.staffPresent,
            lastServed: this.lastServed && { ...this.lastServed },
            at: now
        };
    }

    /**
     * Drops the orders whose time ran out, announcing each. Every call runs this first, so an
     * order past its time cannot be served between ticks.
     * @private
     * @param {boolean} [publish] whether to publish when any expired
     * @returns {boolean} whether any expired
     */
    expire(publish = true) {
        const expired = this.orders.expire(this.world.now());
        expired.forEach(order => this.world.announce({ type: "expired", id: order.id, by: order.by, drink: order.drink }));
        if (expired.length && publish) {
            this.publish();
        }
        return expired.length > 0;
    }

    /**
     * @private
     * @returns {boolean} whether the bartender in userData.staff is in the domain
     */
    findStaff() {
        const staff = this.world.staff();
        if (!staff) {
            return false;
        }
        const avatars = this.world.avatars();
        if (isSession(staff)) {
            return avatars.some(id => sameSession(id, staff));
        }
        return this.config.staffByName && avatars.some(id => this.world.namesOf(id).includes(staff));
    }

    /**
     * @private
     * @param {string} caller
     * @returns {string} the caller's name as shown in orders: cosmetic, never an identity
     */
    nameOf(caller) {
        const [displayName, sessionName] = this.world.namesOf(caller);
        return cleanText(sessionName, PLAYER_NAME_LIMIT) || cleanText(displayName, PLAYER_NAME_LIMIT)
            || "Guest " + sessionKey(caller).slice(0, 8);
    }

    /**
     * The gate every remote call passes first: an authenticated caller within its rate limit.
     * A call with no caller id is only logged, so it cannot cause channel traffic.
     * @private
     * @param {string | undefined} caller
     * @param {string} method
     * @returns {caller is string}
     */
    admit(caller, method) {
        if (!isCaller(caller)) {
            this.world.log(method + " refused, no caller id");
            return false;
        }
        const refusal = this.limiter.admit(caller, this.world.now());
        if (refusal) {
            this.world.log(method + " from " + caller + " refused, " + refusal.reason);
            if (refusal.tell) {
                this.world.announce({ type: "rejected", to: caller, method, reason: refusal.reason });
            }
            return false;
        }
        return true;
    }

    /**
     * @private
     * @param {string} caller
     * @param {string} method
     * @param {string} reason
     * @param {string} [id] the order concerned
     * @returns {false}
     */
    reject(caller, method, reason, id) {
        /** @type {Message} */
        const message = { type: "rejected", to: caller, method, reason };
        if (id !== undefined) {
            message.id = id;
        }
        this.world.log(method + " from " + caller + " refused, " + reason);
        this.world.announce(message);
        return false;
    }

    /** @private */
    publish() {
        this.world.publish(this.mirror("open"));
    }
}
