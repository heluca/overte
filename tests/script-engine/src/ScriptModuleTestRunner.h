//
//  ScriptModuleTestRunner.h
//  tests/script-engine/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_ScriptModuleTestRunner_h
#define overte_ScriptModuleTestRunner_h

#include <functional>
#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>

#include "DependencyManager.h"
#include "ResourceManager.h"
#include "ResourceRequestObserver.h"
#include "ScriptCache.h"
#include "ScriptEngine.h"
#include "ScriptEngines.h"
#include "ScriptException.h"
#include "ScriptManager.h"
#include "StatTracker.h"

// Shared by the ES module test suites. Header only: each test executable is built from one .cpp file.

// Generous: a test only reaches this when a script never stops, and then fails on timedOut
static const int MODULE_TEST_TIMEOUT_MS = 10000;

inline void initModuleTestDependencies() {
    DependencyManager::set<ScriptEngines>(ScriptManager::NETWORKLESS_TEST_SCRIPT, QUrl(""));
    DependencyManager::set<ScriptCache>();
    // ScriptCache fetches file: URLs through ResourceManager, on its own thread, so the loader's async path is real
    DependencyManager::set<ResourceManager>(false);
    DependencyManager::set<ResourceRequestObserver>();
    DependencyManager::set<StatTracker>();
    DependencyManager::set<ScriptInitializers>();
}

struct ModuleTestRun {
    QStringList printed;
    QStringList errors;
    QStringList warnings;
    bool timedOut { false };
    std::shared_ptr<ScriptException> uncaughtException;
};

struct ModuleTestOptions {
    bool abortOnUncaughtException { true };
    // Calls ScriptManager::stop() this long after the script starts, if not negative
    int stopAfterMs { -1 };
    // Keeps the manager and its engine alive and processes events this long after run() returns
    int waitAfterRunMs { 0 };
    // The first file's URL, by default its file: URL; the first file's contents are its source either way
    QString entryURL;
    // Files fetched into ScriptCache before the script runs, so that loading them is a cache hit, which ScriptCache
    // answers synchronously
    QStringList cachedFiles;
    // Called with the manager just before it runs, e.g. to add globals
    std::function<void(ScriptManager*)> beforeRun;
    // Called with the manager after run() has returned and waitAfterRunMs has passed, while it is still alive
    std::function<void(ScriptManager*)> afterRun;
};

inline QString urlOf(const QTemporaryDir& dir, const QString& path) {
    return QUrl::fromLocalFile(dir.filePath(path)).toString();
}

// Writes the files (path relative to the directory -> contents) and runs the first one as a top-level script until it
// stops. Module scripts are served from the directory through ScriptCache as file: URLs.
inline ModuleTestRun runScript(const QTemporaryDir& dir, const QList<QPair<QString, QString>>& files,
                               const ModuleTestOptions& options = ModuleTestOptions()) {
    for (const auto& file : files) {
        QString path = dir.filePath(file.first);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            qFatal("cannot write %s", qUtf8Printable(path));
        }
        out.write(file.second.toUtf8());
    }

    ModuleTestRun run;
    for (const QString& path : options.cachedFiles) {
        auto cached = std::make_shared<int>(0);
        DependencyManager::get<ScriptCache>()->getScriptContents(urlOf(dir, path),
            [cached](const QString&, const QString&, bool, bool success, const QString&) { *cached = success ? 1 : -1; });
        if (!QTest::qWaitFor([cached] { return *cached != 0; }, MODULE_TEST_TIMEOUT_MS) || *cached < 0) {
            run.errors.append("Test setup: could not cache " + path);
            return run;
        }
    }
    QString entryURL = options.entryURL.isEmpty() ? urlOf(dir, files.first().first) : options.entryURL;
    ScriptManagerPointer sm = newScriptManager(ScriptManager::NETWORKLESS_TEST_SCRIPT, files.first().second, entryURL);
    sm->setAbortOnUncaughtException(options.abortOnUncaughtException);
    auto scopeGuard = sm->engine()->getScopeGuard();
    QObject::connect(sm.get(), &ScriptManager::printedMessage, [&run](const QString& message, const QString&) {
        run.printed.append(message);
    });
    QObject::connect(sm.get(), &ScriptManager::errorMessage, [&run](const QString& message, const QString&) {
        qWarning() << "Error from engine:" << message;
        run.errors.append(message);
    });
    QObject::connect(sm.get(), &ScriptManager::warningMessage, [&run](const QString& message, const QString&) {
        run.warnings.append(message);
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, sm.get(), [manager = sm.get(), &run] {
        run.timedOut = true;
        manager->stop();
    });
    timeout.start(MODULE_TEST_TIMEOUT_MS);
    QTimer stopper;
    stopper.setSingleShot(true);
    QObject::connect(&stopper, &QTimer::timeout, sm.get(), [manager = sm.get()] { manager->stop(); });
    if (options.stopAfterMs >= 0) {
        stopper.start(options.stopAfterMs);
    }

    if (options.beforeRun) {
        options.beforeRun(sm.get());
    }
    sm->run();
    if (options.waitAfterRunMs > 0) {
        QTest::qWait(options.waitAfterRunMs);
    }
    if (options.afterRun) {
        options.afterRun(sm.get());
    }
    run.uncaughtException = sm->getUncaughtException();
    return run;
}

#endif // overte_ScriptModuleTestRunner_h
