//
//  ScriptModuleResolver.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptModuleResolver.h"

#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>

#include <PathUtils.h>

#include "ScriptEngines.h"

static const int MAX_MODULE_ID_LENGTH { 4096 };

QString checkModuleSpecifierLength(const QString& specifier) {
    auto idLength = specifier.length();
    if (idLength < 1 || idLength > MAX_MODULE_ID_LENGTH) {
        return QString("rejecting invalid module id size (%1 chars [1,%2])").arg(idLength).arg(MAX_MODULE_ID_LENGTH);
    }
    return QString();
}

QString checkLocalModuleFile(const QUrl& url, const QUrl& sandboxURL) {
    QFileInfo file(url.toLocalFile());
    QUrl canonical = url;
    if (file.exists()) {
        canonical.setPath(file.canonicalFilePath());
    }

    bool disallowOutsideFiles = !PathUtils::defaultScriptsLocation().isParentOf(canonical) && !sandboxURL.isLocalFile();
    if (disallowOutsideFiles && !PathUtils::isDescendantOf(canonical, sandboxURL)) {
        return QString("path '%1' outside of origin script '%2' '%3'")
            .arg(PathUtils::stripFilename(url))
            .arg(PathUtils::stripFilename(sandboxURL))
            .arg(canonical.toString());
    }
    if (!file.exists()) {
        return "path does not exist: " + url.toLocalFile();
    }
    if (!file.isFile()) {
        return "path is not a file: " + url.toLocalFile();
    }
    return QString();
}

bool isAnchoredModuleSpecifier(const QString& specifier) {
    static const QRegularExpression ANCHORED("^\\w+:|^/|^[.]{1,2}(/|$)");
    return ANCHORED.match(specifier).hasMatch();
}

bool isModuleURL(const QUrl& url) {
    return url.path().endsWith(".mjs", Qt::CaseInsensitive);
}

QUrl resolveModuleSpecifier(const QString& specifier, const QUrl& referrer, const QUrl& sandboxURL, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return QUrl();
    };

    QString lengthError = checkModuleSpecifierLength(specifier);
    if (!lengthError.isEmpty()) {
        return fail(lengthError);
    }

    // Script.require.resolve maps a bare id to scripts/modules/<id>.js; imports have no such fallback (yet)
    if (!isAnchoredModuleSpecifier(specifier)) {
        return fail(QString("bare specifier '%1' is not supported; use './%1' for a file next to the importer, or a full URL")
                        .arg(specifier));
    }

    QUrl url(specifier);
    if (specifier.startsWith("/~/") || url.scheme().length() == 1) {
        url = QUrl::fromLocalFile(specifier);
    } else if (url.isRelative()) {
        // atp:<hash> has no path to be relative to
        if (referrer.scheme() == "atp" && !referrer.path().startsWith("/")) {
            return fail("a module loaded by asset hash can only import absolute URLs");
        }
        url = referrer.resolved(url);
        // QUrl takes "/x" against file:///C:/a/m.mjs to file:///x
        static const QRegularExpression DRIVE("^/[A-Za-z]:/");
        auto drive = DRIVE.match(referrer.path());
        if (referrer.isLocalFile() && specifier.startsWith("/") && !specifier.startsWith("//") && drive.hasMatch()) {
            url.setPath(drive.captured(0).chopped(1) + url.path());
        }
    }

    url = expandScriptUrl(url);
    if (url.isEmpty() || url.isRelative()) {
        return fail("could not resolve module specifier; supported are file:, http:, https: and atp: URLs");
    }

    if (url.isLocalFile()) {
        QString fileError = checkLocalModuleFile(url, sandboxURL);
        if (!fileError.isEmpty()) {
            return fail(fileError);
        }
    }
    return url;
}
