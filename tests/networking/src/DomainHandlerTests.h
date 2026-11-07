//
//  DomainHandlerTests.h
//  tests/networking/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_DomainHandlerTests_h
#define overte_DomainHandlerTests_h

#include <QtTest/QtTest>

class DomainHandlerTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void testSettingsFromDomainServer();
    void testSettingsFromOtherSocket();
    void testDomainListFromOtherSocket();
    void testDomainListFromDomainServer();
    void testAddedNodeFromOtherSocket();
    void testRemovedNode();
    void testConnectionToken();
    void testUsernameFromIDReply();
    void testConnectionDenied();
};

#endif // overte_DomainHandlerTests_h
