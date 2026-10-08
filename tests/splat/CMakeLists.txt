# Copyright 2026 Overte e.V.
# SPDX-License-Identifier: Apache-2.0

# Declare dependencies
macro (setup_testcase_dependencies)
  # link in the shared libraries
  link_hifi_libraries(shared splat)

  package_libraries_for_deployment()
endmacro ()

setup_hifi_testcase()
