//
//  SplatSorterTests.h
//  tests/splat/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_SplatSorterTests_h
#define overte_SplatSorterTests_h

#include <QtTest/QtTest>

class SplatSorterTests : public QObject {
    Q_OBJECT
private slots:
    void testBackToFrontAndCulling();
    void testMatchesReferenceSort();
};

#endif // overte_SplatSorterTests_h
