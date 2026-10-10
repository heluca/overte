//
//  FetchPrelude.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "FetchClass.h"

// The WHATWG classes are plain script, so they get private state, instanceof and iteration for free; the native part in
// FetchClass.cpp is only the transport, which settles through the promise bridge. The prelude is called with the native
// functions and returns the globals, so nothing internal is reachable from scripts.
const char FETCH_PRELUDE[] = R"JS((function (native) {
    "use strict";
    const TOKEN = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;
    const NORMALIZED_METHODS = ["DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"];
    const FORBIDDEN_METHODS = ["CONNECT", "TRACE", "TRACK"];
    const internal = {};
    const requestState = new WeakMap();
    const signalState = new WeakMap();

    function headerName(name) {
        name = String(name);
        if (!TOKEN.test(name)) {
            throw new TypeError("Invalid header name: " + name);
        }
        return name.toLowerCase();
    }
    function headerValue(value) {
        return String(value).replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, "");
    }
    function bodyBytes(body) {
        if (body === undefined || body === null) {
            return null;
        }
        if (body instanceof ArrayBuffer) {
            return body;
        }
        if (ArrayBuffer.isView(body)) {
            return body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength);
        }
        return String(body);
    }
    function abortError() {
        const error = new Error("The operation was aborted.");
        error.name = "AbortError";
        return error;
    }

    class Headers {
        #map = new Map();
        constructor(init) {
            if (init === undefined || init === null) {
                return;
            }
            if (typeof init !== "object") {
                throw new TypeError("Headers: init must be an object, an array of pairs or Headers");
            }
            if (typeof init[Symbol.iterator] === "function") {
                for (const pair of init) {
                    if (pair.length !== 2) {
                        throw new TypeError("Headers: each pair must be [name, value]");
                    }
                    this.append(pair[0], pair[1]);
                }
            } else {
                for (const name of Object.keys(init)) {
                    this.append(name, init[name]);
                }
            }
        }
        append(name, value) {
            const key = headerName(name);
            const existing = this.#map.get(key);
            value = headerValue(value);
            this.#map.set(key, existing === undefined ? value : existing + ", " + value);
        }
        delete(name) { this.#map.delete(headerName(name)); }
        get(name) {
            const value = this.#map.get(headerName(name));
            return value === undefined ? null : value;
        }
        has(name) { return this.#map.has(headerName(name)); }
        set(name, value) { this.#map.set(headerName(name), headerValue(value)); }
        entries() { return [...this.#map.entries()].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)); }
        keys() { return this.entries().map(pair => pair[0]); }
        values() { return this.entries().map(pair => pair[1]); }
        forEach(callback, thisArg) {
            for (const [name, value] of this.entries()) {
                callback.call(thisArg, value, name, this);
            }
        }
        [Symbol.iterator]() { return this.entries()[Symbol.iterator](); }
    }

    class AbortSignal {
        constructor(token) {
            if (token !== internal) {
                throw new TypeError("Illegal constructor; use new AbortController().signal");
            }
            signalState.set(this, { aborted: false, reason: undefined, listeners: [] });
            this.onabort = null;
        }
        get aborted() { return signalState.get(this).aborted; }
        get reason() { return signalState.get(this).reason; }
        throwIfAborted() {
            if (this.aborted) {
                throw this.reason;
            }
        }
        addEventListener(type, listener) {
            const listeners = signalState.get(this).listeners;
            if (type === "abort" && typeof listener === "function" && !listeners.includes(listener)) {
                listeners.push(listener);
            }
        }
        removeEventListener(type, listener) {
            const listeners = signalState.get(this).listeners;
            const index = listeners.indexOf(listener);
            if (type === "abort" && index >= 0) {
                listeners.splice(index, 1);
            }
        }
    }
    function abortSignal(signal, reason) {
        const state = signalState.get(signal);
        if (state.aborted) {
            return;
        }
        state.aborted = true;
        state.reason = reason === undefined ? abortError() : reason;
        const event = { type: "abort", target: signal };
        const listeners = (typeof signal.onabort === "function" ? [signal.onabort] : []).concat(state.listeners);
        for (const listener of listeners) {
            try {
                listener.call(signal, event);
            } catch (error) {
                // A throwing listener must not stop the others, nor the abort; report it as unhandled instead
                Promise.reject(error);
            }
        }
    }

    class AbortController {
        #signal = new AbortSignal(internal);
        get signal() { return this.#signal; }
        abort(reason) { abortSignal(this.#signal, reason); }
    }

    class Request {
        #url;
        #method;
        #headers;
        constructor(input, init) {
            init = init === undefined || init === null ? {} : init;
            let url, method = "GET", headers, body = null, signal = null;
            if (input instanceof Request) {
                const state = requestState.get(input);
                url = input.url;
                method = input.method;
                headers = input.headers;
                body = state.body;
                signal = state.signal;
            } else {
                url = native.resolveUrl(String(input));
            }
            if (init.method !== undefined) {
                method = String(init.method);
                if (!TOKEN.test(method)) {
                    throw new TypeError("Invalid method: " + method);
                }
                const upper = method.toUpperCase();
                if (FORBIDDEN_METHODS.includes(upper)) {
                    throw new TypeError("Forbidden method: " + method);
                }
                if (NORMALIZED_METHODS.includes(upper)) {
                    method = upper;
                }
            }
            if (init.headers !== undefined) {
                headers = init.headers;
            }
            if (init.body !== undefined) {
                body = bodyBytes(init.body);
            }
            if (init.signal !== undefined) {
                if (init.signal !== null && !(init.signal instanceof AbortSignal)) {
                    throw new TypeError("Request: signal must be an AbortSignal");
                }
                signal = init.signal;
            }
            if (body !== null && (method === "GET" || method === "HEAD")) {
                throw new TypeError("Request: a " + method + " request cannot have a body");
            }
            this.#url = url;
            this.#method = method;
            this.#headers = new Headers(headers);
            if (typeof body === "string" && !this.#headers.has("content-type")) {
                this.#headers.set("content-type", "text/plain;charset=UTF-8");
            }
            requestState.set(this, { body: body, signal: signal });
        }
        get url() { return this.#url; }
        get method() { return this.#method; }
        get headers() { return this.#headers; }
    }

    class Response {
        #status;
        #statusText;
        #url = "";
        #redirected = false;
        #headers;
        #body;
        #bodyUsed = false;
        constructor(body, init) {
            if (body === internal) {
                this.#status = init.status;
                this.#statusText = init.statusText;
                this.#url = init.url;
                this.#redirected = init.redirected;
                this.#headers = new Headers(init.headers);
                this.#body = init.body;
                return;
            }
            init = init === undefined || init === null ? {} : init;
            const status = init.status === undefined ? 200 : Number(init.status);
            if (!Number.isInteger(status) || status < 200 || status > 599) {
                throw new RangeError("Response: status must be an integer from 200 to 599");
            }
            this.#status = status;
            this.#statusText = init.statusText === undefined ? "" : String(init.statusText);
            this.#headers = new Headers(init.headers);
            body = bodyBytes(body);
            if (typeof body === "string") {
                if (!this.#headers.has("content-type")) {
                    this.#headers.set("content-type", "text/plain;charset=UTF-8");
                }
                body = native.encodeText(body);
            }
            this.#body = body;
        }
        get status() { return this.#status; }
        get statusText() { return this.#statusText; }
        get ok() { return this.#status >= 200 && this.#status <= 299; }
        get url() { return this.#url; }
        get redirected() { return this.#redirected; }
        get headers() { return this.#headers; }
        get bodyUsed() { return this.#bodyUsed; }
        #consume() {
            if (this.#bodyUsed) {
                return Promise.reject(new TypeError("Response: body has already been read"));
            }
            this.#bodyUsed = true;
            return Promise.resolve(this.#body === null ? new ArrayBuffer(0) : this.#body);
        }
        arrayBuffer() { return this.#consume(); }
        text() { return this.#consume().then(bytes => native.decodeText(bytes)); }
        json() { return this.text().then(text => JSON.parse(text)); }
    }

    function fetch(input, init) {
        return new Promise(function (resolve, reject) {
            const request = new Request(input, init);
            const state = requestState.get(request);
            const signal = state.signal;
            if (signal && signal.aborted) {
                reject(signal.reason);
                return;
            }
            const transfer = native.start(request.method, request.url, request.headers.entries(), state.body);
            let settled = false;
            function onAbort() {
                if (!settled) {
                    settled = true;
                    transfer.control.abort();
                    reject(signal.reason);
                }
            }
            function finish() {
                settled = true;
                if (signal) {
                    signal.removeEventListener("abort", onAbort);
                }
            }
            if (signal) {
                signal.addEventListener("abort", onAbort);
            }
            transfer.promise.then(function (record) {
                if (!settled) {
                    finish();
                    resolve(new Response(internal, record));
                }
            }, function (error) {
                if (!settled) {
                    finish();
                    reject(new TypeError("fetch failed: " + error.message));
                }
            });
        });
    }

    return { fetch, Headers, Request, Response, AbortController, AbortSignal };
}))JS";
