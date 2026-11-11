//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "linkedVulkanLibrary.h"

#include <dlfcn.h>

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>
#include <QtCore/QDebug>

void useLinkedVulkanLibraryForQt() {
    if (qEnvironmentVariableIsSet("QT_VULKAN_LIB")) {
        qInfo() << "Qt loads the Vulkan library named by QT_VULKAN_LIB:" << qgetenv("QT_VULKAN_LIB");
        return;
    }
    void* symbol = dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr");
    Dl_info info{};
    if (!symbol || !dladdr(symbol, &info) || !info.dli_fname) {
        qWarning() << "Cannot find the linked Vulkan library; QVulkanInstance will look for its own";
        return;
    }
    qInfo() << "Qt loads the linked Vulkan library" << info.dli_fname;
    qputenv("QT_VULKAN_LIB", QByteArray(info.dli_fname));
}
