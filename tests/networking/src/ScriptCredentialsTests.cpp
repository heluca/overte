//
//  ScriptCredentialsTests.cpp
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptCredentialsTests.h"

#include <QJsonArray>
#include <QJsonObject>

#include <ScriptCredentials.h>

QTEST_MAIN(ScriptCredentialsTests)

static const QString SCRIPTS = "https://scripts.example/trusted/";
static const QUrl SCRIPT("https://scripts.example/trusted/piano.js");

static QJsonObject row(const QString& urlPrefix, const QString& token, const QString& scriptPrefix = SCRIPTS) {
    return QJsonObject { { "url_prefix", urlPrefix }, { "token", token }, { "script_prefix", scriptPrefix } };
}

static QByteArray lookup(const ScriptCredentials& credentials, const QString& request, const QUrl& script = SCRIPT) {
    return credentials.authorizationFor(QUrl(request), QList<QUrl> { script });
}

void ScriptCredentialsTests::testUrlPrefix() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray { row("https://api.example/v1/", "t1"), row("https://host.example", "t2") });

    QCOMPARE(lookup(credentials, "https://api.example/v1/status"), QByteArray("Bearer t1"));
    QCOMPARE(lookup(credentials, "https://api.example/v1/status?x=1#f"), QByteArray("Bearer t1"));
    QCOMPARE(lookup(credentials, "https://API.Example/v1/status"), QByteArray("Bearer t1"));
    QCOMPARE(lookup(credentials, "https://api.example:443/v1/"), QByteArray("Bearer t1"));
    // The path is case-sensitive, and the prefix's trailing slash counts
    QCOMPARE(lookup(credentials, "https://api.example/V1/status"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v10/status"), QByteArray());
    // Another scheme, port or host
    QCOMPARE(lookup(credentials, "http://api.example/v1/status"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example:8443/v1/status"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example.invalid/v1/status"), QByteArray());
    // A host-only prefix covers the host's every path, but not a longer host name
    QCOMPARE(lookup(credentials, "https://host.example"), QByteArray("Bearer t2"));
    QCOMPARE(lookup(credentials, "https://host.example/any/path"), QByteArray("Bearer t2"));
    QCOMPARE(lookup(credentials, "https://host.example.invalid/"), QByteArray());
    QCOMPARE(lookup(credentials, "https://host.examplex/"), QByteArray());
    // User info and dot segments are under no prefix
    QCOMPARE(lookup(credentials, "https://user@api.example/v1/status"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/../admin"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/./status"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/%2e%2e/admin"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/..%2fadmin"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/..."), QByteArray("Bearer t1"));
    // A backslash, literal or encoded, separates segments for some servers
    QCOMPARE(lookup(credentials, "https://api.example/v1/..%5Cadmin"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1/..\\admin"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/v1%5C..%5Cadmin"), QByteArray());
}

void ScriptCredentialsTests::testSegmentBoundary() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray {
        row("https://api.example/api", "no-slash"),
        row("https://other.example/api/", "slash"),
        row("https://lib.example/", "scripts", "https://scripts.example/heluca"),
    });

    QCOMPARE(lookup(credentials, "https://api.example/api"), QByteArray("Bearer no-slash"));
    QCOMPARE(lookup(credentials, "https://api.example/api/status"), QByteArray("Bearer no-slash"));
    QCOMPARE(lookup(credentials, "https://api.example/apix"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/api.json"), QByteArray());
    QCOMPARE(lookup(credentials, "https://api.example/api%5Cstatus"), QByteArray());
    QCOMPARE(lookup(credentials, "https://other.example/api/status"), QByteArray("Bearer slash"));
    QCOMPARE(lookup(credentials, "https://other.example/api"), QByteArray());

    // A script prefix without a trailing slash on a shared host stops at the segment too
    QCOMPARE(lookup(credentials, "https://lib.example/x", QUrl("https://scripts.example/heluca/piano.js")),
             QByteArray("Bearer scripts"));
    QCOMPARE(lookup(credentials, "https://lib.example/x", QUrl("https://scripts.example/helucaevil/piano.js")),
             QByteArray());
    QCOMPARE(lookup(credentials, "https://lib.example/x", QUrl("https://scripts.example/heluca%5Cpiano.js")),
             QByteArray());
}

void ScriptCredentialsTests::testLoopbackHttpOnly() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray {
        row("http://127.0.0.1:8000/", "v4"),
        row("http://localhost:8001/", "name"),
        row("http://[::1]:8002/", "v6"),
        row("http://api.example/", "remote"),
        row("http://10.0.0.1/", "private"),
    });

    QCOMPARE(lookup(credentials, "http://127.0.0.1:8000/status"), QByteArray("Bearer v4"));
    QCOMPARE(lookup(credentials, "http://LOCALHOST:8001/status"), QByteArray("Bearer name"));
    QCOMPARE(lookup(credentials, "http://[::1]:8002/status"), QByteArray("Bearer v6"));
    QCOMPARE(lookup(credentials, "http://127.0.0.1:8001/status"), QByteArray());
    QCOMPARE(lookup(credentials, "http://api.example/status"), QByteArray());
    QCOMPARE(lookup(credentials, "http://10.0.0.1/status"), QByteArray());
}

void ScriptCredentialsTests::testScriptScope() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray {
        row("https://api.example/", "web"),
        row("https://assets.example/", "atp", "atp:/trusted/"),
        row("https://local.example/", "local", "http://127.0.0.1:9000/scripts/"),
    });
    const QString request = "https://api.example/status";

    QCOMPARE(lookup(credentials, request, QUrl("https://scripts.example/trusted/a/b.js")), QByteArray("Bearer web"));
    QCOMPARE(lookup(credentials, request, QUrl("https://SCRIPTS.example/trusted/b.js")), QByteArray("Bearer web"));
    QCOMPARE(lookup(credentials, request, QUrl("https://scripts.example/other/b.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("https://scripts.example/Trusted/b.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("http://scripts.example/trusted/b.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("https://scripts.example.invalid/trusted/b.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("https://scripts.example/trusted/../other/b.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("about:Entities 1")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl("piano.js")), QByteArray());
    QCOMPARE(lookup(credentials, request, QUrl()), QByteArray());

    QCOMPARE(lookup(credentials, "https://assets.example/x", QUrl("atp:/trusted/piano.js")), QByteArray("Bearer atp"));
    QCOMPARE(lookup(credentials, "https://assets.example/x", QUrl("atp:/other/piano.js")), QByteArray());
    QCOMPARE(lookup(credentials, "https://local.example/x", QUrl("http://127.0.0.1:9000/scripts/a.js")),
             QByteArray("Bearer local"));

    // Every URL of the code must be under the script prefix: a module imported from elsewhere takes the credential away
    const QUrl trusted("https://scripts.example/trusted/main.mjs");
    const QUrl library("https://scripts.example/trusted/lib.mjs");
    const QUrl foreign("https://cdn.example/lib.mjs");
    QCOMPARE(credentials.authorizationFor(QUrl(request), QList<QUrl> { trusted, library }), QByteArray("Bearer web"));
    QCOMPARE(credentials.authorizationFor(QUrl(request), QList<QUrl> { trusted, foreign }), QByteArray());
    QCOMPARE(credentials.authorizationFor(QUrl(request), QList<QUrl>()), QByteArray());
}

void ScriptCredentialsTests::testLongestMatch() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray {
        row("https://api.example/", "short"),
        row("https://api.example/v1/admin/", "longest"),
        row("https://api.example/v1/", "middle"),
        row("https://api.example/v2/", "any-trusted"),
        row("https://api.example/v2/", "piano-only", "https://scripts.example/trusted/piano/"),
        row("https://api.example/v3/private/", "other-scripts", "https://scripts.example/other/"),
    });

    QCOMPARE(lookup(credentials, "https://api.example/status"), QByteArray("Bearer short"));
    QCOMPARE(lookup(credentials, "https://api.example/v1/status"), QByteArray("Bearer middle"));
    QCOMPARE(lookup(credentials, "https://api.example/v1/admin/users"), QByteArray("Bearer longest"));
    // Equal URL prefixes: the longer script prefix wins for the scripts under it
    QCOMPARE(lookup(credentials, "https://api.example/v2/x"), QByteArray("Bearer any-trusted"));
    QCOMPARE(lookup(credentials, "https://api.example/v2/x", QUrl("https://scripts.example/trusted/piano/p.js")),
             QByteArray("Bearer piano-only"));
    // A longer URL prefix for other scripts does not hide the shorter one this script may use
    QCOMPARE(lookup(credentials, "https://api.example/v3/private/x"), QByteArray("Bearer short"));
}

void ScriptCredentialsTests::testIgnoredRows() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray {
        row("https://empty.example/", ""),
        row("https://space.example/", "two words"),
        row("https://newline.example/", "line\r\nX-Injected: 1"),
        row("https://unicode.example/", QString::fromUtf8("t\xc3\xa9")),
        row("https://query.example/?key=1", "t"),
        row("https://user@userinfo.example/", "t"),
        row("ftp://ftp.example/", "t"),
        row("atp:/assets/", "t"),
        row("not a url", "t"),
        row("https://dots.example/a/../", "t"),
        row("https://noscript.example/", "t", ""),
        row("https://httpscript.example/", "t", "http://scripts.example/trusted/"),
        row("https://good.example/", "  padded  "),
    });

    for (const char* host : { "empty", "space", "newline", "unicode", "query", "userinfo", "ftp", "dots", "noscript",
                              "httpscript" }) {
        QCOMPARE(lookup(credentials, QString("https://%1.example/x").arg(host)), QByteArray());
    }
    QCOMPARE(lookup(credentials, "atp:/assets/x"), QByteArray());
    QCOMPARE(lookup(credentials, "https://good.example/x"), QByteArray("Bearer padded"));
}

void ScriptCredentialsTests::testReload() {
    ScriptCredentials credentials;
    credentials.setFromSettings(QJsonArray { row("https://api.example/", "old") });
    QCOMPARE(lookup(credentials, "https://api.example/x"), QByteArray("Bearer old"));

    credentials.setFromSettings(QJsonArray { row("https://api.example/", "new") });
    QCOMPARE(lookup(credentials, "https://api.example/x"), QByteArray("Bearer new"));

    credentials.setFromSettings(QJsonArray());
    QCOMPARE(lookup(credentials, "https://api.example/x"), QByteArray());
}
