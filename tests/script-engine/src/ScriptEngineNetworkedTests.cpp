//
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include <QSignalSpy>
#include <QDebug>
#include <QFile>
#include <QTextStream>


#include "ScriptEngineNetworkedTests.h"
#include "DependencyManager.h"
#include "FetchTestServer.h"

#include "ScriptEngines.h"
#include "ScriptEngine.h"
#include "ScriptCache.h"
#include "ScriptManager.h"
#include "ScriptEngines.h"
#include "AddressManager.h"
#include "AccountManager.h"
#include "DomainAccountManager.h"
#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "StatTracker.h"
#include "MessagesClient.h"
#include "ResourceScriptingInterface.h"
#include "UserActivityLogger.h"
#include "UserActivityLoggerScriptingInterface.h"
#include "EntityScriptingInterface.h"
#include "ResourceManager.h"
#include "NodeList.h"
#include "../../../libraries/entities/src/EntityScriptingInterface.h"

QTEST_MAIN(ScriptEngineNetworkedTests)



void ScriptEngineNetworkedTests::initTestCase() {
    // AudioClient starts networking, but for the purposes of the tests here we don't care,
    // so just got to use some port.
    int listenPort = 10000;

    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();

    DependencyManager::set<AccountManager>(true); // use the default user agent getter
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, listenPort);

    DependencyManager::set<ScriptEngines>(ScriptManager::CLIENT_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
   // DependencyManager::set<ResourceManager>();
   // DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
   // DependencyManager::set<EntityScriptingInterface>(true);

    DependencyManager::set<MessagesClient>();
    DependencyManager::set<ResourceScriptingInterface>();
    DependencyManager::set<UserActivityLoggerScriptingInterface>();
    DependencyManager::set<EntityScriptingInterface>(true);
    DependencyManager::set<ResourceManager>();
    DependencyManager::set<ResourceRequestObserver>();


    auto nodeList = DependencyManager::get<NodeList>();
    nodeList->startThread();
    nodeList->setFlagTimeForConnectionStep(true);

}

ScriptManagerPointer ScriptEngineNetworkedTests::makeManager(const QString &scriptSource, const QString &scriptFilename,
                                                             ScriptManager::Context context) {
    ScriptManagerPointer sm = scriptManagerFactory(context, scriptSource, scriptFilename);


    sm->setAbortOnUncaughtException(true);

    connect(sm.get(), &ScriptManager::scriptLoaded, [](const QString& filename){
        qWarning() << "Loaded script" << filename;
    });


    connect(sm.get(), &ScriptManager::errorLoadingScript, [](const QString& filename){
        qWarning() << "Failed to load script" << filename;
    });

    connect(sm.get(), &ScriptManager::printedMessage, [](const QString& message, const QString& engineName){
        qDebug() << "Printed message from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::infoMessage, [](const QString& message, const QString& engineName){
        qInfo() << "Info message from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::warningMessage, [](const QString& message, const QString& engineName){
        qWarning() << "Warning from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::errorMessage, [](const QString& message, const QString& engineName){
        qCritical() << "Error from engine" << engineName << ": " << message;
    });

    connect(sm.get(), &ScriptManager::finished, [](const QString& fileNameString, ScriptManagerPointer smp){
        qInfo() << "Finished running script" << fileNameString;
    });

    connect(sm.get(), &ScriptManager::runningStateChanged, [sm](){
        qInfo() << "Running state changed. Running = " << sm->isRunning() << "; Stopped = " << sm->isStopped() << "; Finished = " << sm->isFinished();
    });

    connect(sm.get(), &ScriptManager::unhandledException, [](std::shared_ptr<ScriptException> exception){
        qWarning() << "Exception from engine: " << exception;
    });


    return sm;
}

void ScriptEngineNetworkedTests::testScriptRequire() {
    auto sm = makeManager(
        "print(\"Starting\");"
        "Script.require('./tests/c.js');"
        "print(\"Done\");"
        "Script.stop(true);", "testRequire.js");
    QStringList printed;
    QStringList expected {"Starting", "Value from A: 6", "Value from B: 6", "Done"};


    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(!sm->isFinished());

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });


    qInfo() << "About to run script";
    sm->run();

    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(sm->isFinished());

    qDebug() << "Script printed :" << printed;
    qDebug() << "Expected output:" << expected;

    for(const auto &str : expected) {
        QString nomatch = QString("Result didn't contain expected string '%1'").arg(str);
        QVERIFY2(printed.contains(str), qPrintable(nomatch));
    }
}

void ScriptEngineNetworkedTests::testRequire() {
    auto sm = makeManager(
        "print(\"Starting\");"
        "require('./tests/c_require.js');"
        "print(\"Done\");"
        "Script.stop(true);", "testRequire.js");
    QStringList printed;
    QStringList expected {"Starting", "Value from A: 6", "Value from B: 6", "Done"};


    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(!sm->isFinished());

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });


    qInfo() << "About to run script";
    sm->run();

    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(sm->isFinished());

    for(const auto &str : expected) {
        QString nomatch = QString("Result didn't contain expected string '%1'").arg(str);
        QVERIFY2(printed.contains(str), qPrintable(nomatch));
    }

}


void ScriptEngineNetworkedTests::testRequireInfinite() {
    auto sm = makeManager(
        "print(\"Starting\");"
        "Script.require('./tests/require_inf_a.js');"
        "print(\"Done\");"
        "Script.stop(true);", "testRequireInf.js");
    QString errors;
    QStringList printed;



    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(!sm->isFinished());

    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        errors.append(message);
    });
    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });


    qInfo() << "About to run script";
    sm->run();

    QVERIFY(!sm->isRunning());
    QVERIFY(!sm->isStopped());
    QVERIFY(sm->isFinished());

    // Acceptable results: either failing, or realizing a prior module is already included
    QVERIFY(errors.contains("Maximum call stack size exceeded") || printed.contains("Done") );
}

QString ScriptEngineNetworkedTests::runWithMetaverseToken(const QString& base, const QString& source,
                                                          ScriptManager::Context context) {
    qputenv("HIFI_METAVERSE_URL", base.toUtf8());
    OAuthAccessToken token;
    token.token = "mv-t0ken";
    DependencyManager::get<AccountManager>()->getAccountInfo().setAccessToken(token);

    auto sm = makeManager(source, "testMetaverseToken.js", context);
    QString printed;
    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName) {
        printed.append(message);
    });
    QTimer::singleShot(10000, sm.get(), [manager = sm.get()] { manager->stop(); });
    sm->run();

    DependencyManager::get<AccountManager>()->getAccountInfo().setAccessToken(OAuthAccessToken());
    qunsetenv("HIFI_METAVERSE_URL");
    return printed;
}

// Each URL by XMLHttpRequest and then fetch, printing what the target saw of the Authorization header
static const QString METAVERSE_TOKEN_REQUESTS =
    "var results = [];\n"
    "function viaXhr(url) {\n"
    "    return new Promise(function(resolve) {\n"
    "        var request = new XMLHttpRequest();\n"
    "        request.onreadystatechange = function() {\n"
    "            if (request.readyState === request.DONE) {\n"
    "                resolve(request.status + ':' + request.responseText);\n"
    "            }\n"
    "        };\n"
    "        request.open('GET', url);\n"
    "        request.send();\n"
    "    });\n"
    "}\n"
    "function viaFetch(url) {\n"
    "    return fetch(url).then(function(response) {\n"
    "        return response.text().then(function(text) { return response.status + ':' + text; });\n"
    "    });\n"
    "}\n"
    "var chain = Promise.resolve();\n"
    "URLS.forEach(function(url) {\n"
    "    chain = chain.then(function() { return viaXhr(url); }).then(function(value) {\n"
    "        results.push('xhr ' + value);\n"
    "        return viaFetch(url);\n"
    "    }).then(function(value) { results.push('fetch ' + value); });\n"
    "});\n"
    "chain.catch(function(e) { results.push('error ' + e); }).then(function() {\n"
    "    print(results.join('|'));\n"
    "    Script.stop(true);\n"
    "});\n";

void ScriptEngineNetworkedTests::testMetaverseTokenRedirect() {
    FetchTestServer metaverse;
    FetchTestServer other;
    QString script = "var URLS = ['" + metaverse.base() + "/api/auth', '" + metaverse.base() + "/api/redirect-out', '" +
        metaverse.base() + "/api/redirect-to/302/" + other.base() + "/auth', '" + metaverse.base() +
        "/api/redirect-to/307/" + other.base() + "/auth'];\n" + METAVERSE_TOKEN_REQUESTS;

    QString printed = runWithMetaverseToken(metaverse.base(), script, ScriptManager::CLIENT_SCRIPT);
    // The token follows a redirect within the metaverse server, even out of /api/, and is left off one to another origin
    QCOMPARE(printed, QString("xhr 200:Bearer mv-t0ken|fetch 200:Bearer mv-t0ken|"
        "xhr 200:Bearer mv-t0ken|fetch 200:Bearer mv-t0ken|xhr 200:none|fetch 200:none|xhr 200:none|fetch 200:none"));
}

void ScriptEngineNetworkedTests::testMetaverseTokenContexts() {
    FetchTestServer metaverse;
    QString script = "var URLS = ['" + metaverse.base() + "/api/auth'];\n" + METAVERSE_TOKEN_REQUESTS;

    const QString WITH_TOKEN = "xhr 200:Bearer mv-t0ken|fetch 200:Bearer mv-t0ken";
    const QString WITHOUT_TOKEN = "xhr 200:none|fetch 200:none";
    QCOMPARE(runWithMetaverseToken(metaverse.base(), script, ScriptManager::CLIENT_SCRIPT), WITH_TOKEN);
    QCOMPARE(runWithMetaverseToken(metaverse.base(), script, ScriptManager::AGENT_SCRIPT), WITH_TOKEN);
    QCOMPARE(runWithMetaverseToken(metaverse.base(), script, ScriptManager::ENTITY_CLIENT_SCRIPT), WITHOUT_TOKEN);
    QCOMPARE(runWithMetaverseToken(metaverse.base(), script, ScriptManager::ENTITY_SERVER_SCRIPT), WITHOUT_TOKEN);
}
