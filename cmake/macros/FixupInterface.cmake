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

        find_program(MACDEPLOYQT_COMMAND macdeployqt PATHS "${QT_DIR}/bin" NO_DEFAULT_PATH)

        if (NOT MACDEPLOYQT_COMMAND AND (PRODUCTION_BUILD OR PR_BUILD))
            message(FATAL_ERROR "Could not find macdeployqt at ${QT_DIR}/bin.\
                It is required to produce an relocatable interface application.\
                Check that the variable QT_DIR points to your Qt installation.\
            ")
        endif ()

        # The Conan Qt is a non-framework build, and macdeployqt then leaves out QtWebEngine's helper
        # process and data; cmake/MacDeployQtWebEngine.cmake adds them after macdeployqt has run.
        set(_QT_PREFIX "${QT_DIR}")
        if (NOT _QT_PREFIX)
            get_filename_component(_QT_PREFIX "${MACDEPLOYQT_COMMAND}" DIRECTORY)
            get_filename_component(_QT_PREFIX "${_QT_PREFIX}" DIRECTORY)
        endif ()
        set(_WEBENGINE_DEPLOY_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/MacDeployQtWebEngine.cmake")

        if (OVERTE_RELEASE_TYPE STREQUAL "DEV")
            install(CODE "
                execute_process(COMMAND ${MACDEPLOYQT_COMMAND}\
                    \${CMAKE_INSTALL_PREFIX}/${_INTERFACE_INSTALL_PATH}/\
                    -verbose=2 -qmldir=${CMAKE_SOURCE_DIR}/interface/resources/qml/\
                )
                execute_process(COMMAND ${CMAKE_COMMAND}\
                    -DBUNDLE=\${CMAKE_INSTALL_PREFIX}/${_INTERFACE_INSTALL_PATH}\
                    -DQT_PREFIX=${_QT_PREFIX}\
                    -P ${_WEBENGINE_DEPLOY_SCRIPT}\
                )"
                COMPONENT ${CLIENT_COMPONENT}
            )
        else ()
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${MACDEPLOYQT_COMMAND} "$<TARGET_FILE_DIR:${TARGET_NAME}>/../.." -verbose=2 -qmldir=${CMAKE_SOURCE_DIR}/interface/resources/qml/
                COMMAND ${CMAKE_COMMAND} -DBUNDLE="$<TARGET_FILE_DIR:${TARGET_NAME}>/../.." -DQT_PREFIX=${_QT_PREFIX} -P ${_WEBENGINE_DEPLOY_SCRIPT}
            )
        endif()
    endif ()
endmacro()
