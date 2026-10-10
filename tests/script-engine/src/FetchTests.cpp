//
//  FetchTests.cpp
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include <QTcpServer>
#include <QTemporaryFile>

#include "FetchTests.h"
#include "DependencyManager.h"
#include "FetchClass.h"
#include "FetchTestServer.h"
#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "ScriptCache.h"
#include "ScriptEngines.h"
#include "StatTracker.h"

QTEST_MAIN(FetchTests)

void FetchTests::initTestCase() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    // For fetch() of file: URLs; ATP stays off, as there is no asset server
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

static ScriptManagerPointer makeManager(const QString& source, const QString& filename) {
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, source, filename);
    sm->setAbortOnUncaughtException(true);
    QObject::connect(sm.get(), &ScriptManager::printedMessage, [](const QString& message, const QString& engineName) {
        qDebug() << "Printed message from engine" << engineName << ": " << message;
    });
    QObject::connect(sm.get(), &ScriptManager::warningMessage, [](const QString& message, const QString& engineName) {
        qWarning() << "Warning from engine" << engineName << ": " << message;
    });
    QObject::connect(sm.get(), &ScriptManager::errorMessage, [](const QString& message, const QString& engineName) {
        qCritical() << "Error from engine" << engineName << ": " << message;
    });
    QObject::connect(sm.get(), &ScriptManager::unhandledException, [](std::shared_ptr<ScriptException> exception) {
        qWarning() << "Exception from engine: " << exception;
    });
    return sm;
}

// Generous: a test only reaches this when a promise never settles, and then fails on the missing output
static const int FETCH_TEST_TIMEOUT_MS = 10000;

bool FetchTests::runFetchScript(const QString& source, const QString& filename, QString& printed, QStringList& errors,
                                bool allowLocalFiles, const QString& closureSource) {
    auto sm = makeManager(source, filename);
    auto scopeGuard = sm->engine()->getScopeGuard();
    // The test context is NETWORKLESS_TEST_SCRIPT, where init() registers neither XMLHttpRequest nor fetch.
    // allowLocalFiles false is what ScriptManager::init() passes for entity scripts.
    registerFetchGlobals(sm->engine().get(), allowLocalFiles);
    ClosureRunner closure(sm->engine().get(), closureSource);
    if (!closureSource.isEmpty()) {
        sm->engine()->registerGlobalObject(scopeGuard.get(), "closure", &closure);
    }

    connect(sm.get(), &ScriptManager::printedMessage, [&printed](const QString& message, const QString& engineName){
        printed.append(message);
    });
    connect(sm.get(), &ScriptManager::errorMessage, [&errors](const QString& message, const QString& engineName){
        errors.append(message);
    });
    bool timedOut = false;
    QTimer::singleShot(FETCH_TEST_TIMEOUT_MS, sm.get(), [manager = sm.get(), &timedOut] {
        timedOut = true;
        manager->stop();
    });

    sm->run();
    return !timedOut && !sm->getUncaughtException();
}

void FetchTests::testFetchHeaders() {
    QString script =
        "var results = [];\n"
        "var h = new Headers({ 'Content-Type': 'text/plain', 'X-B': '1' });\n"
        "h.append('x-b', '2');\n"
        "results.push(h.get('content-type'), h.get('X-b'), h.has('X-B'), h.get('missing'));\n"
        "h.set('X-B', '3');\n"
        "h.delete('Content-Type');\n"
        "results.push(h.has('content-type'), JSON.stringify(h.entries()));\n"
        "var seen = [];\n"
        "h.forEach(function(value, name) { seen.push(name + '=' + value); });\n"
        "results.push(seen.join(';'), JSON.stringify(Array.from(new Headers([['b', '2'], ['a', '1']]))));\n"
        "try { h.set('bad name', 'x'); } catch (e) { results.push(e.name); }\n"
        "try { h.append('x-c', 'split\\r\\nInjected: yes'); } catch (e) { results.push(e.name); }\n"
        "var request = new Request('http://example.invalid/x', { method: 'post', headers: h, body: 'hi' });\n"
        "results.push(request.method, request.url, request.headers.get('x-b'), request.headers.get('content-type'));\n"
        "var copy = new Request(request, { method: 'PUT' });\n"
        "results.push(copy.method, copy.url, copy instanceof Request);\n"
        "try { new Request('http://example.invalid/', { body: 'x' }); } catch (e) { results.push(e.name); }\n"
        "try { new AbortSignal(); } catch (e) { results.push(e.name); }\n"
        "var response = new Response('made', { status: 201, headers: { 'X-Made': 'yes' } });\n"
        "results.push(response.status, response.ok, response.headers.get('x-made'));\n"
        "response.text().then(function(text) {\n"
        "    results.push(text, response.bodyUsed);\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchHeaders.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("text/plain,1, 2,true,,false,[[\"x-b\",\"3\"]],x-b=3,[[\"a\",\"1\"],[\"b\",\"2\"]],TypeError,TypeError,"
        "POST,http://example.invalid/x,3,text/plain;charset=UTF-8,PUT,http://example.invalid/x,true,TypeError,TypeError,"
        "201,true,yes,made,true"));
}

void FetchTests::testFetchText() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "fetch(BASE + '/text').then(function(response) {\n"
        "    results.push(response.status, response.statusText, response.ok, response.url === BASE + '/text',\n"
        "        response.redirected, response.headers.get('Content-Type'), response.bodyUsed);\n"
        "    var text = response.text();\n"
        "    results.push(response.bodyUsed);\n"
        "    return text.then(function(value) { results.push(value); return response.text(); });\n"
        "}).catch(function(e) {\n"
        "    results.push(e.name);\n"
        "    return fetch(new Request(BASE + '/redirect'));\n"
        "}).then(function(response) {\n"
        "    results.push(response.redirected, response.url === BASE + '/text');\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(buffer instanceof ArrayBuffer, buffer.byteLength);\n"
        "}).catch(function(e) { results.push('unexpected ' + e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchText.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("200,OK,true,true,false,text/plain,false,true,hello fetch,TypeError,true,true,true,11"));
}

void FetchTests::testFetchJson() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/json', { headers: new Headers({ Accept: 'application/json' }) }).then(function(response) {\n"
        "    return response.json();\n"
        "}).then(function(value) {\n"
        "    print(value.answer + ',' + value.list.length);\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchJson.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("42,3"));
}

void FetchTests::testFetchPostEcho() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "fetch(BASE + '/echo', { method: 'POST', headers: { 'X-Test': 'custom value' }, body: 'posted body' })\n"
        ".then(function(response) {\n"
        "    var h = response.headers;\n"
        "    results.push(response.status, h.get('x-echo-method'), h.get('x-echo-test'), h.get('x-echo-type'),\n"
        "        h.get('x-echo-agent'));\n"
        "    return response.text();\n"
        "}).then(function(text) {\n"
        "    results.push(text);\n"
        "    return fetch(BASE + '/echo', { method: 'PUT', body: new Uint8Array([1, 2, 250]) });\n"
        "}).then(function(response) {\n"
        "    results.push(response.headers.get('x-echo-method'));\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(Array.from(new Uint8Array(buffer)).join(' '));\n"
        "    return fetch(BASE + '/echo', { method: 'DELETE' });\n"
        "}).then(function(response) {\n"
        "    results.push(response.headers.get('x-echo-method'), response.status);\n"
        "}).catch(function(e) { results.push('unexpected ' + e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchPostEcho.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("200,POST,custom value,text/plain;charset=UTF-8,Mozilla/5.0 (OverteInterface),posted body,"
        "PUT,1 2 250,DELETE,200"));
}

void FetchTests::testFetch404() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/404').then(function(response) {\n"
        "    return response.text().then(function(text) {\n"
        "        print([response.status, response.statusText, response.ok, text].join(','));\n"
        "    });\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetch404.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("404,Not Found,false,missing"));
}

void FetchTests::testFetch400() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/400').then(function(response) {\n"
        "    return response.json().then(function(value) {\n"
        "        print([response.status, response.statusText, response.ok, value.error].join(','));\n"
        "    });\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetch400.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("400,Bad Request,false,bad"));
}

void FetchTests::testFetchBadResponseHeader() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/badheader').then(function(response) {\n"
        "    print(response.status + ',' + response.headers.keys().join(';'));\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchBadResponseHeader.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("200,connection;content-length;x(bad)"));
}

void FetchTests::testFetchBadJson() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "fetch(BASE + '/badjson').then(function(response) {\n"
        "    return response.json().then(function(value) { print('unexpected ' + value); }, function(e) {\n"
        "        print([response.ok, e.name, e instanceof SyntaxError, response.bodyUsed].join(','));\n"
        "    });\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchBadJson.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("true,SyntaxError,true,true"));
}

void FetchTests::testFetchNetworkError() {
    quint16 refusedPort;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        refusedPort = probe.serverPort();
    }
    QString script = "var REFUSED = 'http://127.0.0.1:" + QString::number(refusedPort) + "/text';\n"
        "var results = [];\n"
        "function failure(input, init) {\n"
        "    return fetch(input, init).then(function() { results.push('resolved'); },\n"
        "        function(e) { results.push(e.name + ':' + (e instanceof TypeError)); });\n"
        "}\n"
        "failure(REFUSED)\n"
        ".then(function() { return failure('atp:/missing.txt'); })\n"
        ".then(function() { return failure('file:///nonexistent', { method: 'POST', body: 'x' }); })\n"
        ".then(function() { return failure('http://[bad'); })\n"
        ".then(function() { print(results.join(',')); Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchNetworkError.js", printed, errors));
    QCOMPARE(errors, QStringList());
    // ATP is disabled in this test's ResourceManager, so createResourceRequest() refuses atp: URLs
    QCOMPARE(printed, QString("TypeError:true,TypeError:true,TypeError:true,TypeError:true"));
}

void FetchTests::testFetchAbort() {
    FetchTestServer server;
    QString script = "var BASE = '" + server.base() + "';\n"
        "var results = [];\n"
        "var controller = new AbortController();\n"
        "var events = [];\n"
        "controller.signal.addEventListener('abort', function(event) { events.push(event.type); });\n"
        "var hanging = fetch(BASE + '/hang', { signal: controller.signal });\n"
        "setTimeout(function() { controller.abort(); }, 0);\n"
        "hanging.then(function() { results.push('resolved'); }, function(e) {\n"
        "    results.push(e.name, controller.signal.aborted, events.join(';'));\n"
        "    var early = new AbortController();\n"
        "    early.abort();\n"
        "    return fetch(BASE + '/text', { signal: early.signal });\n"
        "}).then(function() { results.push('resolved'); }, function(e) {\n"
        "    results.push(e.name);\n"
        "    var withReason = new AbortController();\n"
        "    var request = fetch(BASE + '/hang', { signal: withReason.signal });\n"
        "    withReason.abort('why');\n"
        "    return request;\n"
        "}).then(function() { results.push('resolved'); }, function(e) { results.push(e); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchAbort.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("AbortError,true,abort,AbortError,why"));
}

void FetchTests::testFetchFile() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("file body \xC3\xA9");
    file.close();
    const QString fileUrl = QUrl::fromLocalFile(file.fileName()).toString();

    QString script = "var FILE = '" + fileUrl + "';\n"
        "var results = [];\n"
        "fetch(FILE).then(function(response) {\n"
        "    results.push(response.status, response.ok, response.url === FILE);\n"
        "    return response.text();\n"
        "}).then(function(text) {\n"
        "    results.push(text, text.length);\n"
        "    return fetch(FILE, { method: 'HEAD' });\n"
        "}).then(function(response) {\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    results.push(buffer.byteLength);\n"
        "    return fetch(FILE + '.missing');\n"
        "}).then(function() { results.push('resolved'); }, function(e) { results.push(e.name); }).then(function() {\n"
        "    print(results.join(','));\n"
        "    Script.stop(true);\n"
        "});\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchFile.js", printed, errors));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString::fromUtf8("200,true,true,file body \xC3\xA9,11,0,TypeError"));
}

void FetchTests::testFetchFileRefused() {
    QTemporaryFile file;
    QVERIFY(file.open());
    file.write("secret");
    file.close();
    const QString fileUrl = QUrl::fromLocalFile(file.fileName()).toString();

    // What an entity script gets: no local files however they are named, other schemes as before
    QString script = "var results = [];\n"
        "function attempt(input) {\n"
        "    return fetch(input).then(function() { results.push('resolved'); }, function(e) {\n"
        "        results.push(e.name + ':' + (e.message.indexOf('only available to Interface and agent scripts') >= 0));\n"
        "    });\n"
        "}\n"
        "attempt('" + fileUrl + "')\n"
        ".then(function() { return attempt('" + file.fileName() + "'); })\n"
        ".then(function() { return attempt('qrc:/anything'); })\n"
        ".then(function() { return attempt('gopher://example.invalid/'); })\n"
        ".then(function() { return attempt('atp:/missing.txt'); })\n"
        ".then(function() { print(results.join(',')); Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript(script, "testFetchFileRefused.js", printed, errors, false));
    QCOMPARE(errors, QStringList());
    // The path is resolved to file:, and ResourceManager would read an unknown scheme as a local path; atp: is refused
    // for another reason (ATP is off in this test)
    QCOMPARE(printed, QString("TypeError:true,TypeError:true,TypeError:true,TypeError:true,TypeError:false"));
}

void FetchTests::testFetchClosureArrayBuffer() {
    FetchTestServer server;
    QString closureSource = "var BASE = '" + server.base() + "';\n"
        "var type;\n"
        "fetch(BASE + '/echo', { method: 'POST', body: new Uint8Array([7, 8, 9]).buffer }).then(function(response) {\n"
        "    type = response.headers.get('x-echo-type');\n"
        "    return response.arrayBuffer();\n"
        "}).then(function(buffer) {\n"
        "    print(Array.from(new Uint8Array(buffer)).join(' ') + '|' + type);\n"
        "}).catch(function(e) { print('unexpected ' + e); }).then(function() { Script.stop(true); });\n";

    QString printed;
    QStringList errors;
    QVERIFY(runFetchScript("closure.run();", "testFetchClosureArrayBuffer.js", printed, errors, true, closureSource));
    QCOMPARE(errors, QStringList());
    QCOMPARE(printed, QString("7 8 9|"));
}
