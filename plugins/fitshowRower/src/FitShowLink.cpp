//
//  FitShowLink.cpp
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "FitShowLink.h"

#include <algorithm>

#include <QtBluetooth/QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/QBluetoothUuid>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>

#include "FitShowRowerLogging.h"

namespace {

// The console answers only when polled. Fast enough to see a stroke within a fraction of its drive.
const int POLL_INTERVAL_MS = 250;
const int SCAN_TIMEOUT_MS = 10000;
const int MIN_RESCAN_DELAY_MS = 1000;
const int MAX_RESCAN_DELAY_MS = 30000;
const int LINK_SETUP_TIMEOUT_MS = 20000;
const int REPLY_TIMEOUT_MS = 5000;
const int DISCONNECT_TIMEOUT_MS = 2000;

// FitShow: commands are written without response to FFF2, replies are notified on FFF1.
// Frame: 0x02 | command | data | XOR of command and data | 0x03.
const QBluetoothUuid SERVICE_UUID { quint16(0xFFF0) };
const QBluetoothUuid NOTIFY_UUID { quint16(0xFFF1) };
const QBluetoothUuid WRITE_UUID { quint16(0xFFF2) };
const QString CONSOLE_NAME_PREFIX = "FS-";
const char FRAME_START = 0x02;
const char FRAME_END = 0x03;
const char SPORT_DATA_COMMAND = 0x43;
const char SPORT_DATA_SUBCOMMAND = 0x01;
// Reply: 43 01 <elapsed s> <distance, top bit a flag> <unknown> <strokes>, each u16 little-endian
const int SPORT_DATA_REPLY_SIZE = 10;
const quint16 DISTANCE_MASK = 0x7FFF;

char computeChecksum(const QByteArray& body) {
    char checksum = 0;
    for (char byte : body) {
        checksum ^= byte;
    }
    return checksum;
}

QByteArray makeFrame(const QByteArray& body) {
    return FRAME_START + body + computeChecksum(body) + FRAME_END;
}

const QByteArray SPORT_DATA_QUERY = makeFrame(QByteArray().append(SPORT_DATA_COMMAND).append(SPORT_DATA_SUBCOMMAND));

quint16 readUint16(const QByteArray& bytes, int offset) {
    return (quint16)((quint8)bytes[offset] | ((quint8)bytes[offset + 1] << 8));
}

} // namespace

FitShowLink::FitShowLink() : QObject(nullptr) {
}

void FitShowLink::start() {
    _isRunning = true;
    _numFailedScans = 0;
    if (!_pollTimer) {
        _pollTimer = new QTimer(this);
        connect(_pollTimer, &QTimer::timeout, this, &FitShowLink::poll);
        _scanTimer = new QTimer(this);
        _scanTimer->setSingleShot(true);
        connect(_scanTimer, &QTimer::timeout, this, &FitShowLink::scan);
        _replyTimer = new QTimer(this);
        _replyTimer->setSingleShot(true);
        connect(_replyTimer, &QTimer::timeout, this, [this] { drop("the console stopped answering"); });
    }
    scan();
}

void FitShowLink::stop() {
    _isRunning = false;
    if (_scanTimer) {
        _scanTimer->stop();
    }
    if (_discoveryAgent && _discoveryAgent->isActive()) {
        _discoveryAgent->stop();
    }
    if (_controller && _controller->state() != QLowEnergyController::UnconnectedState) {
        _controller->disconnect(this);
        _pollTimer->stop();
        _replyTimer->stop();
        QEventLoop loop;
        connect(_controller, &QLowEnergyController::disconnected, &loop, &QEventLoop::quit);
        connect(_controller, &QLowEnergyController::errorOccurred, &loop, &QEventLoop::quit);
        QTimer::singleShot(DISCONNECT_TIMEOUT_MS, &loop, &QEventLoop::quit);
        _controller->disconnectFromDevice();
        if (_controller->state() != QLowEnergyController::UnconnectedState) {
            loop.exec();
        }
        qCInfo(fitshowRower) << "Disconnected from" << _consoleName;
    }
    closeConnection();
}

void FitShowLink::scan() {
    if (!_isRunning || _controller) {
        return;
    }
    if (!_discoveryAgent) {
        _discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
        _discoveryAgent->setLowEnergyDiscoveryTimeout(SCAN_TIMEOUT_MS);
        connect(_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &FitShowLink::onDeviceDiscovered);
        connect(_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &FitShowLink::onScanFinished);
        connect(_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this] {
            // A missing or powered-off adapter fails every scan; say so once, not on every retry
            if (_numFailedScans == 0) {
                qCWarning(fitshowRower) << "Bluetooth scan failed:" << _discoveryAgent->errorString();
            }
            onScanFinished();
        });
    }
    if (_numFailedScans == 0) {
        qCInfo(fitshowRower) << "Looking for a FitShow console";
    }
    _discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void FitShowLink::scheduleScan() {
    if (!_isRunning) {
        return;
    }
    int delay = std::min(MIN_RESCAN_DELAY_MS << std::min(_numFailedScans, 5), MAX_RESCAN_DELAY_MS);
    _scanTimer->start(delay);
}

void FitShowLink::onDeviceDiscovered(const QBluetoothDeviceInfo& info) {
    if (_controller || !(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) {
        return;
    }
    if (info.name().startsWith(CONSOLE_NAME_PREFIX) || info.serviceUuids().contains(SERVICE_UUID)) {
        _discoveryAgent->stop();
        connectToConsole(info);
    }
}

void FitShowLink::onScanFinished() {
    if (!_controller) {
        _numFailedScans++;
        scheduleScan();
    }
}

void FitShowLink::connectToConsole(const QBluetoothDeviceInfo& info) {
    _consoleName = info.name();
    qCInfo(fitshowRower) << "Connecting to" << _consoleName;
    _controller = QLowEnergyController::createCentral(info, this);
    connect(_controller, &QLowEnergyController::connected, _controller, &QLowEnergyController::discoverServices);
    connect(_controller, &QLowEnergyController::discoveryFinished, this, &FitShowLink::onServicesDiscovered);
    connect(_controller, &QLowEnergyController::disconnected, this, [this] { drop("disconnected"); });
    connect(_controller, &QLowEnergyController::errorOccurred, this, [this] { drop(_controller->errorString()); });
    _replyTimer->start(LINK_SETUP_TIMEOUT_MS);
    _controller->connectToDevice();
}

void FitShowLink::onServicesDiscovered() {
    _service = _controller->createServiceObject(SERVICE_UUID, this);
    if (!_service) {
        drop("it has no FitShow service");
        return;
    }
    connect(_service, &QLowEnergyService::stateChanged, this, &FitShowLink::onServiceStateChanged);
    connect(_service, &QLowEnergyService::characteristicChanged, this, &FitShowLink::onCharacteristicChanged);
    connect(_service, &QLowEnergyService::descriptorWritten, this, &FitShowLink::startPolling);
    connect(_service, &QLowEnergyService::errorOccurred, this, [this] { drop("service error"); });
    _service->discoverDetails();
}

void FitShowLink::onServiceStateChanged(QLowEnergyService::ServiceState state) {
    if (state != QLowEnergyService::RemoteServiceDiscovered) {
        return;
    }
    QLowEnergyCharacteristic notify = _service->characteristic(NOTIFY_UUID);
    if (!notify.isValid() || !_service->characteristic(WRITE_UUID).isValid()) {
        drop("its FitShow service lacks FFF1 or FFF2");
        return;
    }
    QLowEnergyDescriptor notifyConfig = notify.clientCharacteristicConfiguration();
    if (notifyConfig.isValid()) {
        _service->writeDescriptor(notifyConfig, QLowEnergyCharacteristic::CCCDEnableNotification);
    } else {
        startPolling();
    }
}

void FitShowLink::startPolling() {
    if (_pollTimer->isActive()) {
        return;
    }
    qCInfo(fitshowRower) << "Connected to" << _consoleName;
    _replyTimer->start(REPLY_TIMEOUT_MS);
    _pollTimer->start(POLL_INTERVAL_MS);
    poll();
}

void FitShowLink::poll() {
    _service->writeCharacteristic(_service->characteristic(WRITE_UUID), SPORT_DATA_QUERY,
                                  QLowEnergyService::WriteWithoutResponse);
}

void FitShowLink::onCharacteristicChanged(const QLowEnergyCharacteristic& characteristic, const QByteArray& value) {
    if (characteristic.uuid() != NOTIFY_UUID || value.size() < 4 || value.front() != FRAME_START ||
        value.back() != FRAME_END) {
        return;
    }
    QByteArray body = value.mid(1, value.size() - 3);
    if (computeChecksum(body) != value[value.size() - 2] || body.size() != SPORT_DATA_REPLY_SIZE ||
        body[0] != SPORT_DATA_COMMAND || body[1] != SPORT_DATA_SUBCOMMAND) {
        return;
    }
    _replyTimer->start(REPLY_TIMEOUT_MS);
    _numFailedScans = 0;
    emit readingReceived(readUint16(body, 4) & DISTANCE_MASK, readUint16(body, 8));
}

void FitShowLink::drop(const QString& reason) {
    if (!_controller) {
        return;
    }
    qCInfo(fitshowRower) << "Lost" << _consoleName << ":" << reason;
    closeConnection();
    emit linkLost();
    // Counted as a failure, so a console that is found but refuses the connection is retried with backoff too
    _numFailedScans++;
    scheduleScan();
}

void FitShowLink::closeConnection() {
    if (_pollTimer) {
        _pollTimer->stop();
        _replyTimer->stop();
    }
    if (_service) {
        _service->disconnect(this);
        _service->deleteLater();
        _service = nullptr;
    }
    if (_controller) {
        _controller->disconnect(this);
        if (_controller->state() != QLowEnergyController::UnconnectedState) {
            _controller->disconnectFromDevice();
        }
        _controller->deleteLater();
        _controller = nullptr;
    }
}
