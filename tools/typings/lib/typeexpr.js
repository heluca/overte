//
//  typeexpr.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Parses the JSDoc type expressions found in hifiJSDoc.json (e.g. "Array.<Uuid>",
//  "Object.<string, Entities.EntityProperties>") and maps them to TypeScript.
//

"use strict";

const ANY_FUNCTION = "(...args: any[]) => unknown";

// JSDoc / Closure spellings of JavaScript's own types.
const PRIMITIVES = {
    "number": "number", "Number": "number", "int": "number", "integer": "number", "float": "number",
    "double": "number",
    "string": "string", "String": "string",
    "boolean": "boolean", "Boolean": "boolean", "bool": "boolean",
    "object": "object", "Object": "object",
    "function": ANY_FUNCTION, "Function": ANY_FUNCTION,
    "array": "unknown[]", "Array": "unknown[]",
    "undefined": "undefined", "null": "null", "void": "void",
    "*": "unknown"
};

// Types provided by TypeScript's ES2023 lib, which the engine's V8 also provides.
const BUILTINS = new Set([
    "ArrayBuffer", "DataView", "Date", "Error", "RegExp", "Map", "Set", "WeakMap", "WeakSet",
    "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array",
    "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array"
]);

// A JSDoc longname ("Script~entityEventCallback", "Entities.EntityProperties-Model",
// "Vec3(0)") as a TypeScript qualified name.
function tsName(longname) {
    return longname.replace(/\(\d+\)/g, "").replace(/[~#]/g, ".").replace(/-/g, "_");
}

function tokenize(text) {
    const tokens = [];
    const re = /\s*(\.<|[<>,|()\[\]?!*]|"[^"]*"|'[^']*'|[A-Za-z_$][\w$~#-]*(?:\.[A-Za-z_$][\w$~#-]*)*(?:\(\d+\))?|-?\d+(?:\.\d+)?)/y;
    let match;
    while (re.lastIndex < text.length && (match = re.exec(text))) {
        tokens.push(match[1]);
    }
    if (re.lastIndex < text.trimEnd().length) {
        return null;
    }
    return tokens;
}

class TypeMapper {
    // knownTypes: Set of JSDoc longnames that are emitted as TypeScript types.
    constructor(knownTypes) {
        this.knownTypes = knownTypes;
        this.unknownCounts = new Map();
        this.untypedCount = 0;
    }

    unresolved(name) {
        this.unknownCounts.set(name, (this.unknownCounts.get(name) || 0) + 1);
        return "unknown";
    }

    // A JSDoc { names: [...] } object (or undefined) as one TypeScript type.
    map(type) {
        if (!type || !type.names || type.names.length === 0) {
            this.untypedCount++;
            return "unknown";
        }
        const parts = type.names.map(name => this.mapExpression(name));
        return unionOf(parts);
    }

    mapExpression(text) {
        const tokens = tokenize(text);
        if (!tokens) {
            return this.unresolved(text);
        }
        const state = { tokens: tokens, pos: 0 };
        const result = this.parseUnion(state);
        if (state.pos !== tokens.length) {
            return this.unresolved(text);
        }
        return result;
    }

    parseUnion(state) {
        const parts = [this.parseArrayed(state)];
        while (state.tokens[state.pos] === "|") {
            state.pos++;
            parts.push(this.parseArrayed(state));
        }
        return unionOf(parts);
    }

    parseArrayed(state) {
        let result = this.parseAtom(state);
        while (state.tokens[state.pos] === "[" && state.tokens[state.pos + 1] === "]") {
            state.pos += 2;
            result = arrayOf(result);
        }
        return result;
    }

    parseAtom(state) {
        let token = state.tokens[state.pos++];
        while (token === "?" || token === "!") {
            token = state.tokens[state.pos++];
        }
        if (token === undefined) {
            return this.unresolved("(empty)");
        }
        if (token === "(") {
            const inner = this.parseUnion(state);
            if (state.tokens[state.pos] === ")") {
                state.pos++;
            }
            return inner;
        }
        if (/^["']/.test(token)) {
            return JSON.stringify(token.slice(1, -1));
        }
        if (/^-?\d/.test(token)) {
            return token;
        }

        let args = [];
        if (state.tokens[state.pos] === ".<" || state.tokens[state.pos] === "<") {
            state.pos++;
            args.push(this.parseUnion(state));
            while (state.tokens[state.pos] === ",") {
                state.pos++;
                args.push(this.parseUnion(state));
            }
            if (state.tokens[state.pos] === ">") {
                state.pos++;
            }
        }
        return this.resolveName(token, args);
    }

    resolveName(name, args) {
        if (args.length > 0) {
            if (name === "Array" || name === "array") {
                return arrayOf(args[0]);
            }
            if (name === "Object" || name === "object") {
                const key = args.length > 1 ? args[0] : "string";
                const value = args.length > 1 ? args[1] : args[0];
                return "Record<" + (key === "unknown" ? "string" : key) + ", " + value + ">";
            }
            if (name === "Promise") {
                return "Promise<" + args[0] + ">";
            }
            if (name === "Signal") {
                return "Signal";
            }
        }
        if (name === "Promise") {
            return "Promise<unknown>";
        }
        if (name === "Signal") {
            return "Signal";
        }
        if (Object.prototype.hasOwnProperty.call(PRIMITIVES, name)) {
            return PRIMITIVES[name];
        }
        if (BUILTINS.has(name)) {
            return name;
        }
        if (this.knownTypes.has(name)) {
            return tsName(name);
        }
        return this.unresolved(name);
    }

    topUnknowns(count) {
        return [...this.unknownCounts.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))
            .slice(0, count);
    }

    totalUnknown() {
        let total = 0;
        this.unknownCounts.forEach(value => { total += value; });
        return total;
    }
}

function needsParens(type) {
    return /[|&]|=>/.test(type);
}

function arrayOf(type) {
    return needsParens(type) ? "(" + type + ")[]" : type + "[]";
}

function unionOf(parts) {
    const unique = [...new Set(parts)];
    if (unique.includes("unknown")) {
        return "unknown";
    }
    return unique.map(part => (/=>/.test(part) && unique.length > 1) ? "(" + part + ")" : part).join(" | ");
}

module.exports = { TypeMapper, tsName, unionOf, arrayOf, ANY_FUNCTION };
