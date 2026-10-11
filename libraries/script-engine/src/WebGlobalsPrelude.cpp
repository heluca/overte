//
//  WebGlobalsPrelude.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "WebGlobalsClass.h"

// The same shape as the fetch prelude: called with the natives from WebGlobalsClass.cpp and v8/WebGlobalsV8.cpp, it
// returns the globals, so nothing internal is reachable from scripts. It ends in WebGlobalsUrlPrelude.cpp.
// It is kept in parts because MSVC refuses a string literal over about 16 KB (C2026).
static const char* const WEB_GLOBALS_PRELUDE_PARTS[] = {
R"JS((function (native) {
    "use strict";
    const toStringTag = value => Object.prototype.toString.call(value).slice(8, -1);
    const HEX = Array.from({ length: 256 }, (_, i) => "%" + (i < 16 ? "0" : "") + i.toString(16).toUpperCase());

    // Not instanceof: an entity script's buffers come from the realm of its own context
    function bytesOf(source, what) {
        if (ArrayBuffer.isView(source)) {
            return new Uint8Array(source.buffer, source.byteOffset, source.byteLength);
        }
        const tag = toStringTag(source);
        if (tag === "ArrayBuffer" || tag === "SharedArrayBuffer") {
            return new Uint8Array(source);
        }
        throw new TypeError(what + ": expected an ArrayBuffer or an ArrayBufferView");
    }
    function utf8Bytes(text) {
        return new Uint8Array(native.encodeUtf8(String(text)));
    }
    function utf8Text(bytes) {
        return native.decodeUtf8(bytes, false, false)[0];
    }

    const DOM_EXCEPTION_CODES = {
        InvalidCharacterError: 5, NotSupportedError: 9, TypeMismatchError: 17, QuotaExceededError: 22, DataCloneError: 25
    };
    class DOMException extends Error {
        #name;
        constructor(message, name) {
            super(message === undefined ? "" : String(message));
            this.#name = name === undefined ? "Error" : String(name);
        }
        get name() { return this.#name; }
        get code() { return DOM_EXCEPTION_CODES[this.#name] || 0; }
    }

    class TextEncoder {
        get encoding() { return "utf-8"; }
        encode(input) { return utf8Bytes(input === undefined ? "" : input); }
        encodeInto(source, destination) {
            if (toStringTag(destination) !== "Uint8Array") {
                throw new TypeError("TextEncoder.encodeInto: the destination must be a Uint8Array");
            }
            source = String(source);
            let read = 0;
            let written = 0;
            while (read < source.length) {
                let point = source.codePointAt(read);
                const units = point > 0xFFFF ? 2 : 1;
                if (point >= 0xD800 && point <= 0xDFFF) {
                    point = 0xFFFD;
                }
                const size = point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
                if (written + size > destination.length) {
                    break;
                }
                if (size === 1) {
                    destination[written] = point;
                } else {
                    destination[written] = [0, 0, 0xC0, 0xE0, 0xF0][size] | (point >> (6 * (size - 1)));
                    for (let i = 1; i < size; i++) {
                        destination[written + i] = 0x80 | ((point >> (6 * (size - 1 - i))) & 0x3F);
                    }
                }
                written += size;
                read += units;
            }
            return { read: read, written: written };
        }
    }

    const UTF8_LABELS = ["unicode-1-1-utf-8", "unicode11utf8", "unicode20utf8", "utf-8", "utf8", "x-unicode20utf8"];
    class TextDecoder {
        #fatal;
        #ignoreBOM;
        #pending = null;
        #bomSeen = false;
        constructor(label, options) {
            label = label === undefined ? "utf-8" : String(label).replace(/^[\t\n\f\r ]+|[\t\n\f\r ]+$/g, "").toLowerCase();
            if (!UTF8_LABELS.includes(label)) {
                throw new RangeError("TextDecoder: the encoding \"" + label + "\" is not supported; only UTF-8 is");
            }
            options = options === undefined || options === null ? {} : options;
            this.#fatal = Boolean(options.fatal);
            this.#ignoreBOM = Boolean(options.ignoreBOM);
        }
        get encoding() { return "utf-8"; }
        get fatal() { return this.#fatal; }
        get ignoreBOM() { return this.#ignoreBOM; }
        decode(input, options) {
            const stream = Boolean(options && options.stream);
            let bytes = input === undefined ? new Uint8Array(0) : bytesOf(input, "TextDecoder.decode");
            if (this.#pending !== null) {
                const joined = new Uint8Array(this.#pending.length + bytes.length);
                joined.set(this.#pending);
                joined.set(bytes, this.#pending.length);
                bytes = joined;
                this.#pending = null;
            }
            const result = native.decodeUtf8(bytes, this.#fatal, stream);
            if (result === null) {
                this.#bomSeen = false;
                throw new TypeError("TextDecoder.decode: the data is not valid UTF-8");
            }
            let text = result[0];
            if (result[1] > 0) {
                this.#pending = bytes.slice(bytes.length - result[1]);
            }
            if (!this.#bomSeen && text.length > 0) {
                this.#bomSeen = true;
                if (!this.#ignoreBOM && text.charCodeAt(0) === 0xFEFF) {
                    text = text.slice(1);
                }
            }
            if (!stream) {
                this.#bomSeen = false;
            }
            return text;
        }
    }

    function btoa(data) {
        if (arguments.length < 1) {
            throw new TypeError("btoa: 1 argument required");
        }
        data = String(data);
        if (/[^\x00-\xFF]/.test(data)) {
            throw new DOMException("btoa: the string contains a character outside Latin-1", "InvalidCharacterError");
        }
        return native.base64Encode(data);
    }
    function atob(data) {
        if (arguments.length < 1) {
            throw new TypeError("atob: 1 argument required");
        }
        data = String(data).replace(/[\t\n\f\r ]+/g, "");
        if (data.length % 4 === 0) {
            data = data.replace(/={1,2}$/, "");
        }
        if (data.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(data)) {
            throw new DOMException("atob: the string is not valid base64", "InvalidCharacterError");
        }
        return native.base64Decode(data);
    }

)JS",
R"JS(    const INTEGER_ARRAYS = ["Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array",
        "Uint32Array", "BigInt64Array", "BigUint64Array"];
    const DIGESTS = ["SHA-1", "SHA-256", "SHA-384", "SHA-512"];
    const crypto = {
        getRandomValues(array) {
            if (!ArrayBuffer.isView(array)) {
                throw new TypeError("crypto.getRandomValues: expected an integer typed array");
            }
            if (!INTEGER_ARRAYS.includes(toStringTag(array))) {
                throw new DOMException("crypto.getRandomValues: " + toStringTag(array) + " is not an integer typed array",
                    "TypeMismatchError");
            }
            if (array.byteLength > 65536) {
                throw new DOMException("crypto.getRandomValues: " + array.byteLength +
                    " bytes were asked for; the limit is 65536", "QuotaExceededError");
            }
            new Uint8Array(array.buffer, array.byteOffset, array.byteLength)
                .set(new Uint8Array(native.randomBytes(array.byteLength)));
            return array;
        },
        randomUUID() { return native.randomUUID(); },
        subtle: {
            digest(algorithm, data) {
                try {
                    let name = algorithm;
                    if (typeof algorithm === "object" && algorithm !== null) {
                        if (algorithm.name === undefined) {
                            throw new TypeError("crypto.subtle.digest: the algorithm has no name");
                        }
                        name = algorithm.name;
                    }
                    name = String(name).toUpperCase();
                    if (!DIGESTS.includes(name)) {
                        throw new DOMException("crypto.subtle.digest: " + name + " is not supported; use SHA-1, " +
                            "SHA-256, SHA-384 or SHA-512", "NotSupportedError");
                    }
                    return native.digest(name, bytesOf(data, "crypto.subtle.digest").slice().buffer);
                } catch (error) {
                    return Promise.reject(error);
                }
            }
        }
    };

    function structuredClone(value, options) {
        if (arguments.length < 1) {
            throw new TypeError("structuredClone: 1 argument required");
        }
        let transfer = [];
        if (options !== undefined && options !== null && options.transfer !== undefined) {
            if (typeof options.transfer[Symbol.iterator] !== "function") {
                throw new TypeError("structuredClone: transfer must be iterable");
            }
            transfer = Array.from(options.transfer);
        }
        const result = native.clone(value, transfer);
        if (!result[0]) {
            throw new DOMException(result[1], "DataCloneError");
        }
        return result[1];
    }

    const timeOrigin = Date.now();
    const monotonicOrigin = native.monotonicMs();
    const performance = {
        // Coarsened to 100 us, as browsers do without cross-origin isolation
        now() { return Math.floor((native.monotonicMs() - monotonicOrigin) * 10) / 10; },
        toJSON() { return { timeOrigin: timeOrigin }; }
    };
    Object.defineProperty(performance, "timeOrigin", { value: timeOrigin, enumerable: true });

)JS"
};

QString webGlobalsPrelude() {
    QString prelude;
    for (const char* part : WEB_GLOBALS_PRELUDE_PARTS) {
        prelude += QString::fromUtf8(part);
    }
    return prelude + webGlobalsUrlPrelude();
}
