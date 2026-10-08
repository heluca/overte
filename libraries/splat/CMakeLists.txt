# Copyright 2026 Overte e.V.
# SPDX-License-Identifier: Apache-2.0

# Client-only: entities-renderer and interface link this, nothing server-side does.
set(TARGET_NAME splat)
setup_hifi_library(Network Concurrent)
link_hifi_libraries(shared networking gpu)
target_tbb()
