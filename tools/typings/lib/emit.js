//
//  emit.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Renders the indexed doclets as TypeScript declaration files.
//

"use strict";

const { tsName, unionOf, arrayOf } = require("./typeexpr");
const { isOptional } = require("./model");
const { docComment, isReadOnly, paramName, RESERVED } = require("./doc");

// Overloads generated for optional parameters that precede a required one, before falling back
// to "T | undefined".
const MAX_EXPANDED_OPTIONALS = 3;

const SIGNAL_PRELUDE = `/**
 * A Qt signal exposed to scripts. Connect a function to be called each time the signal is emitted;
 * the two-argument forms call the function with \`this\` bound to \`thisObject\`.
 */
interface Signal<F extends (...args: any[]) => void = (...args: any[]) => void> {
    connect(callback: F): void;
    connect<T>(thisObject: T, callback: (this: T, ...args: Parameters<F>) => void): void;
    disconnect(callback: F): void;
    disconnect<T>(thisObject: T, callback: (this: T, ...args: Parameters<F>) => void): void;
}
`;

const IDENTIFIER = /^[A-Za-z_$][\w$]*$/;

function propertyKey(name) {
    return IDENTIFIER.test(name) ? name : JSON.stringify(name);
}

// Flat "options", "options.url", "options.list[].id" names as a tree.
function buildTree(items) {
    const root = [];
    items.filter(item => item.name).forEach(item => {
        const segments = item.name.split(".");
        let level = root;
        segments.forEach((segment, index) => {
            const isArray = segment.endsWith("[]");
            const key = isArray ? segment.slice(0, -2) : segment;
            let node = level.find(candidate => candidate.key === key);
            if (!node) {
                node = { key: key, item: null, children: [], isArray: false };
                level.push(node);
            }
            node.isArray = node.isArray || isArray;
            if (index === segments.length - 1 && !node.item) {
                node.item = item;
            }
            level = node.children;
        });
    });
    return root;
}

function isObjectLike(item) {
    return !item || !item.type || item.type.names.every(name => /^(object|Object|array|Array)(\.<(object|Object|\*)>)?$/.test(name));
}

class Emitter {
    constructor(model, mapper) {
        this.model = model;
        this.mapper = mapper;
        this.dropped = new Set();
        this.skippedProperties = new Set();
        this.rendered = new Map();
    }

    // Each namespace or constructor is rendered once and shared by every context it is
    // available in, so the unknown-type tally counts each documented use once.
    renderOnce(key, render) {
        if (!this.rendered.has(key)) {
            this.rendered.set(key, render());
        }
        return this.rendered.get(key);
    }

    nodeType(node, options) {
        if (node.children.length > 0 && isObjectLike(node.item)) {
            const inline = this.inlineObject(node.children, options);
            const isArray = node.isArray || (node.item && node.item.type && node.item.type.names.some(name => /^(array|Array)/.test(name)));
            return isArray ? inline + "[]" : inline;
        }
        let type = this.mapper.map(node.item && node.item.type);
        if (node.item && node.item.nullable) {
            type = unionOf([type, "null"]);
        }
        return type;
    }

    inlineObject(nodes, options) {
        if (!options.indent) {
            const members = nodes.map(node => propertyKey(node.key) + (options.bag || isOptional(node.item || {}) ? "?" : "") +
                ": " + this.nodeType(node, options));
            return "{ " + members.join("; ") + " }";
        }
        const inner = options.indent + "    ";
        const lines = ["{"];
        nodes.forEach(node => this.propertyLines(node, Object.assign({}, options, { indent: inner }), lines));
        lines.push(options.indent + "}");
        return lines.join("\n");
    }

    propertyLines(node, options, lines) {
        const item = node.item || {};
        // A documented default on a class property is its initial value, not a sign it may be absent.
        const optional = options.readonlyProperties ? Boolean(item.optional) : (options.bag || isOptional(item));
        const readonly = options.readonlyProperties && isReadOnly(item) ? "readonly " : "";
        lines.push(...docComment(item, options.indent));
        lines.push(options.indent + readonly + propertyKey(node.key) + (optional ? "?" : "") + ": " +
            this.nodeType(node, options) + ";");
    }

    // One parameter list per overload: optional parameters that precede a required one are
    // expanded into overloads with and without them.
    paramLists(params, expand) {
        const named = (params || []).map((param, index) => param.name ? param : Object.assign({}, param, { name: "arg" + index }));
        const entries = buildTree(named).map(node => ({
            name: paramName(node.key),
            type: this.nodeType(node, { bag: false }),
            optional: Boolean(node.item) && isOptional(node.item),
            rest: Boolean(node.item && node.item.variable)
        }));
        entries.forEach((entry, index) => {
            if (entry.rest && index !== entries.length - 1) {
                entry.rest = false;
                entry.type = arrayOf(entry.type);
            }
        });
        let lastRequired = -1;
        entries.forEach((entry, index) => {
            if (!entry.optional && !entry.rest) {
                lastRequired = index;
            }
        });
        const middle = entries.map((entry, index) => (entry.optional && index < lastRequired) ? index : -1)
            .filter(index => index >= 0);
        const format = (list) => list.map(entry => {
            if (entry.rest) {
                return "..." + entry.name + ": " + arrayOf(entry.type);
            }
            if (middle.includes(entries.indexOf(entry))) {
                return entry.name + ": " + (entry.forced ? unionOf([entry.type, "undefined"]) : entry.type);
            }
            return entry.name + (entry.optional ? "?" : "") + ": " + entry.type;
        }).join(", ");

        if (!expand || middle.length === 0 || middle.length > MAX_EXPANDED_OPTIONALS) {
            entries.forEach((entry, index) => { entry.forced = middle.includes(index); });
            return [format(entries)];
        }
        const lists = [];
        for (let mask = (1 << middle.length) - 1; mask >= 0; mask--) {
            lists.push(format(entries.filter((entry, index) => {
                const bit = middle.indexOf(index);
                return bit < 0 || (mask & (1 << bit)) !== 0;
            })));
        }
        return lists;
    }

    returnType(doclet) {
        if (!doclet.returns || doclet.returns.length === 0) {
            return "void";
        }
        return unionOf(doclet.returns.map(entry => this.mapper.map(entry.type)));
    }

    signalType(doclet) {
        return "Signal<(" + this.paramLists(doclet.params, false)[0] + ") => void>";
    }

    // Properties win over functions and signals of the same name (a Q_PROPERTY with a getter
    // documented twice); the first-documented kind wins between a function and a signal.
    resolveMembers(container) {
        const taken = new Map((container.properties || []).map(property => [property.name, "property"]));
        return container.members.slice().sort((a, b) => a.name.localeCompare(b.name)).filter(member => {
            const kind = taken.get(member.name);
            if (!kind) {
                taken.set(member.name, member.kind);
                return true;
            }
            if (kind === member.kind && member.kind === "function") {
                return true;
            }
            this.dropped.add(container.longname + "." + member.name + " (" + member.kind + " shadowed by " + kind + ")");
            return false;
        });
    }

    // --- shared types file -------------------------------------------------------------------

    typedefLines(typedef, indent) {
        const name = tsName(typedef.longname).split(".").pop();
        const lines = docComment(typedef, indent);
        const typeNames = typedef.type ? typedef.type.names : [];
        if (typedef.properties && typedef.properties.length > 0) {
            lines.push(indent + "interface " + name + " {");
            buildTree(typedef.properties).forEach(node => this.propertyLines(node,
                { bag: typedef.bag, indent: indent + "    " }, lines));
            lines.push(indent + "}");
        } else if (typeNames.length === 1 && /^function$/i.test(typeNames[0]) && (typedef.params || typedef.returns)) {
            lines.push(indent + "type " + name + " = (" + this.paramLists(typedef.params, false)[0] + ") => " +
                (typedef.returns ? this.returnType(typedef) : "void") + ";");
        } else {
            lines.push(indent + "type " + name + " = " + (typedef.literals || this.mapper.map(typedef.type)) + ";");
        }
        return lines;
    }

    classLines(container, indent) {
        const name = tsName(container.longname).split(".").pop();
        const lines = docComment(container, indent);
        const inner = indent + "    ";
        lines.push(indent + "interface " + name + " {");
        buildTree(container.properties || []).forEach(node => this.propertyLines(node,
            { bag: false, indent: inner, readonlyProperties: true }, lines));
        this.resolveMembers(container).forEach(member => {
            lines.push(...docComment(member, inner));
            if (member.kind === "signal") {
                lines.push(inner + "readonly " + propertyKey(member.name) + ": " + this.signalType(member) + ";");
            } else {
                this.paramLists(member.params, true).forEach(params => {
                    lines.push(inner + propertyKey(member.name) + "(" + params + "): " + this.returnType(member) + ";");
                });
            }
        });
        lines.push(indent + "}");
        return lines;
    }

    sharedFile(header) {
        const groups = new Map();
        const add = (qualified, render) => {
            const path = qualified.split(".");
            const parent = path.slice(0, -1).join(".");
            if (!groups.has(parent)) {
                groups.set(parent, []);
            }
            groups.get(parent).push({ name: path[path.length - 1], render: render });
        };
        this.model.typedefs.forEach(typedef => add(tsName(typedef.longname), indent => this.typedefLines(typedef, indent)));
        this.model.containers.filter(container => container.kind === "class")
            .forEach(container => add(tsName(container.longname), indent => this.classLines(container, indent)));

        const out = [header, SIGNAL_PRELUDE];
        [...groups.keys()].sort().forEach(parent => {
            const entries = groups.get(parent).sort((a, b) => a.name.localeCompare(b.name));
            const indent = parent ? "    " : "";
            if (parent) {
                out.push("declare namespace " + parent + " {");
            }
            entries.forEach(entry => out.push(entry.render(indent).join("\n")));
            if (parent) {
                out.push("}");
            }
            out.push("");
        });
        return out.join("\n");
    }

    // --- per-context files -------------------------------------------------------------------

    namespaceLines(container) {
        const lines = docComment(container, "");
        const inner = "    ";
        lines.push("declare namespace " + tsName(container.longname) + " {");
        buildTree(container.properties || []).forEach(node => {
            if (!IDENTIFIER.test(node.key) || RESERVED.has(node.key)) {
                this.skippedProperties.add(container.longname + "." + node.key);
                return;
            }
            lines.push(...docComment(node.item || {}, inner));
            lines.push(inner + (isReadOnly(node.item || {}) ? "const " : "let ") + node.key + ": " +
                this.nodeType(node, { bag: false, indent: inner }) + ";");
        });
        this.resolveMembers(container).forEach(member => {
            lines.push(...docComment(member, inner));
            if (member.kind === "signal") {
                lines.push(inner + "const " + member.name + ": " + this.signalType(member) + ";");
            } else {
                this.paramLists(member.params, true).forEach(params => {
                    lines.push(inner + "function " + member.name + "(" + params + "): " + this.returnType(member) + ";");
                });
            }
        });
        lines.push("}", "");
        return lines;
    }

    constructorLines(container) {
        const name = tsName(container.longname);
        const lines = docComment(container, "");
        lines.push("declare var " + name + ": {");
        this.paramLists(container.params, true).forEach(params => lines.push("    new (" + params + "): " + name + ";"));
        lines.push("    prototype: " + name + ";", "};", "");
        return lines;
    }

    contextFile(context, header) {
        const out = [header, "/// <reference path=\"./overte-types.d.ts\" />", ""];
        this.model.globals.forEach(global => {
            out.push(...this.renderOnce("global:" + global.longname, () => {
                const lines = docComment(global, "");
                this.paramLists(global.params, true).forEach(params => {
                    lines.push("declare function " + global.name + "(" + params + "): " + this.returnType(global) + ";");
                });
                lines.push("");
                return lines;
            }));
        });
        this.model.containers.filter(container => container[context.flag]).forEach(container => {
            if (container.kind === "namespace") {
                out.push(...this.renderOnce(container.longname, () => this.namespaceLines(container)));
            } else if (!container.hideconstructor) {
                out.push(...this.renderOnce(container.longname, () => this.constructorLines(container)));
            }
        });
        return out.join("\n");
    }
}

module.exports = { Emitter };
