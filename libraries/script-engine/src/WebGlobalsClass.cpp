//
//  WebGlobalsClass.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "WebGlobalsClass.h"

#include <chrono>

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>
#include <QUuid>
#include <QVector>

#include "ScriptContext.h"
#include "ScriptEngineLogging.h"
#include "ScriptValue.h"
#include "v8/FastScriptValueUtils.h"
#include "v8/WebGlobalsV8.h"

static ScriptValue randomBytes(ScriptContext* context, ScriptEngine* engine) {
    const int size = context->argument(0).toInt32();
    QVector<quint32> words((size + 3) / 4);
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    return engine->newArrayBuffer(QByteArray(reinterpret_cast<const char*>(words.constData()), size));
}

static ScriptValue randomUUID(ScriptContext* context, ScriptEngine* engine) {
    return engine->newValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

// digest(name, arrayBuffer): the prelude has normalised the name to one of the four below
static ScriptValue digest(ScriptContext* context, ScriptEngine* engine) {
    const QString name = context->argument(0).toString();
    QByteArray bytes;
    qBytearrayFromScriptValue(context->argument(1), bytes);
    const QCryptographicHash::Algorithm algorithm = name == "SHA-1" ? QCryptographicHash::Sha1
        : name == "SHA-256" ? QCryptographicHash::Sha256
        : name == "SHA-384" ? QCryptographicHash::Sha384
        : QCryptographicHash::Sha512;
    ScriptPromise promise = engine->newPromise();
    promise.resolver->resolve(engine->newArrayBuffer(QCryptographicHash::hash(bytes, algorithm)));
    return promise.promise;
}

static ScriptValue monotonicMs(ScriptContext* context, ScriptEngine* engine) {
    using namespace std::chrono;
    return engine->newValue(duration<double, std::milli>(steady_clock::now().time_since_epoch()).count());
}

static bool isSpecialScheme(const QString& scheme) {
    return scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss" || scheme == "ftp" || scheme == "file";
}

static int defaultPort(const QString& scheme) {
    if (scheme == "http" || scheme == "ws") {
        return 80;
    }
    if (scheme == "https" || scheme == "wss") {
        return 443;
    }
    return scheme == "ftp" ? 21 : -1;
}

static bool isAsciiAlpha(QChar c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The lower-cased scheme if the input starts with one, else empty
static QString schemeOf(const QString& input) {
    if (input.isEmpty() || !isAsciiAlpha(input[0])) {
        return QString();
    }
    for (int i = 1; i < input.size(); i++) {
        const QChar c = input[i];
        if (c == ':') {
            return input.left(i).toLower();
        }
        if (!isAsciiAlpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') {
            return QString();
        }
    }
    return QString();
}

struct ParsedUrl {
    QUrl url;
    bool hasAuthority { false };
    bool opaque { false };
};

// QUrl parses RFC 3986; the steps around it bring the result towards the WHATWG URL parser where the two differ in
// ways scripts commonly meet. What remains different is listed in the URL JSDoc.
static bool parseUrl(const QString& rawInput, const ParsedUrl* base, ParsedUrl& out) {
    int start = 0;
    int end = rawInput.size();
    while (start < end && rawInput[start].unicode() <= 0x20) {
        start++;
    }
    while (end > start && rawInput[end - 1].unicode() <= 0x20) {
        end--;
    }
    QString input = rawInput.mid(start, end - start);
    input.remove('\t').remove('\n').remove('\r');

    QString scheme = schemeOf(input);
    const bool special = isSpecialScheme(scheme.isEmpty() && base ? base->url.scheme() : scheme);
    if (special) {
        for (int i = 0; i < input.size() && input[i] != '?' && input[i] != '#'; i++) {
            if (input[i] == '\\') {
                input[i] = '/';
            }
        }
    }
    if (special && !scheme.isEmpty() && !input.midRef(scheme.size() + 1).startsWith("//")) {
        if (base && scheme == base->url.scheme()) {
            // "http:x" against an http: base is relative
            input = input.mid(scheme.size() + 1);
            scheme.clear();
        } else if (scheme != "file") {
            int rest = scheme.size() + 1;
            while (rest < input.size() && input[rest] == '/') {
                rest++;
            }
            input = scheme + "://" + input.mid(rest);
        }
    }

    QUrl url(input, QUrl::TolerantMode);
    if (!input.isEmpty() && !url.isValid()) {
        return false;
    }
    if (scheme.isEmpty()) {
        if (!base || (base->opaque && !input.isEmpty() && !input.startsWith('#'))) {
            return false;
        }
        // An empty QUrl is invalid, where an empty reference is the base without its fragment
        url = input.isEmpty() ? base->url.adjusted(QUrl::RemoveFragment) : base->url.resolved(url);
    } else {
        // resolved() removes dot segments, which the constructor leaves
        url = url.resolved(url);
    }
    if (!url.isValid()) {
        return false;
    }
    scheme = url.scheme();
    out.hasAuthority = url.toString(QUrl::FullyEncoded).midRef(scheme.size() + 1).startsWith("//");
    if (isSpecialScheme(scheme) && scheme != "file" && (!out.hasAuthority || url.host().isEmpty())) {
        return false;
    }
    out.opaque = !isSpecialScheme(scheme) && !out.hasAuthority && !url.path().startsWith('/');
    out.url = url;
    return true;
}

static ScriptValue urlRecord(ScriptEngine* engine, const ParsedUrl& parsed) {
    const QUrl& url = parsed.url;
    const QString scheme = url.scheme();
    ScriptValue record = engine->newObject();
    record.setProperty("scheme", engine->newValue(scheme));
    record.setProperty("username", engine->newValue(url.userName(QUrl::FullyEncoded)));
    record.setProperty("password", engine->newValue(url.password(QUrl::FullyEncoded)));
    ScriptValue host = engine->nullValue();
    if (parsed.hasAuthority) {
        QString name = url.host(QUrl::FullyEncoded);
        if (name.contains(':')) {
            name = "[" + name + "]";
        } else if (scheme == "file" && name == "localhost") {
            name = QString();
        }
        host = engine->newValue(name);
    }
    record.setProperty("host", host);
    const int port = url.port();
    record.setProperty("port", port < 0 || port == defaultPort(scheme) ? engine->nullValue() : engine->newValue(port));
    QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty() && isSpecialScheme(scheme)) {
        path = "/";
    }
    record.setProperty("path", engine->newValue(path));
    // QUrl leaves ' in a query, where the WHATWG special-query set encodes it
    QString query = url.query(QUrl::FullyEncoded);
    if (isSpecialScheme(scheme)) {
        query.replace('\'', "%27");
    }
    record.setProperty("query", url.hasQuery() ? engine->newValue(query) : engine->nullValue());
    record.setProperty("fragment",
                       url.hasFragment() ? engine->newValue(url.fragment(QUrl::FullyEncoded)) : engine->nullValue());
    record.setProperty("opaque", engine->newValue(parsed.opaque));
    return record;
}

// parseUrl(input, base) -> the URL record the prelude keeps, or null if either does not parse
static ScriptValue parseUrlNative(ScriptContext* context, ScriptEngine* engine) {
    ParsedUrl base;
    const bool hasBase = !context->argument(1).isUndefined();
    if (hasBase && !parseUrl(context->argument(1).toString(), nullptr, base)) {
        return engine->nullValue();
    }
    ParsedUrl parsed;
    if (!parseUrl(context->argument(0).toString(), hasBase ? &base : nullptr, parsed)) {
        return engine->nullValue();
    }
    return urlRecord(engine, parsed);
}

void registerWebGlobals(ScriptEngine* engine) {
    ScriptValue natives = engine->newObject();
    natives.setProperty("encodeUtf8", engine->newFunction(webGlobalsEncodeUtf8, 1));
    natives.setProperty("decodeUtf8", engine->newFunction(webGlobalsDecodeUtf8, 3));
    natives.setProperty("base64Encode", engine->newFunction(webGlobalsBase64Encode, 1));
    natives.setProperty("base64Decode", engine->newFunction(webGlobalsBase64Decode, 1));
    natives.setProperty("randomBytes", engine->newFunction(randomBytes, 1));
    natives.setProperty("randomUUID", engine->newFunction(randomUUID, 0));
    natives.setProperty("digest", engine->newFunction(digest, 2));
    natives.setProperty("monotonicMs", engine->newFunction(monotonicMs, 0));
    natives.setProperty("parseUrl", engine->newFunction(parseUrlNative, 2));
    natives.setProperty("clone", engine->newFunction(webGlobalsClone, 2));

    ScriptValue install = engine->evaluate(webGlobalsPrelude(), "(web globals)");
    if (!install.isFunction()) {
        qCCritical(scriptengine) << "web globals prelude did not evaluate to a function, they are unavailable:"
                                 << install.toString();
        return;
    }
    ScriptValue globals = install.call(engine->undefinedValue(), ScriptValueList({ natives }));
    for (const char* name : { "atob", "btoa", "crypto", "DOMException", "performance", "structuredClone", "TextDecoder",
                              "TextEncoder", "URL", "URLSearchParams" }) {
        engine->globalObject().setProperty(name, globals.property(name));
    }
}
