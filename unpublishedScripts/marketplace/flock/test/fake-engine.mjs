//
//  fake-engine.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Fake Entities, Messages, Script and AvatarList globals for the flock's serverScripts: the
//  entities the members run on, the edits they make, the timers they set, the subscriptions and
//  the avatars in the domain.
//

/**
 * @param {Record<string, Record<string, unknown>>} entities each entity's properties, by id
 */
export function fakeEngine(entities) {
    let nextId = 1;
    const tree = new Map(Object.entries(entities).map(([id, properties]) => [id, { ...properties }]));
    const engine = {
        tree,
        printed: [],
        // [id, properties] of every edit, in order
        edits: [],
        timers: new Map(),
        intervals: new Map(),
        subscribed: [],
        unsubscribed: [],
        handlers: new Set(),
        // session id -> position
        avatars: new Map(),
        getAvatarCalls: 0,
        Entities: {
            editEntity(id, edit) {
                engine.edits.push([id, edit]);
                Object.assign(tree.get(id), edit);
                return id;
            },
            getEntityProperties(id, names) {
                const entity = tree.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            }
        },
        Messages: {
            subscribe(channel) { engine.subscribed.push(channel); },
            unsubscribe(channel) { engine.unsubscribed.push(channel); },
            messageReceived: {
                connect(handler) { engine.handlers.add(handler); },
                disconnect(handler) { engine.handlers.delete(handler); }
            }
        },
        Script: {
            setInterval(callback, ms) { engine.timers.set(nextId, callback); engine.intervals.set(nextId, ms); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); engine.intervals.delete(id); }
        },
        AvatarList: {
            getAvatarsInRange(position, range) {
                return [...engine.avatars].filter(([, p]) => Math.hypot(p.x - position.x, p.y - position.y, p.z - position.z) < range)
                    .map(([id]) => id);
            },
            getAvatarIdentifiers() { return [...engine.avatars.keys()]; },
            getAvatar(id) {
                engine.getAvatarCalls++;
                return { get position() { return engine.avatars.get(id) || { x: 0, y: 0, z: 0 }; } };
            }
        },
        print(text) { engine.printed.push(text); },
        send(channel, message) { [...engine.handlers].forEach(handler => handler(channel, message)); },
        editsOf(id) { return engine.edits.filter(([target]) => target === id).map(([, edit]) => edit); },
        runTimers() { [...engine.timers.values()].forEach(callback => callback()); }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, Messages: engine.Messages, Script: engine.Script, AvatarList: engine.AvatarList, print: engine.print
    });
    return engine;
}

/**
 * A clock the scripts read through Date.now().
 */
export const clock = {
    now: 0,
    /** @param {string} iso */
    set(iso) { clock.now = Date.parse(iso); },
    /** @param {number} ms */
    advance(ms) { clock.now += ms; }
};
Date.now = () => clock.now;
