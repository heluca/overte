//
//  WebGlobalsUrlCases.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_WebGlobalsUrlCases_h
#define overte_WebGlobalsUrlCases_h

// [input, base, WHATWG href or null for a parse failure]: a subset of the cases in the web-platform-tests
// url/resources/urltestdata.json, plus the schemes Overte uses, with the href a WHATWG parser gives.
static const char* const URL_CASES = R"JS([
    ["http://example.com/",null,"http://example.com/"],
    ["HTTP://EXAMPLE.COM/A",null,"http://example.com/A"],
    ["http://example.com:80/",null,"http://example.com/"],
    ["https://example.com:443/x",null,"https://example.com/x"],
    ["http://example.com:8080/x",null,"http://example.com:8080/x"],
    ["ws://example.com:80/",null,"ws://example.com/"],
    ["ftp://example.com:21/",null,"ftp://example.com/"],
    ["http://example.com",null,"http://example.com/"],
    ["http://example.com/a/./b/../c",null,"http://example.com/a/c"],
    ["http://example.com/a/b/../../..",null,"http://example.com/"],
    ["http://example.com/%2e/x",null,"http://example.com/x"],
    ["http://user:pass@example.com:8000/p?q#f",null,"http://user:pass@example.com:8000/p?q#f"],
    ["http://example.com/?",null,"http://example.com/?"],
    ["http://example.com/#",null,"http://example.com/#"],
    ["http://example.com/a b",null,"http://example.com/a%20b"],
    ["http://example.com/\u00fc",null,"http://example.com/%C3%BC"],
    ["http://example.com/?a b",null,"http://example.com/?a%20b"],
    ["http://example.com/#a b",null,"http://example.com/#a%20b"],
    ["http://[::1]:8080/",null,"http://[::1]:8080/"],
    ["http://[0:0:0:0:0:0:0:1]/",null,"http://[::1]/"],
    ["http://192.168.0.1/",null,"http://192.168.0.1/"],
    ["http://0x7f.1/",null,"http://127.0.0.1/"],
    ["http://127.1/",null,"http://127.0.0.1/"],
    ["http://ExAmPlE.CoM",null,"http://example.com/"],
    ["http://\u00e9xample.com/",null,"http://xn--xample-9ua.com/"],
    ["  http://example.com/  ",null,"http://example.com/"],
    ["http://exa\tmple.com/\n",null,"http://example.com/"],
    ["http:\\\\example.com\\a\\b",null,"http://example.com/a/b"],
    ["http:/example.com/",null,"http://example.com/"],
    ["http:example.com/",null,"http://example.com/"],
    ["mailto:someone@example.com",null,"mailto:someone@example.com"],
    ["data:text/plain,hello world",null,"data:text/plain,hello world"],
    ["foo://Host/x",null,"foo://Host/x"],
    ["file:///C:/dir/file.txt",null,"file:///C:/dir/file.txt"],
    ["file://localhost/etc/hosts",null,"file:///etc/hosts"],
    ["http://example.com:65536/",null,null],
    ["http://exa mple.com/",null,null],
    ["http://",null,null],
    ["relative/path",null,null],
    ["//example.org/x","http://example.com/a","http://example.org/x"],
    ["../c?q","http://example.com/a/b/","http://example.com/a/c?q"],
    ["?q=1","http://example.com/a/b?x#y","http://example.com/a/b?q=1"],
    ["#frag","http://example.com/a?x","http://example.com/a?x#frag"],
    ["/abs","https://example.com/a/b","https://example.com/abs"],
    ["","http://example.com/a?b#c","http://example.com/a?b"],
    ["http:x","http://example.com/a/b","http://example.com/a/x"],
    ["x","mailto:a@b",null],
    ["#f","mailto:a@b","mailto:a@b#f"],
    ["atp:/some/file.fbx",null,"atp:/some/file.fbx"],
    ["qrc:///qml/x.qml",null,"qrc:///qml/x.qml"],
    ["http://example.com/a%zzb",null,"http://example.com/a%zzb"],
    ["https://example.com/a|b^c",null,"https://example.com/a|b%5Ec"],
    ["\\\\server\\file","http://example.com/","http://server/file"],
    ["http://example.com/a/b/c/../../",null,"http://example.com/a/"],
    ["HTTP://example.com:/x",null,"http://example.com/x"],
    ["http://example.com/a?b=c%20d&e",null,"http://example.com/a?b=c%20d&e"],
    ["https://example.com/a'b\"c",null,"https://example.com/a'b%22c"],
    ["http://example.com/?q='x'",null,"http://example.com/?q=%27x%27"],
    ["https://xn--xample-9ua.com/",null,"https://xn--xample-9ua.com/"],
    ["http://a.b.c./",null,"http://a.b.c./"],
    ["http://example.com/a\u0000b",null,"http://example.com/a%00b"],
    ["\u0000http://example.com/\u0000",null,"http://example.com/"]
])JS";

// The cases where URL, built on QUrl, gives another href: [input, base, href or null]. Listed in the URL JSDoc.
static const char* const URL_DIVERGENCES = R"JS([)JS"
    R"JS(["data:text/plain,hello world",null,"data:text/plain,hello%20world"],)JS"
    R"JS(["foo://Host/x",null,"foo://host/x"],)JS"
    R"JS(["http://example.com/a%zzb",null,"http://example.com/a%25zzb"],)JS"
    R"JS(["https://example.com/a|b^c",null,"https://example.com/a%7Cb%5Ec"])JS"
    R"JS(])JS";

// The parts of a URL, its origin and its setters
static const char* const URL_PARTS_SCRIPT = R"JS(
var r = [];
var u = new URL("https://user:pa%40ss@EXAMPLE.com:8443/p/a/t/h?query=string#hash");
r.push(u.href, u.protocol, u.username, u.password, u.host, u.hostname, u.port, u.pathname, u.search, u.hash, u.origin);
r.push(new URL("http://example.com:80/").origin, new URL("file:///x").origin, new URL("blob:https://example.com/id").origin,
    new URL("foo://x/").origin, new URL("mailto:a@b").pathname, new URL("mailto:a@b").host);
u = new URL("http://example.com/a?b#c");
u.protocol = "https"; r.push(u.href);
u.protocol = "foo"; r.push(u.href);
u.username = "us er"; r.push(u.href);
u.password = "p@ss"; r.push(u.href);
u.host = "other.org:8080"; r.push(u.href);
u.hostname = "third.net"; r.push(u.host);
u.port = "443"; r.push(u.host);
u.port = "abc"; u.port = "99999"; r.push(u.port);
u.port = "8443x"; r.push(u.port);
u.port = ""; r.push(u.host);
u.pathname = "x y/z?w"; r.push(u.pathname);
u.search = "q=1 2"; r.push(u.search);
u.hash = "top"; r.push(u.hash);
u.hash = ""; u.search = ""; r.push(u.href);
u.href = "http://new.example/"; r.push(u.href);
try { u.href = "not a url"; } catch (e) { r.push(e.name, u.href); }
r.push(JSON.stringify({ u: u }), String(u), URL.canParse("http://x/"), URL.canParse("nope"), URL.canParse("/a", "http://x/"));
["nope", "/a"].forEach(function (input) { try { new URL(input); } catch (e) { r.push(e.name); } });
try { new URL("/a", "nope"); } catch (e) { r.push(e.name); }
try { new URL(); } catch (e) { r.push(e.name); }
r.push(new URL(u).href, new URL("x", new URL("http://b.example/dir/")).href, new URL(u) !== u);
var empty = new URL("http://a.example/p?x#y");
empty.search = "?";
empty.hash = "#";
r.push(empty.href, empty.search, empty.hash);
print(JSON.stringify(r));
Script.stop(true);)JS";

#endif // overte_WebGlobalsUrlCases_h
