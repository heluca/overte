//
//  OctreePersistThreadTests.h
//  tests/octree/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_OctreePersistThreadTests_h
#define overte_OctreePersistThreadTests_h

#include <QtTest/QtTest>

class OctreePersistThreadTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testDataFileReplyFromDomainServer();
    void testDataFileReplyFromOtherSocket();
};

#endif // overte_OctreePersistThreadTests_h
