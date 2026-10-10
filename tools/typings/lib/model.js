//
//  model.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Indexes the doclets in hifiJSDoc.json into what the emitter needs: API namespaces and
//  classes per script context, their members, and the shared typedefs.
//

"use strict";

const CONTEXTS = [
    { id: "interface", flag: "hifiInterface", title: "Interface scripts" },
    { id: "client-entity", flag: "hifiClientEntity", title: "Client entity scripts" },
    { id: "avatar", flag: "hifiAvatar", title: "Avatar scripts" },
    { id: "server-entity", flag: "hifiServerEntity", title: "Server entity scripts" },
    { id: "assignment-client", flag: "hifiAssignmentClient", title: "Assignment client scripts" }
];

function isOptional(item) {
    return Boolean(item.optional) || item.defaultvalue !== undefined;
}

// A typedef describes a bag of settings the engine accepts partially, so every property is
// optional, when most of its properties document a default (PickProperties, Entities.Haze), or
// when some do and a function takes it as an argument (TabletButtonProxy.ButtonProperties for
// Tablet.addButton). Otherwise only the defaulted properties are optional, so one default on a
// result type (BookmarkData, LoadFromCacheResult) does not make the whole result optional.
function isBag(typedef, argumentTypes) {
    if (!typedef.properties) {
        return false;
    }
    const defaulted = typedef.properties.filter(isOptional).length;
    return defaulted * 2 > typedef.properties.length || (defaulted > 0 && argumentTypes.has(typedef.longname));
}

// Longnames that appear in the parameter types of functions and constructors (not signals or
// callbacks, whose arguments the engine produces).
function argumentTypeNames(doclets) {
    const names = new Set();
    doclets.filter(doclet => doclet.kind === "function" || doclet.kind === "class").forEach(doclet => {
        (doclet.params || []).forEach(param => {
            ((param.type && param.type.names) || []).forEach(text => {
                (text.match(/[A-Za-z_$][\w$~-]*(?:\.[A-Za-z_$][\w$~-]*)*/g) || []).forEach(name => names.add(name));
            });
        });
    });
    return names;
}

function literalUnionFromTable(description) {
    const body = /<tbody>([\s\S]*?)<\/tbody>/.exec(description || "");
    if (!body) {
        return null;
    }
    const rows = body[1].split(/<tr[^>]*>/).slice(1);
    const values = rows.map(row => {
        const match = /^\s*<td[^>]*>\s*<code>\s*"([^"]*)"\s*<\/code>\s*<\/td>/.exec(row);
        return match ? match[1] : null;
    });
    if (values.length === 0 || values.includes(null)) {
        return null;
    }
    return [...new Set(values)].map(value => JSON.stringify(value)).join(" | ");
}

function mergeProperties(target, extra, report) {
    const seen = new Map(target.map(property => [property.name, property]));
    extra.forEach(property => {
        const existing = seen.get(property.name);
        if (!existing) {
            target.push(property);
            seen.set(property.name, property);
        } else if (JSON.stringify(existing.type) !== JSON.stringify(property.type) && existing.type && property.type) {
            existing.type = { names: [...new Set(existing.type.names.concat(property.type.names))] };
            if (report) {
                report.push(property.name);
            }
        }
    });
}

function buildModel(rawDoclets) {
    const doclets = rawDoclets.filter(doclet => !doclet.undocumented && doclet.kind !== "package");

    const containers = new Map();
    doclets.filter(doclet => doclet.kind === "namespace" || doclet.kind === "class").forEach(doclet => {
        containers.set(doclet.longname, Object.assign({ members: [] }, doclet));
    });

    const typedefs = new Map();
    doclets.filter(doclet => doclet.kind === "typedef").forEach(doclet => {
        const existing = typedefs.get(doclet.longname);
        if (!existing) {
            typedefs.set(doclet.longname, Object.assign({}, doclet, {
                properties: doclet.properties ? doclet.properties.map(property => Object.assign({}, property)) : undefined
            }));
        } else if (doclet.properties) {
            existing.properties = existing.properties || [];
            mergeProperties(existing.properties, doclet.properties);
        }
    });

    const argumentTypes = argumentTypeNames(doclets);
    typedefs.forEach(typedef => {
        typedef.bag = isBag(typedef, argumentTypes);
        const typeNames = typedef.type ? typedef.type.names : [];
        if (!typedef.properties && typeNames.length === 1 && typeNames[0] === "string") {
            typedef.literals = literalUnionFromTable(typedef.description);
        }
    });

    // "Entities.EntityProperties-Model" documents the Model-specific part of
    // Entities.EntityProperties. The engine takes one flat bag, so a base with variants is a bag
    // and carries the variant properties too.
    typedefs.forEach((typedef, longname) => {
        const dash = longname.lastIndexOf("-");
        if (dash < 0) {
            return;
        }
        const base = typedefs.get(longname.slice(0, dash));
        if (base && base.properties && typedef.properties) {
            base.bag = true;
            typedef.bag = true;
            base.variantNames = (base.variantNames || []).concat([longname]);
            mergeProperties(base.properties, typedef.properties.map(property => Object.assign({}, property)));
        }
    });

    const globals = [];
    const orphans = new Map();
    doclets.filter(doclet => doclet.kind === "function" || doclet.kind === "signal").forEach(doclet => {
        if (!doclet.memberof) {
            if (doclet.scope === "global") {
                globals.push(doclet);
            }
            return;
        }
        const container = containers.get(doclet.memberof);
        if (container) {
            container.members.push(doclet);
        } else {
            orphans.set(doclet.memberof, (orphans.get(doclet.memberof) || 0) + 1);
        }
    });

    const knownTypes = new Set([...typedefs.keys()].concat(
        [...containers.values()].filter(container => container.kind === "class").map(container => container.longname)));

    return {
        containers: [...containers.values()].sort((a, b) => a.name.localeCompare(b.name)),
        typedefs: [...typedefs.values()].sort((a, b) => a.longname.localeCompare(b.longname)),
        globals: globals.sort((a, b) => a.name.localeCompare(b.name)),
        orphans: orphans,
        knownTypes: knownTypes,
        literalTypes: new Set([...typedefs.values()].filter(typedef => typedef.literals).map(typedef => typedef.longname))
    };
}

module.exports = { CONTEXTS, buildModel, isOptional };
