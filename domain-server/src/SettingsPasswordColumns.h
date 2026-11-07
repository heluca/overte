//
//  SettingsPasswordColumns.h
//  domain-server/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_SettingsPasswordColumns_h
#define overte_SettingsPasswordColumns_h

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QVariantList>

// The columns of a settings table whose description gives them "type": "password". The settings page shows them masked
// and posts the mask back for the rows it did not add.

/// The table with its password columns masked, for the settings page
QJsonValue maskPasswordColumns(const QJsonValue& table, const QJsonObject& settingDescription);

/// The posted table with each masked password taken from the saved row whose other columns are all the same. A row whose
/// other columns changed loses it, so a secret is never moved to where a row now points without being entered again.
QJsonArray restorePasswordColumns(const QJsonArray& posted, const QVariantList& saved,
                                  const QJsonObject& settingDescription);

#endif  // overte_SettingsPasswordColumns_h
