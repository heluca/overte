#
#  FindMacDeployQt.cmake
#  cmake/macros
#
#  Copyright 2026 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#  SPDX-License-Identifier: Apache-2.0
#

# Sets MACDEPLOYQT_COMMAND to the macdeployqt of the Qt the project builds against.
# Qt 6 exports it as the Qt6::macdeployqt imported target (from Qt6CoreTools, loaded with Qt6::Core); that works for
# framework and non-framework builds alike, wherever Qt is installed. Fall back to the bin directory of the Qt prefix.
macro(find_macdeployqt)
  if (NOT TARGET Qt6::macdeployqt)
    find_package(Qt6 COMPONENTS Core CoreTools QUIET)
  endif ()
  if (TARGET Qt6::macdeployqt)
    get_target_property(MACDEPLOYQT_COMMAND Qt6::macdeployqt LOCATION)
  else ()
    find_program(MACDEPLOYQT_COMMAND NAMES macdeployqt6 macdeployqt
      HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/bin" "${QT_DIR}/bin"
    )
  endif ()
endmacro()
