//
//  ScriptModuleEntityProbe.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleEntityProbe_h
#define overte_ScriptModuleEntityProbe_h

// Shared by the ES module entity script suites. Header only: each test executable is built from one .cpp file, which
// includes "moc_ScriptModuleEntityProbe.cpp" for the probe's meta-object.

#include <memory>

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include "AccountManager.h"
#include "AddressManager.h"
#include "DomainAccountManager.h"
#include "EntityScriptUtils.h"
#include "NodeList.h"
#include "ScriptModuleTestRunner.h"

static const QString ENTITY_A = "{0f6c1fa8-55a3-4a63-9b7b-2f8f9b0e4c11}";
static const QString ENTITY_B = "{5d2a7c31-8e44-4b0f-a1c9-3e6f20b7d952}";
static const QString ENTITY_C = "{c3a91e07-2b6d-4f58-9e14-6d8b0a7f2c35}";
static const QString CALLER = "{9b1e4f20-3c7d-4a85-b6e2-71d0c5a8f3e4}";
// Prepended to each test's main.mjs
static const QString TICK = "const tick = () => new Promise(function(resolve) { Script.setTimeout(resolve, 0); });\n";

// Exposed to test scripts as Probe
class EntityModuleProbe : public QObject {
    Q_OBJECT
public:
    explicit EntityModuleProbe(ScriptManager* manager) : _manager(manager) {}

    // ScriptManager::loadEntityScript(): names the epoch and marks the entity LOADING. Its own fetch is not used: a test
    // manager does not drain its content queue.
    Q_INVOKABLE void prepare(const QString& entityID, const QString& url, bool reload) {
        _manager->loadEntityScript(EntityItemID(QUuid(entityID)), url, reload);
    }
    // Fetches the source through ScriptCache, bypassing it for a reload, and hands it to entityScriptContentAvailable()
    // from the event loop, as the entity script server does. The promise resolves with the status once it is neither
    // pending nor loading, followed by ": " and the error information if there is any.
    Q_INVOKABLE ScriptValue handOver(const QString& entityID, const QString& url, bool reload) {
        ScriptPromise promise = _manager->engine()->newPromise();
        settleOnStatus(entityID, url, promise.resolver);
        fetch(url, reload, [this, entityID, url](const QString& contents, bool isURL, bool success, const QString& status) {
            QMetaObject::invokeMethod(_manager, [this, entityID, url, contents, isURL, success, status] {
                _handedOver.insert(entityID);
                contentAvailable(entityID, url, contents, isURL, success, status);
            }, Qt::QueuedConnection);
        });
        return promise.promise;
    }
    // Resolves as handOver()'s promise does, for a source handed over by other means
    Q_INVOKABLE ScriptValue settled(const QString& entityID, const QString& url) {
        ScriptPromise promise = _manager->engine()->newPromise();
        settleOnStatus(entityID, url, promise.resolver);
        return promise.promise;
    }
    Q_INVOKABLE ScriptValue load(const QString& entityID, const QString& url, bool reload) {
        prepare(entityID, url, reload);
        return handOver(entityID, url, reload);
    }
    // Fetches the entity's source now, then hands it over at once, from inside the status update, as soon as the
    // watched entity script fails: as an entity script server constructs the next entity in the same frame. The
    // promise resolves once the source is fetched.
    Q_INVOKABLE ScriptValue handOverOnError(const QString& watchedID, const QString& watchedURL, const QString& entityID,
                                            const QString& url) {
        ScriptPromise promise = _manager->engine()->newPromise();
        auto resolver = promise.resolver;
        fetch(url, false, [this, watchedID, watchedURL, entityID, url, resolver](const QString& contents, bool isURL,
                                                                                 bool success, const QString& status) {
            auto connection = std::make_shared<QMetaObject::Connection>();
            *connection = QObject::connect(_manager, &ScriptManager::entityScriptDetailsUpdated, this,
                [=, this] {
                    if (!this->status(watchedID, watchedURL).startsWith("error")) {
                        return;
                    }
                    QObject::disconnect(*connection);
                    contentAvailable(entityID, url, contents, isURL, success, status);
                });
            resolver->resolve(QVariant(true));
        });
        return promise.promise;
    }
    // Empty without details
    Q_INVOKABLE QString status(const QString& entityID, const QString& url) const {
        EntityScriptDetails details;
        bool found = _manager->getEntityScriptDetails(EntityItemID(QUuid(entityID)), url, details);
        return found ? EntityScriptStatus_::valueToKey(details.status).toLower() : QString();
    }
    // ScriptManager::unloadEntityScript(), removing the entity from the map as for a deleted entity
    Q_INVOKABLE void unload(const QString& entityID, const QString& url) {
        _manager->unloadEntityScript(EntityItemID(QUuid(entityID)), url, true);
    }
    // ScriptManager::callEntityScriptMethod() from the event loop, as an entity script server calls it, as a remote call
    // when remoteCallerID is not empty. The promise resolves once the call has returned.
    Q_INVOKABLE ScriptValue call(const QString& entityID, const QString& method, const QStringList& params,
                                 const QString& remoteCallerID) {
        ScriptPromise promise = _manager->engine()->newPromise();
        QMetaObject::invokeMethod(_manager, [manager = _manager, entityID, method, params, remoteCallerID,
                                             resolver = promise.resolver] {
            manager->callEntityScriptMethod(EntityItemID(QUuid(entityID)), method, params, QUuid(remoteCallerID));
            resolver->resolve(QVariant(true));
        }, Qt::QueuedConnection);
        return promise.promise;
    }
    // Whether handOver() has handed the entity's source to entityScriptContentAvailable()
    Q_INVOKABLE bool handedOver(const QString& entityID) const { return _handedOver.contains(entityID); }
    // Test-only: the engine's module records, all epochs (ScriptEngineMemoryStatistics::moduleRecords)
    Q_INVOKABLE int moduleRecords() const {
        return static_cast<int>(_manager->engine()->getMemoryUsageStatistics().moduleRecords);
    }
    // ScriptManager::currentEntityIdentifier(), empty when null
    Q_INVOKABLE QString entityID() const {
        EntityItemID id = _manager->currentEntityIdentifier();
        return id.isNull() ? QString() : id.toString();
    }
    Q_INVOKABLE QString sandboxURL() const { return _manager->currentSandboxURL().toString(); }
    Q_INVOKABLE void writeFile(const QString& url, const QString& contents) {
        QFile file(QUrl(url).toLocalFile());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qFatal("cannot write %s", qUtf8Printable(url));
        }
        file.write(contents.toUtf8());
    }

    // The entity of each errorEntityMessage, in order; empty for none
    QStringList errorEntities;

private:
    using Fetched = std::function<void(const QString& contents, bool isURL, bool success, const QString& status)>;
    void fetch(const QString& url, bool reload, Fetched fetched) {
        DependencyManager::get<ScriptCache>()->getScriptContents(url,
            [fetched](const QString&, const QString& contents, bool isURL, bool success, const QString& status) {
                fetched(contents, isURL, success, status);
            }, reload);
    }
    void contentAvailable(const QString& entityID, const QString& url, const QString& contents, bool isURL, bool success,
                          const QString& status) {
        QMetaObject::invokeMethod(_manager, "entityScriptContentAvailable", Qt::DirectConnection,
            Q_ARG(EntityItemID, EntityItemID(QUuid(entityID))), Q_ARG(QString, url), Q_ARG(QString, contents),
            Q_ARG(bool, isURL), Q_ARG(bool, success), Q_ARG(QString, status));
    }
    void settleOnStatus(const QString& entityID, const QString& url, ScriptPromiseResolverPointer resolver) {
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = QObject::connect(_manager, &ScriptManager::entityScriptDetailsUpdated, this,
            [this, manager = _manager, entityID, url, resolver, connection] {
                EntityScriptDetails details;
                if (!manager->getEntityScriptDetails(EntityItemID(QUuid(entityID)), url, details)
                    || details.status == EntityScriptStatus::PENDING || details.status == EntityScriptStatus::LOADING) {
                    return;
                }
                QObject::disconnect(*connection);
                QString status = EntityScriptStatus_::valueToKey(details.status).toLower();
                QVariant result(details.errorInfo.isEmpty() ? status : status + ": " + details.errorInfo);
                // Settled from the event loop: settling it here would run the test script's continuation in the middle
                // of the load, before preload, when the load was not itself called from script
                QMetaObject::invokeMethod(manager, [resolver, result] { resolver->resolve(result); }, Qt::QueuedConnection);
            });
    }

    ScriptManager* _manager;
    QSet<QString> _handedOver;
};

// Serves files by path on 127.0.0.1 from a thread of its own, so that ScriptCache answers a second, unforced request
// for one from memory even after its content here has changed. Exposed to test scripts as Server.
class MutableScriptServer : public QObject {
    Q_OBJECT
public:
    MutableScriptServer() {
        _context.moveToThread(&_thread);
        _thread.start();
        QMetaObject::invokeMethod(&_context, [this] {
            _server = new QTcpServer();
            _server->listen(QHostAddress::LocalHost, 0);
            _port = _server->serverPort();
            QObject::connect(_server, &QTcpServer::newConnection, _server, [this] {
                while (QTcpSocket* socket = _server->nextPendingConnection()) {
                    serve(socket);
                }
            });
        }, Qt::BlockingQueuedConnection);
    }
    ~MutableScriptServer() {
        QMetaObject::invokeMethod(&_context, [this] { delete _server; }, Qt::BlockingQueuedConnection);
        _thread.quit();
        _thread.wait();
    }
    QString base() const { return QString("http://127.0.0.1:%1/").arg(_port); }
    Q_INVOKABLE void set(const QString& path, const QString& content) {
        QMutexLocker locker(&_mutex);
        _files[path] = content.toUtf8();
    }

private:
    void serve(QTcpSocket* socket) {
        auto received = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received] {
            received->append(socket->readAll());
            if (!received->contains("\r\n\r\n")) {
                return;
            }
            QString path = QString::fromUtf8(received->split(' ').value(1)).mid(1);
            QByteArray content;
            bool found;
            {
                QMutexLocker locker(&_mutex);
                found = _files.contains(path);
                content = _files.value(path);
            }
            socket->write((found ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") +
                          QByteArray("\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: ") +
                          QByteArray::number(content.size()) + "\r\nConnection: close\r\n\r\n" + content);
            socket->disconnectFromHost();
        });
    }

    QMutex _mutex;
    QHash<QString, QByteArray> _files;
    QThread _thread;
    QObject _context;
    QTcpServer* _server { nullptr };
    quint16 _port { 0 };
};

inline void initEntityTestDependencies() {
    // Loading an entity script consults the domain, for the entity script allowlist
    DependencyManager::registerInheritance<LimitedNodeList, NodeList>();
    DependencyManager::set<AccountManager>(true);
    DependencyManager::set<DomainAccountManager>();
    DependencyManager::set<AddressManager>();
    DependencyManager::set<NodeList>(NodeType::Agent, 0);
    initModuleTestDependencies();
}

// "@A", "@B", "@C" and "@CALLER" stand for the IDs, "@/path" for the URL of a file in the test directory, and
// "@H/path" for one on the server whose base is given
inline QString fillEntityTest(QString text, const QTemporaryDir& dir, const QString& httpBase = QString()) {
    text.replace("@A", ENTITY_A).replace("@B", ENTITY_B).replace("@CALLER", CALLER).replace("@C", ENTITY_C);
    text.replace("@H/", httpBase);
    static const QRegularExpression path("@/([\\w./-]+)");
    QRegularExpressionMatch match;
    while ((match = path.match(text)).hasMatch()) {
        text.replace(match.capturedStart(), match.capturedLength(), urlOf(dir, match.captured(1)));
    }
    return text;
}

// Runs main.mjs, the first file, with Probe, and with Server if there is one
inline ModuleTestRun runEntities(const QTemporaryDir& dir, QList<QPair<QString, QString>> files,
                                 std::unique_ptr<EntityModuleProbe>& probe, MutableScriptServer* server = nullptr) {
    for (auto& file : files) {
        file.second = fillEntityTest(file.second, dir, server ? server->base() : QString());
    }
    files.first().second = TICK + files.first().second;
    ModuleTestOptions options;
    options.beforeRun = [&probe, server](ScriptManager* manager) {
        if (server) {
            manager->engine()->globalObject().setProperty("Server", manager->engine()->newQObject(server));
        }
        // loadEntityScript() refuses to load, through a null pointer, without the ScriptEngines that made the manager
        QSharedPointer<ScriptEngines> scriptEngines = DependencyManager::get<ScriptEngines>();
        manager->setScriptEngines(scriptEngines);
        probe = std::make_unique<EntityModuleProbe>(manager);
        manager->engine()->globalObject().setProperty("Probe", manager->engine()->newQObject(probe.get()));
        QObject::connect(manager, &ScriptManager::errorEntityMessage, probe.get(),
            [probe = probe.get()](const QString&, const QString&, int, const EntityItemID& entityID, bool) {
                probe->errorEntities.append(entityID.isNull() ? QString() : entityID.toString());
            });
    };
    return runScript(dir, files, options);
}

#endif // overte_ScriptModuleEntityProbe_h
