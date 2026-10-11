//
//  WebGlobalsTamperCases.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_WebGlobalsTamperCases_h
#define overte_WebGlobalsTamperCases_h

// Replaces the built-ins the prelude captures at start
static const char* const REPLACED_BUILTINS_SCRIPT = R"JS(
var r = [];
var realByteLength = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Int8Array.prototype), "byteLength").get;
var lengthOf = function (view) { return realByteLength.call(view); };
var destination = new Uint8Array(1);
var words = new Uint32Array(4);
var dataView = new DataView(new Int8Array([0, 104, 105]).buffer, 1);
function attempt(fn) { try { return fn(); } catch (e) { return e.name; } }
function hex(buffer) {
    return Array.prototype.map.call(new Int8Array(buffer), function (b) { return ((b & 0xFF) + 0x100).toString(16).slice(1); }).join("");
}
var TypedArrayPrototype = Object.getPrototypeOf(Int8Array.prototype);
var originals = { String: String, Uint8Array: Uint8Array, from: Array.from, isView: ArrayBuffer.isView,
    toString: Object.prototype.toString, set: TypedArrayPrototype.set, slice: TypedArrayPrototype.slice,
    subarray: TypedArrayPrototype.subarray, descriptors: Object.getOwnPropertyDescriptors(TypedArrayPrototype) };
function restore() {
    String = originals.String;
    Uint8Array = originals.Uint8Array;
    Array.from = originals.from;
    ArrayBuffer.isView = originals.isView;
    Object.prototype.toString = originals.toString;
    TypedArrayPrototype.set = originals.set;
    TypedArrayPrototype.slice = originals.slice;
    TypedArrayPrototype.subarray = originals.subarray;
    ["byteLength", "byteOffset", "length", "buffer"].forEach(function (name) {
        Object.defineProperty(TypedArrayPrototype, name, originals.descriptors[name]);
    });
}
String = function () { return 42; };
Uint8Array = function () { return { length: 1e9 }; };
Array.from = function () { return "not an array"; };
ArrayBuffer.isView = function () { return true; };
Object.prototype.toString = function () { return "[object Uint8Array]"; };
["byteLength", "byteOffset", "length", "buffer"].forEach(function (name) {
    Object.defineProperty(TypedArrayPrototype, name, { get: function () { return 1e9; } });
});
TypedArrayPrototype.set = function () {};
TypedArrayPrototype.slice = function () { return {}; };
TypedArrayPrototype.subarray = function () { return {}; };
r.push(lengthOf(new TextEncoder().encode("hi")), new TextEncoder().encodeInto("hi", destination).written);
var decoder = new TextDecoder();
r.push(decoder.decode(new Int8Array([104, 105])), decoder.decode(dataView),
    attempt(function () { return decoder.decode({}); }));
r.push(decoder.decode(new Int8Array([-16, -97]), { stream: true }), decoder.decode(new Int8Array([-104, -128])).codePointAt(0));
r.push(btoa("hi"), atob("aGk="), crypto.getRandomValues(words) === words,
    attempt(function () { return crypto.getRandomValues({}); }),
    attempt(function () { return crypto.getRandomValues(new Float32Array(2)); }));
var clone = structuredClone([1, 2], { transfer: [] });
r.push(Array.isArray(clone) && clone.length === 2,
    attempt(function () { return structuredClone(1, { transfer: { [Symbol.iterator]: function () { throw new RangeError("it"); } } }); }),
    attempt(function () { return structuredClone({ get x() { throw new SyntaxError("getter"); } }); }));
r.push(new URL("http://example.com/a?x=1").searchParams.get("x"), new URLSearchParams("a=%41").get("a"),
    new URLSearchParams([["k", "v"]]).toString());
var hashed = crypto.subtle.digest("SHA-1", new Int8Array([104, 105]));
var refused = crypto.subtle.digest("SHA-1", {});
// Put back before the promise callbacks run: hex() below maps over an Int8Array, whose length getter is replaced
restore();
hashed.then(function (hash) {
    r.push(hex(hash));
    return refused;
}).catch(function (e) {
    r.push(e.name);
}).then(function () {
    print(JSON.stringify(r));
    Script.stop(true);
});
)JS";

// Replaces methods the prelude still looks up when called, and tries the transfer list's own checks
static const char* const NATIVE_CHECKS_SCRIPT = R"JS(
// The prelude passes on what live lookups return; the natives reject it
var r = [];
function attempt(fn) { try { var value = fn(); return typeof value === "string" ? value : "returned"; } catch (e) { return e.name; } }
var realByteLength = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, "byteLength").get;
var moved = new Int8Array([5, 6, 7]).buffer;
var replace = String.prototype.replace, test = RegExp.prototype.test, includes = Array.prototype.includes;
String.prototype.replace = function () { return { replace: function () { return 5; } }; };
RegExp.prototype.test = function () { return false; };
Array.prototype.includes = function () { return true; };
Array.from = function () { return "not an array"; };
r.push(attempt(function () { return atob("aGk="); }), attempt(function () { return new URL("http://example.com/"); }),
    attempt(function () { return btoa("\u0100"); }));
var unsupported = crypto.subtle.digest("MD5", new Int8Array(1));
var movedCopy = structuredClone({ b: moved }, { transfer: [moved] });
r.push(realByteLength.call(moved), realByteLength.call(movedCopy.b), new Int8Array(movedCopy.b)[2]);
String.prototype.replace = replace;
RegExp.prototype.test = test;
Array.prototype.includes = includes;
r.push(attempt(function () { return structuredClone(1, { transfer: [1] }); }),
    attempt(function () { return structuredClone(1, { transfer: [{}] }); }));
var victim = new ArrayBuffer(4);
r.push(attempt(function () {
    return structuredClone({ get first() { structuredClone(victim, { transfer: [victim] }); return 1; }, victim: victim },
        { transfer: [victim] });
}), realByteLength.call(victim));
unsupported.then(function () { r.push("resolved"); }, function (e) { r.push(e.name); }).then(function () {
    print(JSON.stringify(r));
    Script.stop(true);
});
)JS";

#endif // overte_WebGlobalsTamperCases_h
