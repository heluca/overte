//
//  doc.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Turns the HTML-flavoured JSDoc descriptions into TSDoc comments for editor hovers.
//

"use strict";

const { tsName } = require("./typeexpr");

const ENTITIES = {
    "&lt;": "<", "&gt;": ">", "&amp;": "&", "&quot;": "\"", "&#39;": "'", "&apos;": "'", "&nbsp;": " ",
    "&ndash;": "-", "&mdash;": "-", "&bull;": "*", "&deg;": "deg", "&times;": "x", "&hellip;": "...",
    "&le;": "<=", "&ge;": ">=", "&plusmn;": "+/-", "&rarr;": "->", "&larr;": "<-", "&pi;": "pi"
};

const RESERVED = new Set(("break case catch class const continue debugger default delete do else enum export " +
    "extends false finally for function if import in instanceof new null return super switch this throw true try " +
    "typeof var void while with yield let static implements interface package private protected public await")
    .split(" "));

function paramName(name) {
    let result = name.replace(/[^\w$]/g, "_");
    if (RESERVED.has(result) || /^\d/.test(result)) {
        result += "_";
    }
    return result;
}

function isReadOnly(item) {
    return /Read-only/i.test(item.description || "");
}

function htmlToLines(html) {
    if (!html) {
        return [];
    }
    let text = String(html)
        .replace(/\{@link\s+([^}|\s]+)\s*(?:\|\s*([^}]*))?\}/g, (match, target, label) =>
            "{@link " + tsName(target) + (label ? " | " + label.trim() : "") + "}")
        .replace(/<\/?(p|br|ul|ol|table|thead|tbody|div|pre|h\d)\b[^>]*>/gi, "\n")
        .replace(/<li[^>]*>/gi, "\n- ")
        .replace(/<tr[^>]*>/gi, "\n")
        .replace(/<\/t[dh]>/gi, " | ")
        .replace(/<\/?code>/gi, "`")
        .replace(/<[^>]+>/g, "")
        .replace(/&[#\w]+;/g, entity => ENTITIES[entity] || entity)
        .replace(/\*\//g, "*\\/");
    const lines = text.split("\n").map(line => line.replace(/\s+/g, " ").trim().replace(/^@/, "\\@"));
    const result = [];
    lines.forEach(line => {
        if (line !== "" || (result.length > 0 && result[result.length - 1] !== "")) {
            result.push(line);
        }
    });
    while (result.length > 0 && result[result.length - 1] === "") {
        result.pop();
    }
    return result;
}

// A TSDoc comment for a doclet, property or parameter, as lines indented by `indent`.
function docComment(item, indent) {
    const body = htmlToLines(item.description);
    (item.params || []).filter(param => param.name && !param.name.includes(".")).forEach(param => {
        const text = htmlToLines(param.description).join(" ");
        body.push("@param " + paramName(param.name) + (text ? " - " + text : ""));
    });
    (item.returns || []).forEach(entry => {
        const text = htmlToLines(entry.description).join(" ");
        if (text) {
            body.push("@returns " + text);
        }
    });
    if (item.defaultvalue !== undefined) {
        body.push("@defaultValue `" + String(item.defaultvalue).replace(/\*\//g, "*\\/") + "`");
    }
    if (item.deprecated) {
        body.push("@deprecated" + (typeof item.deprecated === "string" ? " " + htmlToLines(item.deprecated).join(" ") : ""));
    }
    if (body.length === 0) {
        return [];
    }
    return [indent + "/**"].concat(body.map(line => (indent + " * " + line).trimEnd()), [indent + " */"]);
}

module.exports = { docComment, isReadOnly, paramName, RESERVED };
