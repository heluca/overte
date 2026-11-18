//
//  FitShowRowerProvider.cpp
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include <mutex>

#include <QtCore/QObject>
#include <QtCore/QtPlugin>

#include <plugins/InputPlugin.h>
#include <plugins/RuntimePlugin.h>

#include "FitShowRowerPlugin.h"

class FitShowRowerProvider : public QObject, public InputProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID InputProvider_iid FILE "plugin.json")
    Q_INTERFACES(InputProvider)

public:
    FitShowRowerProvider(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~FitShowRowerProvider() {}

    virtual InputPluginList getInputPlugins() override {
        static std::once_flag once;
        std::call_once(once, [&] {
            InputPluginPointer plugin(std::make_shared<FitShowRowerPlugin>());
            if (plugin->isSupported()) {
                _inputPlugins.push_back(plugin);
            }
        });
        return _inputPlugins;
    }

    virtual void destroyInputPlugins() override {
        _inputPlugins.clear();
    }

private:
    InputPluginList _inputPlugins;
};

#include "FitShowRowerProvider.moc"
