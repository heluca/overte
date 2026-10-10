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
        const bases = model.variantBases.map(base => tsName(base.longname).replace(/[.$]/g, "\\$&"));
        this.variantBasePattern = new RegExp("\\b(" + bases.join("|") + ")(?![\\w$])", "g");
        this.typedefs = new Map(model.typedefs.map(typedef => [typedef.longname, typedef]));
        this.discriminantTypes = new Set(model.variantBases.map(base => base.discriminant.type.names[0]));
    }

    // The engine returns only the requested properties and edits only the given ones, so return
    // types and the parameters of edit functions take a variant union with every member partial,
    // the discriminant included. Partial<> distributes over the union.
    partialVariants(type) {
        return this.model.variantBases.length > 0 ? type.replace(this.variantBasePattern, "Partial<$1>") : type;
    }

    // Whether a parameter's type is a typedef that holds a variant base in one of its properties,
    // at any depth (Pointers.RayPointerRenderState's start/path/end), so that an edit takes it
    // through PartialVariants<>.
    nestsVariants(item, seen) {
        const visited = seen || new Set();
        return ((item && item.type && item.type.names) || []).some(text =>
            (text.match(/[A-Za-z_$][\w$~-]*(?:\.[A-Za-z_$][\w$~-]*)*/g) || []).some(name => {
                const typedef = this.typedefs.get(name);
                if (!typedef || typedef.variants || typedef.variantOf || visited.has(name)) {
                    return false;
                }
                visited.add(name);
                return (typedef.properties || []).some(property =>
                    ((property.type && property.type.names) || []).some(inner =>
                        this.model.variantBases.some(base => new RegExp("(^|[^\\w$.-])" + base.longname.replace(/[.$-]/g, "\\$&") +
                            "(?![\\w$-])").test(inner))) || this.nestsVariants(property, visited));
            }));
    }

    // An edit (editEntity, updateAction) or a function that takes the discriminant as its own
    // parameter (addAction's actionType, addOverlay's type) does not need it in the properties.
    takesPartialVariants(doclet) {
        return /^(edit|update)/.test(doclet.name) || (doclet.params || []).some(param =>
            ((param.type && param.type.names) || []).some(name => this.discriminantTypes.has(name)));
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
    paramLists(params, expand, partial) {
        const named = (params || []).map((param, index) => param.name ? param : Object.assign({}, param, { name: "arg" + index }));
        const typeOf = node => {
            const type = this.nodeType(node, { bag: false });
            if (!partial) {
                return type;
            }
            return this.nestsVariants(node.item) ? "PartialVariants<" + type + ">" : this.partialVariants(type);
        };
        const entries = buildTree(named).map(node => ({
            name: paramName(node.key),
            type: typeOf(node),
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
        return this.partialVariants(this.mappedReturn(doclet));
    }

    mappedReturn(doclet) {
        if (!doclet.returns || doclet.returns.length === 0) {
            return "void";
        }
        return unionOf(doclet.returns.map(entry => this.mapper.map(entry.type)));
    }

    // [parameters, return type] per overload of a function or method. A getter of a variant base
    // called without its optional parameters (getEntityProperties without desiredProperties)
    // returns every property, the discriminant included, so that call returns the full union.
    overloads(member) {
        const full = this.mappedReturn(member);
        const returns = this.partialVariants(full);
        const lists = this.paramLists(member.params, true, this.takesPartialVariants(member)).map(params => [params, returns]);
        const optional = (member.params || []).filter(param => param.name && !param.name.includes(".") && isOptional(param))
            .map(param => param.name);
        if (full === returns || optional.length === 0) {
            return lists;
        }
        const required = member.params.filter(param => !optional.includes((param.name || "").split(".")[0]));
        return [[this.paramLists(required, false, false)[0], full]].concat(lists);
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

    // A variant base is the union of its variants plus a "_Common" interface they extend, holding
    // the base properties other than the discriminant.
    variantBaseLines(base, indent) {
        const name = tsName(base.longname);
        const lines = docComment(base, indent);
        lines.push(indent + "type " + name.split(".").pop() + " =");
        base.variants.filter(variant => variant.values.length > 0).forEach(variant => {
            lines.push(indent + "    | " + tsName(variant.longname));
        });
        if (base.otherValues.length > 0) {
            lines.push(indent + "    | (" + name + "_Common & { " + propertyKey(base.discriminant.name) + ": " +
                unionOf(base.otherValues) + " })");
        }
        lines[lines.length - 1] += ";";
        lines.push(indent + "/**", indent + " * The properties of {@link " + name + "} common to all its variants.", indent + " */");
        lines.push(indent + "interface " + name.split(".").pop() + "_Common {");
        buildTree(base.properties.filter(property => property !== base.discriminant)).forEach(node =>
            this.propertyLines(node, { bag: true, indent: indent + "    " }, lines));
        lines.push(indent + "}");
        return lines;
    }

    variantLines(variant, indent) {
        const base = variant.variantOf;
        const lines = docComment(variant, indent);
        const parent = variant.inherits ? "Omit<" + tsName(variant.inherits.longname) + ", " +
            JSON.stringify(base.discriminant.name) + ">" : tsName(base.longname) + "_Common";
        lines.push(indent + "interface " + tsName(variant.longname).split(".").pop() + " extends " + parent + " {");
        if (variant.values.length > 0) {
            lines.push(...docComment(base.discriminant, indent + "    "));
            lines.push(indent + "    " + propertyKey(base.discriminant.name) + ": " + unionOf(variant.values) + ";");
        }
        buildTree(variant.properties || []).forEach(node => this.propertyLines(node, { bag: true, indent: indent + "    " }, lines));
        lines.push(indent + "}");
        return lines;
    }

    typedefLines(typedef, indent) {
        if (typedef.variants) {
            return this.variantBaseLines(typedef, indent);
        }
        if (typedef.variantOf) {
            return this.variantLines(typedef, indent);
        }
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
                this.overloads(member).forEach(([params, returns]) => {
                    lines.push(inner + propertyKey(member.name) + "(" + params + "): " + returns + ";");
                });
            }
        });
        lines.push(indent + "}");
        return lines;
    }

    // Deep partial of the variant unions inside T, for edits that nest entity properties in another
    // object (Pointers.editRenderState's start/path/end). Only the variant unions become partial.
    partialVariantsPrelude() {
        if (this.model.variantBases.length === 0) {
            return "";
        }
        const bases = this.model.variantBases.map(base => tsName(base.longname)).join(" | ");
        return [
            "/**",
            " * `T` with every variant union inside it (" + bases + ") made partial, as an edit",
            " * that nests them in another object takes them.",
            " */",
            "type PartialVariants<T> = T extends " + bases + " ? Partial<T>",
            "    : T extends (...args: any[]) => unknown ? T",
            "    : T extends object ? { [K in keyof T]: PartialVariants<T[K]> }",
            "    : T;",
            ""
        ].join("\n");
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

        const out = [header, SIGNAL_PRELUDE, this.partialVariantsPrelude()];
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
                this.overloads(member).forEach(([params, returns]) => {
                    lines.push(inner + "function " + member.name + "(" + params + "): " + returns + ";");
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
