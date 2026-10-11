//
//  WebGlobalsTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "WebGlobalsTests.h"
#include "DependencyManager.h"
#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "ScriptCache.h"
#include "ScriptEngines.h"
#include "StatTracker.h"
#include "WebGlobalsUrlCases.h"

QTEST_MAIN(WebGlobalsTests)

// The expected results are what Node 24 (a WHATWG implementation) prints for the same scripts, except where noted.

void WebGlobalsTests::initTestCase() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

// Generous: a test only reaches this when a promise never settles, and then fails on the missing output
static const int WEB_GLOBALS_TEST_TIMEOUT_MS = 10000;

bool WebGlobalsTests::runScript(const QString& source, QStringList& printed, const QString& closureSource) {
    // NETWORKLESS_TEST_SCRIPT, the context with the fewest globals; nothing is registered by hand, so this goes through
    // ScriptManager::init(), which registers the web globals for every context before any context check
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, source, "webGlobalsTest.js");
    sm->setAbortOnUncaughtException(true);
    auto scopeGuard = sm->engine()->getScopeGuard();
    ClosureRunner closure(sm->engine().get(), closureSource);
    sm->engine()->registerGlobalObject(scopeGuard.get(), "closure", &closure);

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName) {
        qDebug() << "Printed:" << message;
        printed.append(message);
    });
    connect(sm.get(), &ScriptManager::errorMessage, [](const QString& message, const QString& engineName) {
        qCritical() << "Error:" << message;
    });
    connect(sm.get(), &ScriptManager::unhandledException, [](std::shared_ptr<ScriptException> exception) {
        qWarning() << "Exception:" << exception;
    });
    bool timedOut = false;
    QTimer::singleShot(WEB_GLOBALS_TEST_TIMEOUT_MS, sm.get(), [manager = sm.get(), &timedOut] {
        timedOut = true;
        manager->stop();
    });

    sm->run();
    return !timedOut && !sm->getUncaughtException();
}

static const char* const GLOBAL_TYPES =
    "var types = ['TextEncoder', 'TextDecoder', 'atob', 'btoa', 'crypto', 'structuredClone', 'performance', 'URL',\n"
    "    'URLSearchParams', 'DOMException'].map(function (name) { return typeof globalThis[name]; });\n"
    "print(types.join(',') + '|' + [typeof crypto.getRandomValues, typeof crypto.randomUUID, typeof crypto.subtle.digest,\n"
    "    typeof performance.now, typeof URL.canParse, typeof native].join(','));\n";

void WebGlobalsTests::testPresentEverywhere() {
    QStringList printed;
    QVERIFY(runScript(QString(GLOBAL_TYPES) + "closure.run();\nScript.stop(true);\n", printed, GLOBAL_TYPES));
    const QString expected("function,function,function,function,object,function,object,function,function,function|"
                           "function,function,function,function,function,undefined");
    QCOMPARE(printed, QStringList({ expected, expected }));
}

void WebGlobalsTests::testTextEncoder() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
var enc = new TextEncoder();
r.push(enc.encoding, Array.from(enc.encode("a\u00e9\u20ac\ud83d\ude00\ud800x")).join(" "), enc.encode().length,
    Array.from(enc.encode("a\u0000b")).join(" "));
var dest = new Uint8Array(4);
var res = enc.encodeInto("a\ud83d\ude00b", dest);
r.push(res.read, res.written, Array.from(dest).join(" "));
dest = new Uint8Array(6);
res = enc.encodeInto("a\ud83d\ude00b\udc00", dest);
r.push(res.read, res.written, Array.from(dest).join(" "));
res = enc.encodeInto("\u00e9\u00e9", new Uint8Array(3));
r.push(res.read, res.written);
try { enc.encodeInto("x", [0]); } catch (e) { r.push(e.name); }
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["utf-8","97 195 169 226 130 172 240 159 152 128 239 191 189 120",0,"97 0 98",1,1,"97 0 0 0",4,6)OUT"
        R"OUT(,"97 240 159 152 128 98",1,2,"TypeError"])OUT" }));
}

void WebGlobalsTests::testTextDecoder() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
function cps(text) { return Array.from(text, function (c) { return c.codePointAt(0).toString(16); }).join(" "); }
function dec(bytes, options) { return cps(new TextDecoder("utf-8", options).decode(new Uint8Array(bytes))); }
r.push(dec([0xE2, 0x82, 0xAC]), dec([0xF0, 0x80, 0x80]), dec([0xED, 0xA0, 0x80]), dec([0xF0, 0x9F, 0x98]),
    dec([0xC0, 0xAF]), dec([0xE0, 0x80, 0xAF]), dec([0xFF]), dec([0xF4, 0x90, 0x80, 0x80]),
    dec([0x61, 0xF1, 0x80, 0x80, 0xE1, 0x80, 0xC2, 0x62, 0x80, 0x63, 0x80, 0xBF, 0x64]),
    dec([0xEF, 0xBB, 0xBF, 0x61]), dec([0xEF, 0xBB, 0xBF, 0x61], { ignoreBOM: true }), dec([0xEF, 0xBB, 0xBF, 0xEF, 0xBB, 0xBF]),
    dec([0x61, 0xEF, 0xBB, 0xBF]), dec([0x61, 0x00, 0x62]));
function fatal(bytes) {
    try { return cps(new TextDecoder("utf-8", { fatal: true }).decode(new Uint8Array(bytes))); } catch (e) { return e.name; }
}
r.push(fatal([0x68, 0x69]), fatal([0xFF]), fatal([0xE2, 0x82]), fatal([0xED, 0xA0, 0x80]));
var d = new TextDecoder();
r.push(d.encoding, d.fatal, d.ignoreBOM, new TextDecoder("utf-8", { fatal: true }).fatal);
r.push(cps(d.decode(new Uint8Array([0xF0, 0x9F]), { stream: true })), cps(d.decode(new Uint8Array([0x98, 0x80, 0x61]), { stream: true })),
    cps(d.decode()));
r.push(cps(d.decode(new Uint8Array([0xEF, 0xBB]), { stream: true })), cps(d.decode(new Uint8Array([0xBF, 0x62]), { stream: true })),
    cps(d.decode(new Uint8Array([0xE2, 0x82]), { stream: true })), cps(d.decode()));
var f = new TextDecoder("utf-8", { fatal: true });
r.push(cps(f.decode(new Uint8Array([0xE2]), { stream: true })), cps(f.decode(new Uint8Array([0x82, 0xAC]))));
r.push(new TextDecoder(" UTF8 ").encoding, new TextDecoder("unicode-1-1-utf-8").encoding);
try { new TextDecoder("latin1"); } catch (e) { r.push(e.name); }
var buffer = new Uint8Array([0x68, 0x69]).buffer;
r.push(d.decode(buffer), d.decode(new DataView(buffer, 1)), d.decode(new Uint16Array([0x6968])));
try { d.decode("hi"); } catch (e) { r.push(e.name); }
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["20ac","fffd fffd fffd","fffd fffd fffd","fffd","fffd fffd","fffd fffd fffd","fffd","fffd fffd )OUT"
        R"OUT(fffd fffd","61 fffd fffd fffd 62 fffd 63 fffd fffd 64","61","feff 61","feff","61 feff","61 0 62")OUT"
        R"OUT(,"68 69","TypeError","TypeError","TypeError","utf-8",false,false,true,"","1f600 61","","","62",")OUT"
        R"OUT(","fffd","","20ac","utf-8","utf-8","RangeError","hi","i","hi","TypeError"])OUT" }));
}

void WebGlobalsTests::testBase64() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
function name(fn) { try { return JSON.stringify(fn()); } catch (e) { return e.name + (typeof e.code === "number" ? ":" + e.code : ""); } }
r.push(btoa("hello"), btoa("\u0000"), btoa("\u00ff\u0000"), btoa(""), name(function () { return btoa("\u0100"); }));
r.push(atob("aGVsbG8="), atob(" aGVs\nbG8 "), atob("aGVsbG8"), Array.from(atob("/wA="), function (c) { return c.charCodeAt(0); }).join(" "),
    atob("aGk="), atob(""));
r.push(name(function () { return atob("abc$"); }), name(function () { return atob("a"); }), name(function () { return atob("ab=c"); }),
    name(function () { return atob("ab="); }), name(function () { return atob("aGk==="); }), name(function () { return atob(); }));
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["aGVsbG8=","AA==","/wA=","","InvalidCharacterError:5","hello","hello","hello","255 0","hi","",")OUT"
        R"OUT(InvalidCharacterError:5","InvalidCharacterError:5","InvalidCharacterError:5","InvalidCharacterEr)OUT"
        R"OUT(ror:5","InvalidCharacterError:5","TypeError"])OUT" }));
}

void WebGlobalsTests::testRandom() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
function name(fn) { try { fn(); return "ok"; } catch (e) { return e.name + (typeof e.code === "number" ? ":" + e.code : ""); } }
var a = new Uint32Array(8);
r.push(crypto.getRandomValues(a) === a, a.some(function (v) { return v !== 0; }));
["Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array", "Uint32Array", "BigInt64Array",
    "BigUint64Array"].forEach(function (type) {
    var array = new globalThis[type](4);
    r.push(type + ":" + (crypto.getRandomValues(array) === array));
});
r.push(name(function () { crypto.getRandomValues(new Float32Array(4)); }), name(function () { crypto.getRandomValues(new Float64Array(4)); }),
    name(function () { crypto.getRandomValues(new DataView(new ArrayBuffer(4))); }), name(function () { crypto.getRandomValues([0]); }),
    name(function () { crypto.getRandomValues(new Uint8Array(65536)); }), name(function () { crypto.getRandomValues(new Uint8Array(65537)); }),
    name(function () { crypto.getRandomValues(new Uint32Array(16385)); }));
var big = new Uint8Array(64);
crypto.getRandomValues(big.subarray(8, 56));
r.push(big.slice(0, 8).every(function (v) { return v === 0; }) && big.slice(56).every(function (v) { return v === 0; }),
    big.slice(8, 56).some(function (v) { return v !== 0; }));
var uuid = crypto.randomUUID();
r.push(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(uuid), uuid !== crypto.randomUUID());
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT([true,true,"Int8Array:true","Uint8Array:true","Uint8ClampedArray:true","Int16Array:true","Uint16)OUT"
        R"OUT(Array:true","Int32Array:true","Uint32Array:true","BigInt64Array:true","BigUint64Array:true","Typ)OUT"
        R"OUT(eMismatchError:17","TypeMismatchError:17","TypeMismatchError:17","TypeError","ok","QuotaExceeded)OUT"
        R"OUT(Error:22","QuotaExceededError:22",true,true,true,true])OUT" }));
}

void WebGlobalsTests::testDigest() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var hex = function (buffer) {
    return Array.from(new Uint8Array(buffer), function (b) { return (b < 16 ? "0" : "") + b.toString(16); }).join("");
};
var jobs = [];
["SHA-1", "SHA-256", "SHA-384", "SHA-512"].forEach(function (algorithm) {
    ["", "abc"].forEach(function (input) {
        jobs.push(crypto.subtle.digest(algorithm, new TextEncoder().encode(input)).then(function (hash) {
            return algorithm + "(" + input + ")=" + hex(hash);
        }));
    });
});
var settled = false;
var pending = crypto.subtle.digest({ name: "sha-256" }, new Uint8Array([0x61, 0x62, 0x63]).buffer);
pending.then(function () { settled = true; });
var wasSettled = settled;
jobs.push(pending.then(function (hash) { return "object:" + Object.prototype.toString.call(hash) + ":" + hex(hash).slice(0, 8); }));
jobs.push(crypto.subtle.digest("sha-1", new DataView(new Uint8Array([0, 0x61, 0x62, 0x63]).buffer, 1)).then(function (hash) {
    return "view:" + hex(hash).slice(0, 8);
}));
function rejection(promise) {
    return promise.then(function () { return "resolved"; }, function (e) {
        return e.name + ":" + (e instanceof DOMException) + (typeof e.code === "number" ? ":" + e.code : "");
    });
}
jobs.push(rejection(crypto.subtle.digest("MD5", new Uint8Array(0))));
jobs.push(rejection(crypto.subtle.digest({ name: "SHA-3" }, new Uint8Array(0))));
jobs.push(rejection(crypto.subtle.digest("SHA-256", "abc")));
jobs.push(rejection(crypto.subtle.digest({}, new Uint8Array(0))));
Promise.all(jobs).then(function (results) {
    print(JSON.stringify([wasSettled].concat(results)));
    Script.stop(true);
});)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT([false,"SHA-1()=da39a3ee5e6b4b0d3255bfef95601890afd80709","SHA-1(abc)=a9993e364706816aba3e257178)OUT"
        R"OUT(50c26c9cd0d89d","SHA-256()=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SH)OUT"
        R"OUT(A-256(abc)=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-384()=38b060a7)OUT"
        R"OUT(51ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b","SHA-3)OUT"
        R"OUT(84(abc)=cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca1)OUT"
        R"OUT(34c825a7","SHA-512()=cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d8)OUT"
        R"OUT(5f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e","SHA-512(abc)=ddaf35a193617abacc417349ae2)OUT"
        R"OUT(0413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54)OUT"
        R"OUT(ca49f","object:[object ArrayBuffer]:ba7816bf","view:a9993e36","NotSupportedError:true:9","NotSup)OUT"
        R"OUT(portedError:true:9","TypeError:false","TypeError:false"])OUT" }));
}

void WebGlobalsTests::testStructuredClone() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
var source = { n: 1, s: "x", list: [1, [2]], map: new Map([["k", { v: 1 }]]), set: new Set([1, "a"]), when: new Date(0),
    pattern: /a+/gi, bytes: new Uint8Array([1, 2, 3]), big: 10n, error: new RangeError("bad") };
source.self = source;
source.view = new Uint8Array(source.bytes.buffer, 1, 2);
var copy = structuredClone(source);
r.push(copy !== source, copy.self === copy, copy.list[1][0], copy.map.get("k").v, copy.map.get("k") !== source.map.get("k"),
    copy.set.has("a"), copy.when.getTime(), copy.pattern.source + "/" + copy.pattern.flags, Array.from(copy.bytes).join(" "),
    copy.view.buffer === copy.bytes.buffer, copy.view.byteOffset, copy.big === 10n, copy.error.name + ":" + copy.error.message,
    copy.error instanceof RangeError, copy.map instanceof Map, copy.when instanceof Date);
var buffer = new Uint8Array([9, 8]).buffer;
var bufferCopy = structuredClone(buffer);
r.push(bufferCopy !== buffer, buffer.byteLength, new Uint8Array(bufferCopy)[0]);
var moved = new Uint8Array([5, 6, 7]).buffer;
var movedCopy = structuredClone({ b: moved }, { transfer: [moved] });
r.push(moved.byteLength, movedCopy.b.byteLength, new Uint8Array(movedCopy.b)[2]);
function failure(fn) {
    try { fn(); return "no throw"; } catch (e) { return e.name + ":" + e.code + ":" + (e instanceof DOMException); }
}
r.push(failure(function () { structuredClone(function () {}); }), failure(function () { structuredClone({ f: function () {} }); }),
    failure(function () { structuredClone(Symbol("s")); }), failure(function () { structuredClone(moved, { transfer: [moved] }); }),
    failure(function () { var twice = new ArrayBuffer(1); structuredClone(twice, { transfer: [twice, twice] }); }),
    failure(function () { structuredClone(1, { transfer: [new Uint8Array(1)] }); }),
    failure(function () { structuredClone({ get x() { throw new RangeError("getter"); } }); }));
try { structuredClone(); } catch (e) { r.push(e.name); }
r.push(JSON.stringify(structuredClone({ x: 1, y: 2, z: 3 })), structuredClone(null), structuredClone("s"));
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT([true,true,2,1,true,true,0,"a+/gi","1 2 3",true,1,true,"RangeError:bad",true,true,true,true,2,9,)OUT"
        R"OUT(0,3,7,"DataCloneError:25:true","DataCloneError:25:true","DataCloneError:25:true","DataCloneError)OUT"
        R"OUT(:25:true","DataCloneError:25:true","DataCloneError:25:true","RangeError:undefined:false","TypeEr)OUT"
        R"OUT(ror","{\"x\":1,\"y\":2,\"z\":3}",null,"s"])OUT" }));
}

void WebGlobalsTests::testPerformanceNow() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
var a = performance.now();
var b = performance.now();
function rounded(t) { return Math.abs(t * 10 - Math.round(t * 10)) < 1e-6; }
var origin = performance.timeOrigin;
try { performance.timeOrigin = 0; } catch (e) {}
r.push(typeof a, a >= 0, b >= a, rounded(a) && rounded(b), typeof origin, Math.abs(origin + b - Date.now()) < 1000,
    performance.timeOrigin === origin, JSON.stringify(performance.toJSON()) === JSON.stringify({ timeOrigin: origin }));
setTimeout(function () {
    var c = performance.now();
    r.push(c > b, rounded(c));
    print(JSON.stringify(r));
    Script.stop(true);
}, 20);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["number",true,true,true,"number",true,true,true,true,true])OUT" }));
}

void WebGlobalsTests::testDomException() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
var e = new DOMException("msg", "DataCloneError");
r.push(e.name, e.message, e.code, e instanceof Error, e instanceof DOMException, String(e), typeof e.stack);
var plain = new DOMException();
r.push(plain.name, plain.message, plain.code, new DOMException("x", "NotSupportedError").code, new DOMException("x", "Custom").code);
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["DataCloneError","msg",25,true,true,"DataCloneError: msg","string","Error","",0,9,0])OUT" }));
}

void WebGlobalsTests::testUrlSearchParams() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
var p = new URLSearchParams("?a=1&b=%20x+y&a=2&c&=e&%zz=%E2%82%AC");
r.push(p.size, p.get("a"), p.getAll("a").join("|"), p.get("b"), p.get("c"), p.get(""), p.get("%zz").codePointAt(0), p.has("a", "2"),
    p.has("a", "3"), p.get("missing"));
p.delete("%zz");
p.append("d", "x y&z");
p.delete("a", "1");
r.push(p.toString());
p.set("a", "only");
p.sort();
r.push(p.toString());
p.delete("a");
r.push(p.toString(), JSON.stringify(Array.from(p)), Array.from(p.keys()).join("|"), Array.from(p.values()).join("|"));
var seen = [];
p.forEach(function (value, name) { seen.push(name + "=" + value); });
r.push(seen.join(";"));
r.push(new URLSearchParams({ x: "1", y: "\u00e9" }).toString(), new URLSearchParams([["a", "b"], ["c", "d"]]).toString(),
    new URLSearchParams(new URLSearchParams("q=1")).toString(), new URLSearchParams([["a b", "~!*()'"]]).toString(),
    new URLSearchParams().toString(), new URLSearchParams("a=%zz&b=%41%42").toString());
try { new URLSearchParams([["a"]]); } catch (e) { r.push(e.name); }
var sorted = new URLSearchParams("z=b&a=1&z=a&a=0");
sorted.sort();
r.push(sorted.toString());
var url = new URL("https://example.com/p?x=1#h");
url.searchParams.append("y", "2 3");
r.push(url.href);
url.search = "?z=9";
r.push(url.searchParams.get("z"), url.searchParams.has("x"));
url.searchParams.delete("z");
r.push(url.href, url.search);
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT([6,"1","1|2"," x y","","e",8364,true,false,null,"b=+x+y&a=2&c=&=e&d=x+y%26z","=e&a=only&b=+x+y&c)OUT"
        R"OUT(=&d=x+y%26z","=e&b=+x+y&c=&d=x+y%26z","[[\"\",\"e\"],[\"b\",\" x y\"],[\"c\",\"\"],[\"d\",\"x y&)OUT"
        R"OUT(z\"]]","|b|c|d","e| x y||x y&z","=e;b= x y;c=;d=x y&z","x=1&y=%C3%A9","a=b&c=d","q=1","a+b=%7E%2)OUT"
        R"OUT(1*%28%29%27","","a=%25zz&b=AB","TypeError","a=1&a=0&z=b&z=a","https://example.com/p?x=1&y=2+3#h")OUT"
        R"OUT(,"9",false,"https://example.com/p#h",""])OUT" }));
}

void WebGlobalsTests::testUrlParts() {
    QStringList printed;
    QVERIFY(runScript(URL_PARTS_SCRIPT, printed));
    QCOMPARE(printed, QStringList({ R"OUT(["https://user:pa%40ss@example.com:8443/p/a/t/h?query=string#hash","https:","user","pa%40ss","ex)OUT"
        R"OUT(ample.com:8443","example.com","8443","/p/a/t/h","?query=string","#hash","https://example.com:844)OUT"
        R"OUT(3","http://example.com","null","https://example.com","null","a@b","","https://example.com/a?b#c")OUT"
        R"OUT(,"https://example.com/a?b#c","https://us%20er@example.com/a?b#c","https://us%20er:p%40ss@example)OUT"
        R"OUT(.com/a?b#c","https://us%20er:p%40ss@other.org:8080/a?b#c","third.net:8080","third.net","","8443")OUT"
        R"OUT(,"third.net","/x%20y/z%3Fw","?q=1%202","#top","https://us%20er:p%40ss@third.net/x%20y/z%3Fw","ht)OUT"
        R"OUT(tp://new.example/","TypeError","http://new.example/","{\"u\":\"http://new.example/\"}","http://n)OUT"
        R"OUT(ew.example/",true,false,true,"TypeError","TypeError","TypeError","TypeError","http://new.example)OUT"
        R"OUT(/","http://b.example/dir/x",true,"http://a.example/p?#","",""])OUT" }));
}

// A script replaces the built-ins the globals use; they keep working, or throw a TypeError, as with the originals
void WebGlobalsTests::testReplacedBuiltins() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
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
// Put back before anything else runs: the engine's own script helpers use the built-ins too
restore();
hashed.then(function (hash) {
    r.push(hex(hash));
    return refused;
}).catch(function (e) {
    r.push(e.name);
}).then(function () {
    print(JSON.stringify(r));
    Script.stop(true);
});)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT([2,1,"hi","hi","TypeError","",128512,"aGk=","hi",true,"TypeError","TypeMismatchError",true,"Rang)OUT"
        R"OUT(eError","SyntaxError","1","A","k=v","c22b5f9178342609428d6f51b2c5af4c0bde6a42","TypeError"])OUT" }));
}

void WebGlobalsTests::testStructuredCloneOverteObjects() {
    QStringList printed;
    QVERIFY(runScript(R"JS(
var r = [];
function failure(fn) {
    try { fn(); return "no throw"; } catch (e) { return e.name + ":" + e.code + ":" + (e instanceof DOMException); }
}
r.push(failure(function () { structuredClone(closure); }), failure(function () { structuredClone(Script); }),
    failure(function () { structuredClone({ library: Vec3 }); }), failure(function () { structuredClone([Script.update]); }));
try { structuredClone(Script); } catch (e) { r.push(e.message); }
print(JSON.stringify(r));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ R"OUT(["DataCloneError:25:true","DataCloneError:25:true","DataCloneError:25:true",)OUT"
        R"OUT("DataCloneError:25:true","An Overte native object could not be cloned."])OUT" }));
}

void WebGlobalsTests::testUrlTable() {
    QStringList printed;
    QVERIFY(runScript(QString("var cases = ") + URL_CASES + ";\n" + R"JS(
var divergent = [];
cases.forEach(function (c) {
    var base = c[1] === null ? undefined : c[1];
    var href;
    try { href = new URL(c[0], base).href; } catch (e) { href = e.name === "TypeError" ? null : "threw " + e.name; }
    if (URL.canParse(c[0], base) !== (href !== null)) {
        href = "canParse disagrees";
    }
    if (href !== c[2]) {
        divergent.push([c[0], c[1], href]);
    }
});
print(JSON.stringify(divergent));
Script.stop(true);)JS", printed));
    QCOMPARE(printed, QStringList({ URL_DIVERGENCES }));
}

void WebGlobalsTests::testClosureRealm() {
    // What a Script.require() module sees from its own context: the globals are the main context's, and so is what they
    // make, whether called during the module's own evaluation or from a later callback. (A classic entity script is
    // evaluated in the main context, so this does not apply to it; the live check shows instanceof working there.)
    QStringList printed;
    QVERIFY(runScript("closure.run();", printed, R"JS(
var r = [];
var tag = function (value) { return Object.prototype.toString.call(value); };
var bytes = new TextEncoder().encode("hi");
r.push(bytes instanceof Uint8Array, ArrayBuffer.isView(bytes), tag(bytes));
var copy = structuredClone({ list: [1, 2], when: new Date(0) });
r.push(copy instanceof Object, Array.isArray(copy.list), copy.list instanceof Array, copy.when instanceof Date, tag(copy.when));
r.push(new TextDecoder().decode(new Uint8Array([104, 105])), new TextEncoder().encodeInto("a", new Uint8Array(1)).written);
r.push(new URL("x", "http://e.example/d/") instanceof URL, structuredClone(new Uint8Array([1]).buffer).byteLength);
try { atob("$"); } catch (e) { r.push(e instanceof DOMException, e instanceof Error, e.name); }
crypto.subtle.digest("SHA-1", new Uint8Array(0)).then(function (hash) {
    var later = structuredClone([1]);
    r.push(hash instanceof ArrayBuffer, tag(hash), later instanceof Array, Array.isArray(later));
    print(JSON.stringify(r));
    Script.stop(true);
});)JS"));
    QCOMPARE(printed, QStringList({ R"OUT([false,true,"[object Uint8Array]",false,true,false,false,"[object Date]","hi",1,true,1,)OUT"
        R"OUT(true,false,"InvalidCharacterError",false,"[object ArrayBuffer]",false,true])OUT" }));
}
