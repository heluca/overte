//
//  bar-config.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bar's configuration without the engine: userData parsed, the menu cleaned and capped, the
//  glasses, the channel, the counter and the limits checked, each problem reported and replaced
//  by its default. Also who userData.staff names, and which menu item a caller's text means.
//

export const MENU_LIMIT = 50;
export const DRINK_NAME_LIMIT = 40;
export const KEY_LIMIT = 32;
// The longest drink or order id a caller may send; a longer one matches nothing.
export const PARAM_LIMIT = 64;
export const DEFAULT_GLASS_DIMENSIONS = { x: 0.08, y: 0.16, z: 0.08 };

/** @typedef {{ maxOpenOrders: number, orderTtlS: number, glassLifetimeS: number, callsPerWindow: number, callWindowS: number, maxGlasses: number }} Limits */

/** @type {Limits} */
export const LIMIT_DEFAULTS = {
    maxOpenOrders: 12,
    orderTtlS: 900,
    glassLifetimeS: 600,
    callsPerWindow: 10,
    callWindowS: 30,
    maxGlasses: 24
};
/** @type {Record<keyof Limits, [number, number]>} */
const LIMIT_RANGES = {
    maxOpenOrders: [1, 100],
    orderTtlS: [30, 7200],
    glassLifetimeS: [10, 3600],
    callsPerWindow: [1, 100],
    callWindowS: [1, 600],
    maxGlasses: [1, 200]
};

const CHANNEL = /^[A-Za-z0-9:._-]{1,64}$/;
const ASSET_URL = /^(https?:\/\/[^/\s]|atp:)\S*$/;
const CONTROL = /[\u0000-\u001f\u007f]/g;

/**
 * @typedef {{ name: string, kind: string, glass: string }} MenuItem
 * @typedef {{ url: string, dimensions: Vec3Like }} Glass
 * @typedef {{ x: number, y: number, z: number }} Vec3Like
 * @typedef {{ y: number, z: number, halfLength: number }} Counter
 * @typedef {{ menu: MenuItem[], glasses: Record<string, Glass>, channel: string, counter: Counter | null,
 *     staffByName: boolean } & Limits} Config
 */

/**
 * @param {string} entityID
 * @returns {string} the channel a bar posts on when userData names none
 */
export function channelFor(entityID) {
    return "bar:" + entityID;
}

/**
 * @param {unknown} text
 * @returns {Record<string, any>} the parsed object, or an empty one for anything else
 */
export function parseObject(text) {
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        return isObject(value) ? value : {};
    } catch (e) {
        return {};
    }
}

/**
 * @param {unknown} value
 * @returns {value is Record<string, any>}
 */
function isObject(value) {
    return value !== null && typeof value === "object" && !Array.isArray(value);
}

/**
 * @param {unknown} value
 * @param {number} limit
 * @returns {string} printable text, trimmed and cut to `limit`; "" for anything not a string
 */
export function cleanText(value, limit) {
    return (typeof value === "string" ? value : "").replace(CONTROL, "").trim().slice(0, limit);
}

/**
 * @param {unknown} value
 * @returns {Vec3Like | null} a size of three positive numbers up to 2 m, else null
 */
function size(value) {
    if (!isObject(value)) {
        return null;
    }
    const { x, y, z } = value;
    return [x, y, z].every(n => typeof n === "number" && n > 0 && n <= 2) ? { x, y, z } : null;
}

/**
 * The bar's configuration from its parsed userData. Each problem is reported and the value
 * replaced by its default (or dropped, for a menu item or a glass). `staff` and `status` are
 * not configuration: the script reads `staff` afresh on every call and writes `status`.
 * @param {Record<string, any>} userData
 * @param {string} entityID
 * @returns {{ config: Config, problems: string[] }}
 */
export function parseConfig(userData, entityID) {
    /** @type {string[]} */
    const problems = [];
    /** @type {MenuItem[]} */
    const menu = [];
    const seen = new Set();
    const items = Array.isArray(userData.menu) ? userData.menu : [];
    if (!Array.isArray(userData.menu)) {
        problems.push("menu must be an array of { name, kind, glass }; every order is refused");
    } else if (items.length > MENU_LIMIT) {
        problems.push("menu has " + items.length + " items; only the first " + MENU_LIMIT + " are served");
    }
    items.slice(0, MENU_LIMIT).forEach((item, index) => {
        const name = cleanText(isObject(item) ? item.name : "", DRINK_NAME_LIMIT);
        if (!name) {
            problems.push("menu[" + index + "] has no name; dropped");
        } else if (seen.has(name.toLowerCase())) {
            problems.push("menu[" + index + "] repeats " + JSON.stringify(name) + "; dropped");
        } else {
            seen.add(name.toLowerCase());
            menu.push({ name, kind: cleanText(item.kind, KEY_LIMIT), glass: cleanText(item.glass, KEY_LIMIT) });
        }
    });

    /** @type {Record<string, Glass>} */
    const glasses = {};
    const glassesIn = isObject(userData.glasses) ? userData.glasses : {};
    if (userData.glasses !== undefined && !isObject(userData.glasses)) {
        problems.push("glasses must be an object of { url, dimensions }; every glass is a Text placeholder");
    }
    for (const [key, glass] of Object.entries(glassesIn).slice(0, MENU_LIMIT)) {
        const url = isObject(glass) && typeof glass.url === "string" ? glass.url : "";
        const dimensions = isObject(glass) && glass.dimensions !== undefined ? size(glass.dimensions) : DEFAULT_GLASS_DIMENSIONS;
        if (key.length > KEY_LIMIT || !ASSET_URL.test(url)) {
            problems.push("glasses." + key + " needs an http(s) or atp: url; that glass is a Text placeholder");
        } else if (!dimensions) {
            problems.push("glasses." + key + ".dimensions must be { x, y, z } in metres, each up to 2; that glass is a Text placeholder");
        } else {
            glasses[key] = { url, dimensions };
        }
    }

    let channel = channelFor(entityID);
    if (userData.channel !== undefined) {
        if (typeof userData.channel === "string" && CHANNEL.test(userData.channel)) {
            channel = userData.channel;
        } else {
            problems.push("channel must be 1 to 64 of A-Z a-z 0-9 : . _ -; using " + channel);
        }
    }

    /** @type {Limits} */
    const limits = { ...LIMIT_DEFAULTS };
    for (const name of /** @type {(keyof Limits)[]} */ (Object.keys(LIMIT_DEFAULTS))) {
        const value = userData[name];
        if (value === undefined) {
            continue;
        }
        const [min, max] = LIMIT_RANGES[name];
        if (Number.isInteger(value) && value >= min && value <= max) {
            limits[name] = value;
        } else {
            problems.push(name + " must be an integer from " + min + " to " + max + ", got "
                + JSON.stringify(value) + "; using " + LIMIT_DEFAULTS[name]);
        }
    }

    /** @type {Counter | null} */
    let counter = null;
    if (userData.counter !== undefined) {
        const c = isObject(userData.counter) ? userData.counter : {};
        const finite = [c.y, c.z, c.halfLength].every(n => typeof n === "number" && Number.isFinite(n) && Math.abs(n) <= 50);
        if (finite && c.halfLength >= 0) {
            counter = { y: c.y, z: c.z, halfLength: c.halfLength };
        } else {
            problems.push("counter must be { y, z, halfLength } in metres in the bar's frame; using the top of the bar's box");
        }
    }

    if (userData.staffByName !== undefined && typeof userData.staffByName !== "boolean") {
        problems.push("staffByName must be true or false; using false");
    }

    return {
        config: { menu, glasses, channel, counter, staffByName: userData.staffByName === true, ...limits },
        problems
    };
}

/**
 * @param {Record<string, any>} userData
 * @returns {string | null} who the bartender is: a session id, or (dev domains) a display name
 */
export function staffOf(userData) {
    const staff = cleanText(userData.staff, PARAM_LIMIT);
    return staff || null;
}

/**
 * @param {MenuItem[]} menu
 * @param {unknown} text the drink as a caller wrote it
 * @returns {MenuItem | null} the menu item of that name, ignoring case
 */
export function findItem(menu, text) {
    if (typeof text !== "string" || text.length > PARAM_LIMIT) {
        return null;
    }
    const wanted = cleanText(text, PARAM_LIMIT).toLowerCase();
    return menu.find(item => item.name.toLowerCase() === wanted) || null;
}
