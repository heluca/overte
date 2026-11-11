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
#  A framework build of Qt (Homebrew's) keeps the helper and data inside QtWebEngineCore.framework, which
#  macdeployqt deploys as a whole; there is no QtWebEngineProcess in the libexec directory then, and this
#  script does nothing.
#
#  Arguments: -DBUNDLE=<path to the .app> and Qt's install directories (QT6_INSTALL_PREFIX joined with
#  QT6_INSTALL_LIBEXECS, QT6_INSTALL_DATA and QT6_INSTALL_TRANSLATIONS): -DQT_LIBEXECS=<dir> -DQT_DATA=<dir>
#  -DQT_TRANSLATIONS=<dir>
#

if (NOT BUNDLE OR NOT QT_LIBEXECS OR NOT QT_DATA OR NOT QT_TRANSLATIONS)
    message(FATAL_ERROR "MacDeployQtWebEngine.cmake needs -DBUNDLE, -DQT_LIBEXECS, -DQT_DATA and -DQT_TRANSLATIONS")
endif ()

set(WEBENGINE_PROCESS "${QT_LIBEXECS}/QtWebEngineProcess")
if (NOT EXISTS "${WEBENGINE_PROCESS}")
    message(STATUS "No ${WEBENGINE_PROCESS}; assuming a framework build of Qt that macdeployqt handled itself")
    return()
endif ()

message(STATUS "Deploying QtWebEngine helper process and data into ${BUNDLE}")
file(COPY "${WEBENGINE_PROCESS}" DESTINATION "${BUNDLE}/Contents/MacOS")
file(COPY "${QT_DATA}/resources" DESTINATION "${BUNDLE}/Contents/Resources")
file(COPY "${QT_TRANSLATIONS}/qtwebengine_locales" DESTINATION "${BUNDLE}/Contents/Resources/translations")

set(QT_CONF "${BUNDLE}/Contents/Resources/qt.conf")
if (EXISTS "${QT_CONF}")
    file(READ "${QT_CONF}" QT_CONF_CONTENTS)
else ()
    set(QT_CONF_CONTENTS "[Paths]\n")
endif ()
if (NOT QT_CONF_CONTENTS MATCHES "LibraryExecutables")
    file(APPEND "${QT_CONF}" "LibraryExecutables = MacOS\nData = Resources\nTranslations = Resources/translations\n")
endif ()
