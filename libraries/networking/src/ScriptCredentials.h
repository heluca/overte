//
//  ScriptCredentials.h
//  libraries/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptCredentials_h
#define overte_ScriptCredentials_h

#include <mutex>
#include <vector>

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <DependencyManager.h>

/// Bearer tokens the domain holds for server-side scripts: the rows of the <code>script_credentials</code> domain
/// settings, which only the entity script server and Agent assignment clients receive. The script engine's HTTP requests
/// ask this store for an Authorization header; nothing here is reachable from script code. Thread-safe.
///
/// A row applies to a request when the request URL is under its url_prefix and every script URL is under its
/// script_prefix. Under a prefix means the same scheme, host (in any case) and port, and a path that starts with the
/// prefix's path, case-sensitively. A URL with user info or with "." or ".." path segments is under no prefix. The row
/// with the longest url_prefix path wins, then the one with the longest script_prefix path.
class ScriptCredentials : public Dependency {
    SINGLETON_DEPENDENCY

public:
    static const QString SETTINGS_GROUP;
    static const QString SETTINGS_TABLE;

    /// Replaces the table with the rows of the settings table. A row is ignored, and logged without its token, unless its
    /// url_prefix is https, or http on a loopback host; its script_prefix is https, http on a loopback host, or atp; and its
    /// token is printable ASCII.
    void setFromSettings(const QJsonArray& rows);

    /// <code>"Bearer &lt;token&gt;"</code> for a request made by code loaded from all of the script URLs, or empty
    QByteArray authorizationFor(const QUrl& request, const QList<QUrl>& scripts) const;
    QByteArray authorizationFor(const QUrl& request, const QUrl& script) const {
        return authorizationFor(request, QList<QUrl>{ script });
    }

    /// authorizationFor() of the process's store, or empty in a process that has none, such as Interface
    static QByteArray find(const QUrl& request, const QList<QUrl>& scripts);
    /// setFromSettings() of the process's store with the table in the domain settings, if the store is still there
    static void loadDomainSettings(const QJsonObject& domainSettings);

private:
    struct Prefix {
        QString scheme;
        QString host;
        int port { -1 };
        QString path;
    };
    struct Row {
        Prefix url;
        Prefix script;
        QByteArray authorization;
    };

    static bool parsePrefix(const QString& text, bool forScripts, Prefix& prefix, QString& problem);
    static bool isUnder(const QUrl& url, const Prefix& prefix);

    mutable std::mutex _mutex;
    std::vector<Row> _rows;
};

#endif  // overte_ScriptCredentials_h
