#
#  Copyright 2026 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#  SPDX-License-Identifier: Apache-2.0
#

# Qt Bluetooth is not part of every Qt build (Debian and Ubuntu ship it as qt6-connectivity-dev, Homebrew as
# qtconnectivity), so the plugin is built only where it is found.
find_package(Qt6 COMPONENTS Bluetooth QUIET)
if (Qt6Bluetooth_FOUND)
    set(TARGET_NAME fitshowRower)
    setup_hifi_plugin(Bluetooth)
    link_hifi_libraries(shared controllers ui plugins input-plugins)
    include_hifi_library_headers(script-engine)
else()
    message(STATUS "Qt Bluetooth not found: not building the FitShow rower plugin")
endif()
