#
#  MacDeployQtWebEngine.cmake
#  cmake
#
#  Copyright 2026 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#  SPDX-License-Identifier: Apache-2.0
#
#  Run with cmake -P after macdeployqt. The Conan Qt is a non-framework build, and macdeployqt only knows
#  how to deploy QtWebEngine's helper process and data from inside QtWebEngineCore.framework, so with
#  this Qt the deployed bundle aborts at startup with "Could not find QtWebEngineProcess". Copy the
#  helper next to the executable, the resources and locales under Contents/Resources, and tell Qt where
#  they are through qt.conf (QtWebEngine looks them up through QLibraryInfo).
#
#  Arguments: -DBUNDLE=<path to the .app> -DQT_PREFIX=<Qt installation prefix>
#

if (NOT BUNDLE OR NOT QT_PREFIX)
    message(FATAL_ERROR "MacDeployQtWebEngine.cmake needs -DBUNDLE and -DQT_PREFIX")
endif ()

set(WEBENGINE_PROCESS "${QT_PREFIX}/libexec/QtWebEngineProcess")
if (NOT EXISTS "${WEBENGINE_PROCESS}")
    message(STATUS "No ${WEBENGINE_PROCESS}; assuming a framework build of Qt that macdeployqt handled itself")
    return()
endif ()

message(STATUS "Deploying QtWebEngine helper process and data into ${BUNDLE}")
file(COPY "${WEBENGINE_PROCESS}" DESTINATION "${BUNDLE}/Contents/MacOS")
file(COPY "${QT_PREFIX}/resources" DESTINATION "${BUNDLE}/Contents/Resources")
file(COPY "${QT_PREFIX}/translations/qtwebengine_locales" DESTINATION "${BUNDLE}/Contents/Resources/translations")

set(QT_CONF "${BUNDLE}/Contents/Resources/qt.conf")
if (EXISTS "${QT_CONF}")
    file(READ "${QT_CONF}" QT_CONF_CONTENTS)
else ()
    set(QT_CONF_CONTENTS "[Paths]\n")
endif ()
if (NOT QT_CONF_CONTENTS MATCHES "LibraryExecutables")
    file(APPEND "${QT_CONF}" "LibraryExecutables = MacOS\nData = Resources\nTranslations = Resources/translations\n")
endif ()
