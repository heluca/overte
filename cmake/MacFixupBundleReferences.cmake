#
#  MacFixupBundleReferences.cmake
#  cmake
#
#  Copyright 2026 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#  SPDX-License-Identifier: Apache-2.0
#
#  Run with cmake -P after macdeployqt (and MacDeployQtWebEngine.cmake). macdeployqt leaves references to the
#  build machine in the bundle, and dyld follows them whenever the path exists, loading a second copy of a library
#  from outside the bundle:
#  - The libraries it copies from the build tree keep their build rpaths (the build tree, the Conan cache, the
#    Qt prefix); so does the executable for the extra INSTALL_RPATH entries it needed to find them.
#  - A Qt installed by Homebrew has its inter-framework references rewritten to absolute paths in the Homebrew
#    prefix. macdeployqt fixes them in the frameworks it deploys, but not in the QtWebEngineProcess helper it copies
#    inside QtWebEngineCore.framework, nor the install names of frameworks it only found through QML plugins.
#  - It points the references it does fix at @executable_path/../Frameworks. That only holds for the main
#    executable: QtWebEngineProcess, a framework build's helper inside QtWebEngineCore.framework, then fails to load
#    QtCore's dependencies and every web view stays empty.
#  This script removes every absolute rpath, points every reference to a library in Contents/Frameworks (absolute
#  or through @executable_path) at that copy through @rpath, adding an @loader_path rpath to Contents/Frameworks
#  where one is missing, gives those libraries @rpath install names, and re-signs the bundle ad hoc, as the edits
#  invalidate the signatures macdeployqt made. References to libraries that are neither in the bundle nor in the
#  system are reported as warnings.
#
#  Arguments: -DBUNDLE=<path to the .app>
#

if (NOT BUNDLE OR NOT IS_DIRECTORY "${BUNDLE}/Contents")
    message(FATAL_ERROR "MacFixupBundleReferences.cmake needs -DBUNDLE=<path to an .app>")
endif ()

set(FRAMEWORKS_DIR "${BUNDLE}/Contents/Frameworks")
set(SYSTEM_PREFIXES "/System/" "/usr/lib/")

function(is_system_path PATH OUT)
    set(${OUT} FALSE PARENT_SCOPE)
    foreach (PREFIX ${SYSTEM_PREFIXES})
        string(FIND "${PATH}" "${PREFIX}" POS)
        if (POS EQUAL 0)
            set(${OUT} TRUE PARENT_SCOPE)
        endif ()
    endforeach ()
endfunction()

# The path of a library inside Contents/Frameworks that stands for the absolute reference PATH, or "" if there is
# none: "QtGui.framework/Versions/A/QtGui" for a framework, the file name for a plain library.
function(bundled_library_for PATH OUT)
    set(${OUT} "" PARENT_SCOPE)
    if (PATH MATCHES "([^/]+\\.framework/.*)$")
        set(CANDIDATE "${CMAKE_MATCH_1}")
    else ()
        get_filename_component(CANDIDATE "${PATH}" NAME)
    endif ()
    if (EXISTS "${FRAMEWORKS_DIR}/${CANDIDATE}")
        set(${OUT} "${CANDIDATE}" PARENT_SCOPE)
    endif ()
endfunction()

file(GLOB_RECURSE BUNDLE_FILES LIST_DIRECTORIES false "${BUNDLE}/Contents/*")
set(CHANGED_FILES 0)
foreach (FILE ${BUNDLE_FILES})
    if (IS_SYMLINK "${FILE}" OR FILE MATCHES "/_CodeSignature/|\\.(qml|js|json|png|svg|qmlc|plist|pak|dat|qm|txt)$")
        continue()
    endif ()
    execute_process(COMMAND otool -l "${FILE}" OUTPUT_VARIABLE LOAD_COMMANDS ERROR_QUIET RESULT_VARIABLE OTOOL_RESULT)
    if (NOT OTOOL_RESULT EQUAL 0 OR NOT LOAD_COMMANDS MATCHES "Load command")
        continue()
    endif ()

    # Collect the install name, the dependencies and the rpaths from the load commands.
    set(INSTALL_NAME "")
    set(DEPENDENCIES "")
    set(RPATHS "")
    string(REGEX MATCHALL "cmd LC_[A-Z_]+\n[^\n]*\n *(name|path) [^\n]+ \\(offset" ENTRIES "${LOAD_COMMANDS}")
    foreach (ENTRY ${ENTRIES})
        string(REGEX REPLACE "^cmd (LC_[A-Z_]+)\n.*$" "\\1" LOAD_COMMAND "${ENTRY}")
        string(REGEX REPLACE "^.*(name|path) (.+) \\(offset$" "\\2" VALUE "${ENTRY}")
        if (LOAD_COMMAND STREQUAL "LC_ID_DYLIB")
            set(INSTALL_NAME "${VALUE}")
        elseif (LOAD_COMMAND MATCHES "^LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB$")
            list(APPEND DEPENDENCIES "${VALUE}")
        elseif (LOAD_COMMAND STREQUAL "LC_RPATH")
            list(APPEND RPATHS "${VALUE}")
        endif ()
    endforeach ()

    file(RELATIVE_PATH FILE_IN_BUNDLE "${BUNDLE}" "${FILE}")
    get_filename_component(FILE_DIR "${FILE}" DIRECTORY)
    file(RELATIVE_PATH FRAMEWORKS_FROM_FILE "${FILE_DIR}" "${FRAMEWORKS_DIR}")
    set(FRAMEWORKS_RPATH "@loader_path/${FRAMEWORKS_FROM_FILE}")
    set(ARGS "")
    set(NEEDS_FRAMEWORKS_RPATH FALSE)

    foreach (RPATH ${RPATHS})
        if (NOT RPATH MATCHES "^@")
            list(APPEND ARGS -delete_rpath "${RPATH}")
        endif ()
    endforeach ()

    foreach (DEPENDENCY ${DEPENDENCIES})
        if (DEPENDENCY MATCHES "^@executable_path/\\.\\./Frameworks/(.*)$")
            set(BUNDLED "${CMAKE_MATCH_1}")
            if (EXISTS "${FRAMEWORKS_DIR}/${BUNDLED}")
                list(APPEND ARGS -change "${DEPENDENCY}" "@rpath/${BUNDLED}")
                set(NEEDS_FRAMEWORKS_RPATH TRUE)
            else ()
                message(WARNING "${FILE_IN_BUNDLE} depends on ${DEPENDENCY}, which is not in the bundle")
            endif ()
            continue()
        elseif (DEPENDENCY MATCHES "^@loader_path/(.*)$")
            if (NOT EXISTS "${FILE_DIR}/${CMAKE_MATCH_1}")
                message(WARNING "${FILE_IN_BUNDLE} depends on ${DEPENDENCY}, which is not in the bundle")
            endif ()
            continue()
        elseif (DEPENDENCY MATCHES "^@")
            continue()
        endif ()
        is_system_path("${DEPENDENCY}" IS_SYSTEM)
        if (IS_SYSTEM)
            continue()
        endif ()
        bundled_library_for("${DEPENDENCY}" BUNDLED)
        if (BUNDLED)
            list(APPEND ARGS -change "${DEPENDENCY}" "@rpath/${BUNDLED}")
            set(NEEDS_FRAMEWORKS_RPATH TRUE)
        else ()
            message(WARNING "${FILE_IN_BUNDLE} depends on ${DEPENDENCY}, which is not in the bundle")
        endif ()
    endforeach ()

    if (INSTALL_NAME AND NOT INSTALL_NAME MATCHES "^@(rpath|loader_path)/")
        file(RELATIVE_PATH FILE_IN_FRAMEWORKS "${FRAMEWORKS_DIR}" "${FILE}")
        if (NOT FILE_IN_FRAMEWORKS MATCHES "^\\.\\.")
            list(APPEND ARGS -id "@rpath/${FILE_IN_FRAMEWORKS}")
        endif ()
    endif ()

    if (NEEDS_FRAMEWORKS_RPATH)
        # Any existing @ rpath may or may not lead to Contents/Frameworks; add the one that does if it is not there.
        set(HAS_FRAMEWORKS_RPATH FALSE)
        foreach (RPATH ${RPATHS})
            string(REGEX REPLACE "/$" "" RPATH_TRIMMED "${RPATH}")
            if (RPATH_TRIMMED STREQUAL FRAMEWORKS_RPATH)
                set(HAS_FRAMEWORKS_RPATH TRUE)
            elseif (RPATH MATCHES "^@(loader|executable)_path/(.*)$")
                if (CMAKE_MATCH_1 STREQUAL "loader")
                    get_filename_component(RPATH_DIR "${FILE_DIR}/${CMAKE_MATCH_2}" REALPATH)
                else ()
                    get_filename_component(RPATH_DIR "${BUNDLE}/Contents/MacOS/${CMAKE_MATCH_2}" REALPATH)
                endif ()
                get_filename_component(REAL_FRAMEWORKS_DIR "${FRAMEWORKS_DIR}" REALPATH)
                if (RPATH_DIR STREQUAL REAL_FRAMEWORKS_DIR)
                    set(HAS_FRAMEWORKS_RPATH TRUE)
                endif ()
            endif ()
        endforeach ()
        if (NOT HAS_FRAMEWORKS_RPATH)
            list(APPEND ARGS -add_rpath "${FRAMEWORKS_RPATH}")
        endif ()
    endif ()

    if (ARGS)
        execute_process(COMMAND install_name_tool ${ARGS} "${FILE}" RESULT_VARIABLE INT_RESULT ERROR_VARIABLE INT_ERROR)
        if (NOT INT_RESULT EQUAL 0)
            message(FATAL_ERROR "install_name_tool failed on ${FILE_IN_BUNDLE}: ${INT_ERROR}")
        endif ()
        math(EXPR CHANGED_FILES "${CHANGED_FILES} + 1")
    endif ()
endforeach ()

message(STATUS "Fixed up the library references of ${CHANGED_FILES} files in ${BUNDLE}")
if (CHANGED_FILES GREATER 0)
    execute_process(COMMAND codesign --force --deep --sign - "${BUNDLE}" RESULT_VARIABLE SIGN_RESULT)
    if (NOT SIGN_RESULT EQUAL 0)
        message(FATAL_ERROR "Ad hoc signing ${BUNDLE} failed")
    endif ()
endif ()
