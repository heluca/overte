//
//  fake-engine.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Fake Entities, Messages and Script globals for the sky zone's serverScripts: the sky zone,
//  the entities it finds by name, the edits and messages it makes and the timers it sets.
//

export const SKY = "{5c7a0000-0000-4000-8000-000000000000}";

export function fakeEngine(skyUserData = "", locked = false) {
    let nextId = 1;
    const entities = new Map([[SKY, {
        name: "Sky zone", position: { x: 0, y: 100, z: 0 }, userData: skyUserData, locked, visible: true
    }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        // [id, properties] of every edit, in order
        edits: [],
        // [name, center, radius, caseSensitive] of every search
        searches: [],
        timers: new Map(),
        intervals: new Map(),
        Entities: {
            editEntity(id, properties) {
                engine.edits.push([id, properties]);
                Object.assign(entities.get(id), properties);
                return id;
            },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            },
            findEntitiesByName(name, center, radius, caseSensitive) {
                engine.searches.push([name, center, radius, caseSensitive]);
                return [...entities].filter(([, entity]) => entity.name === name).map(([id]) => id);
            }
        },
        Messages: { sendMessage(channel, message) { engine.sent.push({ channel, message: JSON.parse(message) }); } },
        Script: {
            setInterval(callback, ms) { engine.timers.set(nextId, callback); engine.intervals.set(nextId, ms); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); engine.intervals.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        add(properties) {
            const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
            entities.set(id, { ...properties });
            return id;
        },
        editsTo(id) {
            return engine.edits.filter(([target]) => target === id).map(([, properties]) => properties);
        },
        runTimers() {
            [...engine.timers.values()].forEach(callback => callback());
        }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Messages: engine.Messages, Script: engine.Script, print: engine.print });
    return engine;
}
