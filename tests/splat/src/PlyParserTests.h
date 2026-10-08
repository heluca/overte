//
//  PlyParserTests.h
//  tests/splat/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_PlyParserTests_h
#define overte_PlyParserTests_h

#include <QtTest/QtTest>

class PlyParserTests : public QObject {
    Q_OBJECT
private slots:
    void testParseThreeSplats();
    void testTrimmedBounds();
    void testSmallScaleSurvivesPacking();
    void testDropsNonFiniteScaleAndOpacity();
    void testRejectsAsciiBody();
    void testRejectsBigEndianBody();
    void testRejectsMissingProperty();
    void testRejectsTruncatedBody();
    void testRejectsNonPly();
};

#endif // overte_PlyParserTests_h
