//
//  AudioCodecNegotiationTests.h
//  tests/audio/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AudioCodecNegotiationTests_h
#define overte_AudioCodecNegotiationTests_h

#include <QtTest/QtTest>

class AudioCodecNegotiationTests : public QObject {
    Q_OBJECT
private slots:
    void testParseDefault();
    void testParseTrimsSpaces();
    void testParseSkipsEmpty();
    void testNegotiateSpacedSettingForNonOpusClient();
    void testNegotiatePrefersSettingOrder();
    void testNegotiateNoMatchLeavesMapUnchanged();
};

#endif // overte_AudioCodecNegotiationTests_h
