//
//  WebGlobalsUrlPrelude.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "WebGlobalsClass.h"

// The end of the web globals prelude (WebGlobalsPrelude.cpp): URLSearchParams and URL, and the returned globals.
static const char* const WEB_GLOBALS_URL_PRELUDE_PARTS[] = {
R"JS(    function formEncode(text) {
        let out = "";
        for (const byte of utf8Bytes(text)) {
            if (byte === 0x20) {
                out += "+";
            } else if ((byte >= 0x30 && byte <= 0x39) || (byte >= 0x41 && byte <= 0x5A) || (byte >= 0x61 && byte <= 0x7A) ||
                "*-._".includes(String.fromCharCode(byte))) {
                out += String.fromCharCode(byte);
            } else {
                out += HEX[byte];
            }
        }
        return out;
    }
    function isHexDigit(byte) {
        return (byte >= 0x30 && byte <= 0x39) || (byte >= 0x41 && byte <= 0x46) || (byte >= 0x61 && byte <= 0x66);
    }
    function percentDecode(text) {
        if (!text.includes("%")) {
            return text;
        }
        const bytes = utf8Bytes(text);
        const size = byteLengthOf(bytes);
        const out = new Uint8Array(size);
        let length = 0;
        for (let i = 0; i < size; i++) {
            if (bytes[i] === 0x25 && i + 2 < size && isHexDigit(bytes[i + 1]) && isHexDigit(bytes[i + 2])) {
                out[length++] = parseInt(String.fromCharCode(bytes[i + 1], bytes[i + 2]), 16);
                i += 2;
            } else {
                out[length++] = bytes[i];
            }
        }
        return utf8Text(new Uint8Array(apply(TYPED_ARRAY.buffer, out, []), 0, length));
    }
    function parseForm(input) {
        const list = [];
        for (const sequence of input.split("&")) {
            if (sequence === "") {
                continue;
            }
            const equals = sequence.indexOf("=");
            const name = equals < 0 ? sequence : sequence.slice(0, equals);
            const value = equals < 0 ? "" : sequence.slice(equals + 1);
            list.push([percentDecode(name.replace(/\+/g, " ")), percentDecode(value.replace(/\+/g, " "))]);
        }
        return list;
    }

    let linkParams;
    let replaceParams;
    let setUrlQuery;

    class URLSearchParams {
        #list = [];
        #url = null;
        static {
            linkParams = (params, url) => { params.#url = url; };
            replaceParams = (params, query) => { params.#list = parseForm(query); };
        }
        constructor(init) {
            if (init === undefined || init === null) {
                return;
            }
            if (typeof init === "object" || typeof init === "function") {
                if (typeof init[Symbol.iterator] === "function") {
                    for (const pair of init) {
                        const entry = apply(arrayFrom, Array, [pair]);
                        if (entry.length !== 2) {
                            throw new TypeError("URLSearchParams: each pair must be [name, value]");
                        }
                        this.#list.push([String(entry[0]).toWellFormed(), String(entry[1]).toWellFormed()]);
                    }
                } else {
                    for (const name of Object.keys(init)) {
                        this.#list.push([name.toWellFormed(), String(init[name]).toWellFormed()]);
                    }
                }
                return;
            }
            init = String(init);
            this.#list = parseForm(init.startsWith("?") ? init.slice(1) : init);
        }
        #update() {
            if (this.#url !== null) {
                setUrlQuery(this.#url, this.toString());
            }
        }
        get size() { return this.#list.length; }
        append(name, value) {
            this.#list.push([String(name).toWellFormed(), String(value).toWellFormed()]);
            this.#update();
        }
        delete(name, value) {
            name = String(name);
            value = value === undefined ? undefined : String(value);
            this.#list = this.#list.filter(pair => pair[0] !== name || (value !== undefined && pair[1] !== value));
            this.#update();
        }
        get(name) {
            name = String(name);
            const pair = this.#list.find(candidate => candidate[0] === name);
            return pair === undefined ? null : pair[1];
        }
        getAll(name) {
            name = String(name);
            return this.#list.filter(pair => pair[0] === name).map(pair => pair[1]);
        }
        has(name, value) {
            name = String(name);
            value = value === undefined ? undefined : String(value);
            return this.#list.some(pair => pair[0] === name && (value === undefined || pair[1] === value));
        }
        set(name, value) {
            name = String(name).toWellFormed();
            value = String(value).toWellFormed();
            const index = this.#list.findIndex(pair => pair[0] === name);
            if (index < 0) {
                this.#list.push([name, value]);
            } else {
                this.#list[index] = [name, value];
                this.#list = this.#list.filter((pair, i) => i <= index || pair[0] !== name);
            }
            this.#update();
        }
        sort() {
            this.#list.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
            this.#update();
        }
        entries() { return this.#list.map(pair => [pair[0], pair[1]]); }
        keys() { return this.#list.map(pair => pair[0]); }
        values() { return this.#list.map(pair => pair[1]); }
        forEach(callback, thisArg) {
            for (const [name, value] of this.entries()) {
                callback.call(thisArg, value, name, this);
            }
        }
        toString() { return this.#list.map(pair => formEncode(pair[0]) + "=" + formEncode(pair[1])).join("&"); }
        [Symbol.iterator]() { return this.entries()[Symbol.iterator](); }
    }

)JS",
R"JS(    const USERINFO_ENCODE = "\"#/:;<=>?@[\\]^`{|}";
    function percentEncode(text, set) {
        let out = "";
        for (const byte of utf8Bytes(text)) {
            const c = String.fromCharCode(byte);
            out += byte <= 0x20 || byte >= 0x7F || set.includes(c) ? HEX[byte] : c;
        }
        return out;
    }
    function serializeUrl(record) {
        let out = record.scheme + ":";
        if (record.host !== null) {
            out += "//";
            if (record.username !== "" || record.password !== "") {
                out += record.username + (record.password !== "" ? ":" + record.password : "") + "@";
            }
            out += record.host + (record.port !== null ? ":" + record.port : "");
        } else if (!record.opaque && record.path.startsWith("//")) {
            out += "/.";
        }
        out += record.path;
        if (record.query !== null) {
            out += "?" + record.query;
        }
        if (record.fragment !== null) {
            out += "#" + record.fragment;
        }
        return out;
    }
    // A NUL would end the string on its way to the native parser
    function urlInput(url) {
        return String(url).replace(/^[\0-\x20]+|[\0-\x20]+$/g, "").replace(/\0/g, "%00");
    }
    function parseUrl(url, base) {
        return native.parseUrl(urlInput(url), base === undefined ? undefined : urlInput(base));
    }
    const SPECIAL_SCHEMES = ["ftp", "file", "http", "https", "ws", "wss"];
    // Where a host and port setter's value ends
    function authorityPart(value, special) {
        const end = value.search(special ? /[\/?#\\]/ : /[\/?#]/);
        return end < 0 ? value : value.slice(0, end);
    }
    // The index of the port's ":" in host[:port], outside an IPv6 address's brackets, or -1
    function portColon(value) {
        const close = value.lastIndexOf("]");
        return value.indexOf(":", close < 0 ? 0 : close);
    }

    class URL {
        #record;
        #params;
        static {
            setUrlQuery = (url, query) => { url.#record.query = query === "" ? null : query; };
        }
        static canParse(url, base) {
            return parseUrl(url, base) !== null;
        }
        constructor(url, base) {
            if (arguments.length < 1) {
                throw new TypeError("URL: 1 argument required");
            }
            const record = parseUrl(url, base);
            if (record === null) {
                throw new TypeError("URL: invalid URL: " + String(url) + (base === undefined ? "" : " against " + String(base)));
            }
            this.#record = record;
            this.#params = new URLSearchParams(record.query === null ? "" : record.query);
            linkParams(this.#params, this);
        }
        #adopt(record) {
            this.#record = record;
            replaceParams(this.#params, record.query === null ? "" : record.query);
        }
        // Setters edit one part and reparse the whole, which leaves the URL unchanged when the result does not parse
        #reparse(changes) {
            const record = parseUrl(serializeUrl(Object.assign({}, this.#record, changes)));
            if (record !== null) {
                this.#adopt(record);
            }
        }
        #hasCredentialsHost() {
            return this.#record.host !== null && this.#record.host !== "" && this.#record.scheme !== "file";
        }
        get href() { return serializeUrl(this.#record); }
        set href(value) {
            const record = parseUrl(value);
            if (record === null) {
                throw new TypeError("URL: invalid URL: " + String(value));
            }
            this.#adopt(record);
        }
        get origin() {
            let record = this.#record;
            if (record.scheme === "blob") {
                record = parseUrl(record.path);
                if (record === null || (record.scheme !== "http" && record.scheme !== "https")) {
                    return "null";
                }
            }
            if (record.scheme === "file" || !SPECIAL_SCHEMES.includes(record.scheme)) {
                return "null";
            }
            return record.scheme + "://" + record.host + (record.port !== null ? ":" + record.port : "");
        }
        get protocol() { return this.#record.scheme + ":"; }
        set protocol(value) {
            const scheme = String(value).split(":")[0].toLowerCase();
            const record = this.#record;
            if (!/^[a-z][a-z0-9+\-.]*$/.test(scheme) ||
                SPECIAL_SCHEMES.includes(scheme) !== SPECIAL_SCHEMES.includes(record.scheme) ||
                (scheme === "file" && (record.username !== "" || record.password !== "" || record.port !== null))) {
                return;
            }
            this.#reparse({ scheme: scheme });
        }
        get username() { return this.#record.username; }
        set username(value) {
            if (this.#hasCredentialsHost()) {
                this.#reparse({ username: percentEncode(String(value), USERINFO_ENCODE) });
            }
        }
        get password() { return this.#record.password; }
        set password(value) {
            if (this.#hasCredentialsHost()) {
                this.#reparse({ password: percentEncode(String(value), USERINFO_ENCODE) });
            }
        }
        get host() {
            const record = this.#record;
            return record.host === null ? "" : record.host + (record.port !== null ? ":" + record.port : "");
        }
        set host(value) {
            if (this.#record.opaque) {
                return;
            }
            value = authorityPart(String(value), SPECIAL_SCHEMES.includes(this.#record.scheme));
            const colon = portColon(value);
            if (colon < 0) {
                this.#reparse({ host: value });
                return;
            }
            const port = /^\d*/.exec(value.slice(colon + 1))[0];
            this.#reparse(port === "" ? { host: value.slice(0, colon) } : { host: value.slice(0, colon), port: port });
        }
        get hostname() { return this.#record.host === null ? "" : this.#record.host; }
        set hostname(value) {
            value = authorityPart(String(value), SPECIAL_SCHEMES.includes(this.#record.scheme));
            if (!this.#record.opaque && portColon(value) < 0) {
                this.#reparse({ host: value });
            }
        }
        get port() { return this.#record.port === null ? "" : String(this.#record.port); }
        set port(value) {
            if (!this.#hasCredentialsHost()) {
                return;
            }
            value = String(value);
            const digits = /^\d*/.exec(value)[0];
            if (value === "") {
                this.#reparse({ port: null });
            } else if (digits !== "" && Number(digits) <= 65535) {
                this.#reparse({ port: Number(digits) });
            }
        }
        get pathname() { return this.#record.path; }
        set pathname(value) {
            if (this.#record.opaque) {
                return;
            }
            let path = String(value).replace(/\?/g, "%3F").replace(/#/g, "%23");
            if (!path.startsWith("/") && (this.#record.host !== null || SPECIAL_SCHEMES.includes(this.#record.scheme))) {
                path = "/" + path;
            }
            this.#reparse({ path: path });
        }
        get search() { return this.#record.query ? "?" + this.#record.query : ""; }
        set search(value) {
            value = String(value);
            const query = value.startsWith("?") ? value.slice(1) : value;
            this.#reparse({ query: value === "" ? null : query.replace(/#/g, "%23") });
        }
        get searchParams() { return this.#params; }
        get hash() { return this.#record.fragment ? "#" + this.#record.fragment : ""; }
        set hash(value) {
            value = String(value);
            const fragment = value.startsWith("#") ? value.slice(1) : value;
            this.#reparse({ fragment: value === "" ? null : fragment });
        }
        toString() { return this.href; }
        toJSON() { return this.href; }
    }

    return { atob, btoa, crypto, DOMException, performance, structuredClone, TextDecoder, TextEncoder, URL, URLSearchParams };
}))JS"
};

QString webGlobalsUrlPrelude() {
    QString prelude;
    for (const char* part : WEB_GLOBALS_URL_PRELUDE_PARTS) {
        prelude += QString::fromUtf8(part);
    }
    return prelude;
}
