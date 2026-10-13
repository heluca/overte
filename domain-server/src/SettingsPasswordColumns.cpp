//
//  SettingsPasswordColumns.cpp
//  domain-server/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "SettingsPasswordColumns.h"

#include <algorithm>

#include <QtCore/QStringList>

static const QString PASSWORD_COLUMN_MASK = "******";

static QStringList passwordColumns(const QJsonObject& settingDescription) {
    QStringList columns;
    for (const QJsonValue& column : settingDescription["columns"].toArray()) {
        if (column.toObject()["type"].toString() == "password") {
            columns.append(column.toObject()["name"].toString());
        }
    }
    return columns;
}

QJsonValue maskPasswordColumns(const QJsonValue& table, const QJsonObject& settingDescription) {
    const QStringList columns = passwordColumns(settingDescription);
    if (columns.isEmpty() || !table.isArray()) {
        return table;
    }
    QJsonArray masked;
    for (const QJsonValue& row : table.toArray()) {
        QJsonObject rowObject = row.toObject();
        for (const QString& column : columns) {
            if (!rowObject[column].toString().isEmpty()) {
                rowObject[column] = PASSWORD_COLUMN_MASK;
            }
        }
        masked.append(rowObject);
    }
    return masked;
}

QJsonArray restorePasswordColumns(const QJsonArray& posted, const QVariantList& saved,
                                  const QJsonObject& settingDescription) {
    const QStringList columns = passwordColumns(settingDescription);
    if (columns.isEmpty()) {
        return posted;
    }
    QVariantList unclaimed = saved;
    QJsonArray restored;
    for (const QJsonValue& row : posted) {
        QJsonObject rowObject = row.toObject();
        bool isMasked = std::any_of(columns.begin(), columns.end(), [&rowObject](const QString& column) {
            return rowObject[column].toString() == PASSWORD_COLUMN_MASK;
        });
        if (isMasked) {
            auto match = std::find_if(unclaimed.begin(), unclaimed.end(), [&](const QVariant& savedRow) {
                QJsonObject savedObject = QJsonObject::fromVariantMap(savedRow.toMap());
                for (const QString& name : rowObject.keys()) {
                    if (!columns.contains(name) && savedObject[name] != rowObject[name]) {
                        return false;
                    }
                }
                return true;
            });
            QJsonObject savedObject = match != unclaimed.end() ? QJsonObject::fromVariantMap(match->toMap()) : QJsonObject();
            for (const QString& column : columns) {
                if (rowObject[column].toString() == PASSWORD_COLUMN_MASK) {
                    rowObject[column] = savedObject[column].toString();
                }
            }
            if (match != unclaimed.end()) {
                unclaimed.erase(match);
            }
        }
        restored.append(rowObject);
    }
    return restored;
}
