//
//  ScriptCredentials.cpp
//  libraries/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptCredentials.h"

#include <algorithm>

#include <QtNetwork/QHostAddress>

#include "NetworkLogging.h"

const QString ScriptCredentials::SETTINGS_GROUP = "script_credentials";
const QString ScriptCredentials::SETTINGS_TABLE = "credentials";

static int defaultPort(const QString& scheme) {
    if (scheme == "https") {
        return 443;
    }
    if (scheme == "http") {
        return 80;
    }
    return -1;
}

static bool isLoopbackHost(const QString& host) {
    if (host.compare("localhost", Qt::CaseInsensitive) == 0) {
        return true;
    }
    QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

// A server may decode and resolve dot segments before routing, so a path that only reads as under a prefix is refused
static bool hasNormalPath(const QUrl& url) {
    const QStringList segments = url.path(QUrl::FullyDecoded).split('/');
    return !segments.contains(".") && !segments.contains("..");
}

static QString pathOf(const QUrl& url) {
    QString path = url.path(QUrl::FullyEncoded);
    return path.isEmpty() ? QString("/") : path;
}

bool ScriptCredentials::parsePrefix(const QString& text, bool forScripts, Prefix& prefix, QString& problem) {
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.isRelative()) {
        problem = "is not an absolute URL";
        return false;
    }
    if (!url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || !hasNormalPath(url)) {
        problem = "has user info, a query, a fragment or a non-normal path";
        return false;
    }
    const QString scheme = url.scheme().toLower();
    const bool isHttps = scheme == "https" && !url.host().isEmpty();
    const bool isLoopbackHttp = scheme == "http" && isLoopbackHost(url.host());
    const bool isAssetServer = forScripts && scheme == "atp";
    if (!isHttps && !isLoopbackHttp && !isAssetServer) {
        problem = forScripts ? "must be https, atp, or http on a loopback host" : "must be https, or http on a loopback host";
        return false;
    }
    prefix.scheme = scheme;
    prefix.host = url.host();
    prefix.port = url.port(defaultPort(scheme));
    prefix.path = pathOf(url);
    return true;
}

bool ScriptCredentials::isUnder(const QUrl& url, const Prefix& prefix) {
    if (!url.isValid() || !url.userInfo().isEmpty() || !hasNormalPath(url)) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == prefix.scheme
        && url.host().compare(prefix.host, Qt::CaseInsensitive) == 0
        && url.port(defaultPort(scheme)) == prefix.port
        && pathOf(url).startsWith(prefix.path);
}

void ScriptCredentials::setFromSettings(const QJsonArray& rows) {
    std::vector<Row> parsed;
    for (int i = 0; i < rows.size(); i++) {
        const QJsonObject row = rows[i].toObject();
        const QString urlPrefix = row["url_prefix"].toString();
        const QByteArray token = row["token"].toString().trimmed().toUtf8();
        Row credential;
        QString problem;
        if (!parsePrefix(urlPrefix, false, credential.url, problem)) {
            problem = "url_prefix " + problem;
        } else if (!parsePrefix(row["script_prefix"].toString(), true, credential.script, problem)) {
            problem = "script_prefix " + problem;
        } else if (token.isEmpty() || std::any_of(token.begin(), token.end(), [](char c) { return c < 0x21 || c > 0x7e; })) {
            problem = "token is empty or has characters other than printable ASCII";
        }
        if (!problem.isEmpty()) {
            qCWarning(networking) << "Ignoring script credential" << (i + 1) << "for"
                                  << QUrl(urlPrefix).toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery) << "-" << problem;
            continue;
        }
        credential.authorization = "Bearer " + token;
        parsed.push_back(credential);
    }
    qCInfo(networking) << "Loaded" << parsed.size() << "script credentials";

    std::lock_guard<std::mutex> lock(_mutex);
    _rows = std::move(parsed);
}

QByteArray ScriptCredentials::authorizationFor(const QUrl& request, const QList<QUrl>& scripts) const {
    if (scripts.isEmpty()) {
        return QByteArray();
    }
    std::lock_guard<std::mutex> lock(_mutex);
    const Row* best = nullptr;
    for (const Row& row : _rows) {
        if (!isUnder(request, row.url)) {
            continue;
        }
        bool allScriptsUnder = true;
        for (const QUrl& script : scripts) {
            allScriptsUnder = allScriptsUnder && isUnder(script, row.script);
        }
        if (!allScriptsUnder) {
            continue;
        }
        if (!best || row.url.path.size() > best->url.path.size() ||
            (row.url.path.size() == best->url.path.size() && row.script.path.size() > best->script.path.size())) {
            best = &row;
        }
    }
    return best ? best->authorization : QByteArray();
}

QByteArray ScriptCredentials::find(const QUrl& request, const QList<QUrl>& scripts) {
    if (!DependencyManager::isSet<ScriptCredentials>()) {
        return QByteArray();
    }
    // The assignment can tear the store down between the two calls
    auto credentials = DependencyManager::get<ScriptCredentials>();
    return credentials ? credentials->authorizationFor(request, scripts) : QByteArray();
}

void ScriptCredentials::loadDomainSettings(const QJsonObject& domainSettings) {
    if (!DependencyManager::isSet<ScriptCredentials>()) {
        return;
    }
    // A settings packet can still arrive after the assignment tore the store down
    if (auto credentials = DependencyManager::get<ScriptCredentials>()) {
        credentials->setFromSettings(domainSettings[SETTINGS_GROUP].toObject()[SETTINGS_TABLE].toArray());
    }
}
