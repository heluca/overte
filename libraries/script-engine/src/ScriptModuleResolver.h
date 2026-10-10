//
//  ScriptModuleResolver.h
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_ScriptModuleResolver_h
#define overte_ScriptModuleResolver_h

#include <QtCore/QString>
#include <QtCore/QUrl>

// Module specifier resolution shared by Script.require.resolve and ES module imports. None of these read the JS stack
// or throw, so they can run from fetch callbacks.

/// Empty if the specifier length is acceptable, else why not.
QString checkModuleSpecifierLength(const QString& specifier);

/// Empty if a local module file may be loaded by a script whose top-level URL is sandboxURL and is a regular file,
/// else why not. An empty or local sandboxURL allows any local file.
QString checkLocalModuleFile(const QUrl& url, const QUrl& sandboxURL);

/// Absolute, dotted or path-like: the kind of specifier ScriptManager::resolvePath handles. Anything else is bare.
bool isAnchoredModuleSpecifier(const QString& specifier);

/// Whether the URL names an ES module, by its .mjs extension. A query string or fragment does not count.
bool isModuleURL(const QUrl& url);

/// Resolves an import specifier against the importing module's URL. Accepted: absolute URLs, Windows paths with a
/// drive letter, "/~/" paths into the default scripts, and "/", "./" or "../" paths resolved against the referrer as
/// URLs ("/" is the root of the referrer's origin, or of its drive for a local Windows referrer). Bare specifiers
/// ("lodash") are refused. Returns an empty URL and sets error on failure.
QUrl resolveModuleSpecifier(const QString& specifier, const QUrl& referrer, const QUrl& sandboxURL, QString* error);

#endif  // overte_ScriptModuleResolver_h

/// @}
