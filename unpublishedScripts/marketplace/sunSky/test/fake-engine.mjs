//
//  fake-engine.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Fake Entities, Messages and Script globals for the serverScripts: the one entity a script
//  runs on, the edits and messages it makes and the timers it sets.
//

export const SELF = "{5c7a0000-0000-4000-8000-000000000000}";

/**
 * @param {Record<string, unknown>} properties the entity's properties
 */
export function fakeEngine(properties) {
    let nextId = 1;
    const entities = new Map([[SELF, { ...properties }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        // [id, properties] of every edit, in order
        edits: [],
        timers: new Map(),
        intervals: new Map(),
        Entities: {
            editEntity(id, edit) {
                engine.edits.push([id, edit]);
                Object.assign(entities.get(id), edit);
                return id;
            },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            }
        },
        Messages: { sendMessage(channel, message) { engine.sent.push({ channel, message: JSON.parse(message) }); } },
        Script: {
            setInterval(callback, ms) { engine.timers.set(nextId, callback); engine.intervals.set(nextId, ms); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); engine.intervals.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        self() { return entities.get(SELF); },
        selfEdits() { return engine.edits.filter(([id]) => id === SELF).map(([, edit]) => edit); },
        runTimers() { [...engine.timers.values()].forEach(callback => callback()); }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Messages: engine.Messages, Script: engine.Script, print: engine.print });
    return engine;
}

/**
 * A clock the scripts read through Date.now().
 */
export const clock = {
    now: 0,
    /** @param {string} iso */
    set(iso) { clock.now = Date.parse(iso); }
};
Date.now = () => clock.now;
