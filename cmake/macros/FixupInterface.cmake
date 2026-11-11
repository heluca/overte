#
#  FixupInterface.cmake
#  cmake/macros
#
#  Copyright 2016 High Fidelity, Inc.
#  Copyright 2025 Overte e.V.
#  Created by Stephen Birarda on January 6th, 2016
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#

macro(fixup_interface)
    if (APPLE)
        string(REPLACE " " "\\ " ESCAPED_BUNDLE_NAME ${INTERFACE_BUNDLE_NAME})
        string(REPLACE " " "\\ " ESCAPED_INSTALL_PATH ${INTERFACE_INSTALL_DIR})
        set(_INTERFACE_INSTALL_PATH "${ESCAPED_INSTALL_PATH}/${ESCAPED_BUNDLE_NAME}.app")

        find_macdeployqt()

        if (NOT MACDEPLOYQT_COMMAND AND (PRODUCTION_BUILD OR PR_BUILD))
            message(FATAL_ERROR "Could not find Qt's macdeployqt.\
                It is required to produce an relocatable interface application.\
            ")
        endif ()

        # A non-framework Qt (the Conan one) makes macdeployqt leave out QtWebEngine's helper process and data;
        # cmake/MacDeployQtWebEngine.cmake adds them after macdeployqt has run. With a framework Qt (Homebrew's)
        # macdeployqt deploys them from QtWebEngineCore.framework itself and the script finds nothing to do.
        if (QT6_INSTALL_PREFIX)
            set(_QT_LIBEXECS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}")
            set(_QT_DATA "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_DATA}")
            set(_QT_TRANSLATIONS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
        else ()
            get_filename_component(_QT_PREFIX "${MACDEPLOYQT_COMMAND}" DIRECTORY)
            get_filename_component(_QT_PREFIX "${_QT_PREFIX}" DIRECTORY)
            set(_QT_LIBEXECS "${_QT_PREFIX}/libexec")
            set(_QT_DATA "${_QT_PREFIX}")
            set(_QT_TRANSLATIONS "${_QT_PREFIX}/translations")
        endif ()
        set(_WEBENGINE_DEPLOY_ARGS -DQT_LIBEXECS=${_QT_LIBEXECS} -DQT_DATA=${_QT_DATA} -DQT_TRANSLATIONS=${_QT_TRANSLATIONS})
        string(REPLACE ";" " " _WEBENGINE_DEPLOY_ARGS_STRING "${_WEBENGINE_DEPLOY_ARGS}")
        set(_WEBENGINE_DEPLOY_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/MacDeployQtWebEngine.cmake")

        if (OVERTE_RELEASE_TYPE STREQUAL "DEV")
            install(CODE "
                execute_process(COMMAND ${MACDEPLOYQT_COMMAND}\
                    \${CMAKE_INSTALL_PREFIX}/${_INTERFACE_INSTALL_PATH}/\
                    -verbose=2 -qmldir=${CMAKE_SOURCE_DIR}/interface/resources/qml/\
                )
                execute_process(COMMAND ${CMAKE_COMMAND}\
                    -DBUNDLE=\${CMAKE_INSTALL_PREFIX}/${_INTERFACE_INSTALL_PATH}\
                    ${_WEBENGINE_DEPLOY_ARGS_STRING}\
                    -P ${_WEBENGINE_DEPLOY_SCRIPT}\
                )"
                COMPONENT ${CLIENT_COMPONENT}
            )
        else ()
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${MACDEPLOYQT_COMMAND} "$<TARGET_FILE_DIR:${TARGET_NAME}>/../.." -verbose=2 -qmldir=${CMAKE_SOURCE_DIR}/interface/resources/qml/
                COMMAND ${CMAKE_COMMAND} -DBUNDLE="$<TARGET_FILE_DIR:${TARGET_NAME}>/../.." ${_WEBENGINE_DEPLOY_ARGS} -P ${_WEBENGINE_DEPLOY_SCRIPT}
            )
        endif()
    endif ()
endmacro()
