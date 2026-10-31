//
//  AvatarIdentityTests.h
//  tests/avatars/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_AvatarIdentityTests_h
#define overte_AvatarIdentityTests_h

#include <QtTest/QtTest>

class AvatarIdentityTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testRenamesReachMixerAfterSessionName();
    void testOwnIdentityNotListed();
    void testOtherIdentityListed();
};

#endif // overte_AvatarIdentityTests_h
