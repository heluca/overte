//
//  fake-engine.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Fake Entities, Messages, AvatarList and Script globals for the bar's serverScripts entry:
//  the bar entity, the glasses it makes, the messages sent, the avatars in the domain and the
//  timers set.
//

export const BAR = "{ba400000-0000-4000-8000-000000000000}";

export function fakeEngine(userData = "", { privateUserData = "", locked = false, position = { x: 57.6, y: 0.72, z: 10 },
    rotation = { x: 0, y: 0, z: 0, w: 1 }, dimensions = { x: 6, y: 1.44, z: 1.4 },
    registrationPoint = { x: 0.5, y: 0.5, z: 0.5 } } = {}) {
    let nextId = 1;
    const entities = new Map([[BAR, {
        name: "Bar", userData, privateUserData, locked, position, rotation, dimensions, registrationPoint
    }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        timers: new Map(),
        // session id -> { position, displayName, sessionDisplayName }
        avatars: new Map(),
        Entities: {
            addEntity(properties) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties });
                return id;
            },
            editEntity(id, properties) {
                Object.assign(entities.get(id), properties);
                return id;
            },
            deleteEntity(id) { entities.delete(id); },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            }
        },
        AvatarList: {
            getAvatarIdentifiers() { return [...engine.avatars.keys()]; },
            // As the engine does for a session it does not know: an object with empty values
            getAvatar(id) {
                return engine.avatars.get(id) || { position: { x: 0, y: 0, z: 0 }, displayName: "", sessionDisplayName: "" };
            }
        },
        Messages: { sendMessage(channel, message) { engine.sent.push({ channel, message: JSON.parse(message) }); } },
        Script: {
            remoteCallerID: undefined,
            setInterval(callback) { engine.timers.set(nextId, callback); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        // As the entity script server does: the caller id is set only while the method runs
        call(bar, caller, method, params = []) {
            engine.Script.remoteCallerID = caller;
            bar[method](BAR, params);
            engine.Script.remoteCallerID = undefined;
        },
        userData() { return JSON.parse(entities.get(BAR).userData); },
        get last() { return engine.sent[engine.sent.length - 1]; },
        glasses() {
            return [...entities.values()].filter(entity => (entity.name || "").startsWith("Bar.Glass "));
        }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, AvatarList: engine.AvatarList, Messages: engine.Messages, Script: engine.Script, print: engine.print
    });
    return engine;
}
