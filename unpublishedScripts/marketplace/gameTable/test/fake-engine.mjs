//
//  fake-engine.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Fake Entities, Messages and Script globals for the serverScripts entry and its layouts:
//  a board entity, its children, the messages sent and the timers set.
//

export const BOARD = "{b0a4d000-0000-4000-8000-000000000000}";

export function fakeEngine(boardUserData = "", locked = false, dimensions = { x: 0.7, y: 0.6, z: 0.04 }) {
    let nextId = 1;
    const entities = new Map([[BOARD, {
        name: "Board", dimensions, userData: boardUserData, locked
    }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        boardEdits: 0,
        timers: new Map(),
        Entities: {
            addEntity(properties) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties });
                return id;
            },
            editEntity(id, properties) {
                if (id === BOARD) {
                    engine.boardEdits++;
                }
                Object.assign(entities.get(id), properties);
                return id;
            },
            deleteEntity(id) { entities.delete(id); },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            },
            getChildrenIDs(parentID) {
                return [...entities].filter(([, entity]) => entity.parentID === parentID).map(([id]) => id);
            }
        },
        Messages: { sendMessage(channel, message) { engine.sent.push({ channel, message: JSON.parse(message) }); } },
        Script: {
            remoteCallerID: undefined,
            setInterval(callback) { engine.timers.set(nextId, callback); return nextId++; },
            setTimeout(callback) { engine.timers.set(nextId, callback); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); },
            clearTimeout(id) { engine.timers.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        // As the entity script server does: the caller id is set only while the method runs
        call(referee, caller, method, params = []) {
            engine.Script.remoteCallerID = caller;
            referee[method](BOARD, params);
            engine.Script.remoteCallerID = undefined;
        },
        named(name) {
            return [...entities.values()].filter(entity => entity.name === name);
        },
        discs() {
            return [...entities.values()].filter(entity => (entity.name || "").startsWith("GT.Disc "));
        },
        projection() { return JSON.parse(entities.get(BOARD).userData).gameTable; },
        // A child the entity server delivers after preload has started
        arrive(name) {
            return engine.Entities.addEntity({ name, parentID: BOARD });
        },
        runTimer(id) {
            const callback = engine.timers.get(id);
            engine.timers.delete(id);
            callback();
        }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Messages: engine.Messages, Script: engine.Script, print: engine.print });
    return engine;
}
