//
//  ScriptStringTests.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptStringTests_h
#define overte_ScriptStringTests_h

#include <QtTest/QtTest>

#include "ScriptValue.h"

// Exposed to test scripts as strings: notes the length of each string it receives from script
class StringProbe : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QString echo(const QString& text) {
        lengths.append(text.length());
        return text;
    }
    Q_INVOKABLE int variantLength(const QVariant& value) { return value.toString().length(); }
    Q_INVOKABLE int valueLength(const ScriptValue& value) { return value.toString().length(); }
    Q_INVOKABLE QString withNul() const { return nulText; }
    Q_INVOKABLE QByteArray bytesWithNul() const { return QByteArray("x\0y", 3); }
    Q_INVOKABLE QByteArray echoBytes(const QByteArray& bytes) {
        lengths.append(bytes.size());
        return bytes;
    }

    QString nulText;
    QList<int> lengths;
};

class ScriptStringTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testScriptStringWithNulRoundTrip();
    void testCppStringWithNulReachesScript();
    void testCppByteArrayWithNulReachesScript();
    void testArrayBufferRoundTrip();
};

#endif // overte_ScriptStringTests_h
