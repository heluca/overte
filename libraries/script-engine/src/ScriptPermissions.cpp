//
//  ScriptPermissions.cpp
//  libraries/script-engine/src/ScriptPermissions.cpp
//
//  Created by dr Karol Suprynowicz on 2024/03/24.
//  Copyright 2024 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ScriptPermissions.h"

#include <array>
#include <QJsonArray>

#include "ScriptEngine.h"
#include "ScriptEnvironment.h"
#include "ScriptManager.h"
#include "Scriptable.h"

static const bool PERMISSIONS_DEBUG_ENABLED = false;

extern const std::array<QString, static_cast<int>(ScriptPermissions::Permission::SCRIPT_PERMISSIONS_SIZE)> scriptPermissionNames {
    // SCRIPT_PERMISSION_GET_AVATAR_URL
    "Permission to get user's avatar URL",
    // SCRIPT_PERMISSION_BOOKMARKS
    "Permission to view user's bookmarked locations",
    // SCRIPT_PERMISSION_KEYBOARD_EVENTS
    "Permission to emit virtual keyboard events",
};

extern const std::array<QString, static_cast<int>(ScriptPermissions::Permission::SCRIPT_PERMISSIONS_SIZE)> scriptPermissionSettingKeyNames {
    // SCRIPT_PERMISSION_GET_AVATAR_URL
    "private/scriptPermissionGetAvatarURLSafeURLs",
    // SCRIPT_PERMISSION_BOOKMARKS
    "private/scriptPermissionBookmarksSafeURLs",
    // SCRIPT_PERMISSION_KEYBOARD_EVENTS
    "private/scriptPermissionKeyboardEventsSafeURLs",
};

extern const std::array<QString, static_cast<int>(ScriptPermissions::Permission::SCRIPT_PERMISSIONS_SIZE)> scriptPermissionSettingEnableKeyNames {
    // SCRIPT_PERMISSION_GET_AVATAR_URL
    "private/scriptPermissionGetAvatarURLEnable",
    // SCRIPT_PERMISSION_BOOKMARKS
    "private/scriptPermissionBookmarksEnable",
    // SCRIPT_PERMISSION_KEYBOARD_EVENTS
    "private/scriptPermissionKeyboardEventsEnable",
};

extern const std::array<bool, static_cast<int>(ScriptPermissions::Permission::SCRIPT_PERMISSIONS_SIZE)> scriptPermissionSettingEnableDefaultValues {
    // SCRIPT_PERMISSION_GET_AVATAR_URL
    true,
    // SCRIPT_PERMISSION_BOOKMARKS
    true,
    // SCRIPT_PERMISSION_KEYBOARD_EVENTS
    true,
};

// True if every URL starts with a safe prefix: the built-in ones or the user's allowlist for the permission
static bool areURLsAllowed(int permissionIndex, const std::vector<QString>& urlsToCheck) {
    QList<QString> safeURLPrefixes = { "file:///", "qrc:/", NetworkingConstants::OVERTE_COMMUNITY_APPLICATIONS,
                                       NetworkingConstants::OVERTE_TUTORIAL_SCRIPTS, "about:console"};
    Setting::Handle<QString> allowedURLsSetting(scriptPermissionSettingKeyNames[permissionIndex]);
    QList<QString> allowedURLs = allowedURLsSetting.get().split("\n");

    for (auto entry : allowedURLs) {
        safeURLPrefixes.push_back(entry);
    }

    for (auto urlToCheck : urlsToCheck) {
        bool urlIsAllowed = false;
        for (const auto& str : safeURLPrefixes) {
            if (!str.isEmpty() && urlToCheck.startsWith(str)) {
                urlIsAllowed = true;
                if (PERMISSIONS_DEBUG_ENABLED) {
                    qDebug() << "ScriptPermissions::isCurrentScriptAllowed: " << scriptPermissionNames[permissionIndex]
                             << " for script " << urlToCheck << " accepted with rule: " << str;
                }
            }
        }

        if (!urlIsAllowed) {
            if (PERMISSIONS_DEBUG_ENABLED) {
                qDebug() << "ScriptPermissions::isCurrentScriptAllowed: " << scriptPermissionNames[permissionIndex]
                         << " for script " << urlToCheck << " rejected.";
            }
            return false;
        }
    }

    return true;
}

// ES module code: an environment is only as trusted as every URL it loaded, whatever frames an await has left on the
// stack. Design question Q3 of #10 would bring classic scripts here by giving them an environment too.
static bool isEnvironmentAllowed(int permissionIndex, ScriptEngine* engine, const ScriptEnvironment& environment,
                                 const QString& scriptURL) {
    // Frames of Script.require modules too, which run in contexts of their own
    QStringList stack = engine->getCurrentScriptURLs(true);
    if (stack.isEmpty()) {
        // A native function called with no script frame, e.g. passed straight to then() or Script.setTimeout(): the
        // environment says whose continuation it is, but not which of its URLs chose the function, so fail closed
        if (PERMISSIONS_DEBUG_ENABLED) {
            qDebug() << "ScriptPermissions::isCurrentScriptAllowed: " << scriptPermissionNames[permissionIndex]
                     << " rejected for a native call without script frames in module environment " << environment.sandboxURL;
        }
        return false;
    }

    std::vector<QString> urlsToCheck;
    if (!scriptURL.startsWith("about:Entities")) {
        urlsToCheck.push_back(scriptURL);
    }
    for (const QString& url : environment.loadedURLs()) {
        urlsToCheck.push_back(url);
    }
    // A frame without a name is checked as the empty string, which no prefix allows
    for (const QString& url : stack) {
        urlsToCheck.push_back(url);
    }
    return areURLsAllowed(permissionIndex, urlsToCheck);
}

bool ScriptPermissions::isCurrentScriptAllowed(ScriptPermissions::Permission permission) {
    if (permission >= ScriptPermissions::Permission::SCRIPT_PERMISSIONS_SIZE) {
        return false;
    }
    int permissionIndex = static_cast<int>(permission);
    // Check if the permission checking is active
    Setting::Handle<bool> isCheckingEnabled(scriptPermissionSettingEnableKeyNames[permissionIndex], scriptPermissionSettingEnableDefaultValues[permissionIndex]);
    if (!isCheckingEnabled.get()) {
        return true;
    }
    // Get the script manager:
    auto engine = Scriptable::engine();
    if (!engine) {
        // When this happens it means that function was called from QML or C++ and should always be allowed
        if (PERMISSIONS_DEBUG_ENABLED) {
            qDebug() << "ScriptPermissions::isCurrentScriptAllowed called outside script engine for permission: "
                     << scriptPermissionNames[permissionIndex];
        }
        return true;
    }
    auto manager = engine->manager();
    if (!manager) {
        qDebug() << "ScriptPermissions::isCurrentScriptAllowed called from script engine with no script manager for permission: " << scriptPermissionNames[permissionIndex];
        return false;
    }
    QString scriptURL = manager->getAbsoluteFilename();
    if (auto environment = engine->currentEnvironment()) {
        return isEnvironmentAllowed(permissionIndex, engine.get(), *environment, scriptURL);
    }

    std::vector<QString> urlsToCheck;

    // If this is an entity script manager, we need to find the file name of the current script instead
    if (!scriptURL.startsWith("about:Entities")) {
        urlsToCheck.push_back(scriptURL);
    }

    // The topmost script frame (every ScriptContext in the chain reports the same one), by the name it was compiled
    // with: ScriptContext::currentFileName() follows sourceURL directives and is for messages. A frame without a name
    // is checked as the empty string, which no prefix allows; with no frame at all only the manager URL counts.
    QStringList frames = engine->getCurrentScriptURLs();
    if (!frames.isEmpty() && frames.first() != scriptURL) {
        urlsToCheck.push_back(frames.first());
    }

    if (PERMISSIONS_DEBUG_ENABLED) {
        qDebug() << "ScriptPermissions::isCurrentScriptAllowed: filename: " << scriptURL << " topmost frame: " << frames.value(0);
    }

    // Check if the script is allowed:
    return areURLsAllowed(permissionIndex, urlsToCheck);
}
