//
//  FitShowLink.h
//  plugins/fitshowRower/src
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef hifi_FitShowLink_h
#define hifi_FitShowLink_h

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyService>
#include <QtCore/QObject>

class QBluetoothDeviceDiscoveryAgent;
class QTimer;

// A Bluetooth LE link to a FitShow console: finds the first one advertising, connects, polls its sport data and
// reports each reading. The console answers only when polled and sleeps when idle, which drops the link; the link
// then scans again, backing off while none is found.
// Lives on its own thread: create it, move it there, then call start() and stop() through that thread's event loop.
class FitShowLink : public QObject {
    Q_OBJECT
public:
    FitShowLink();

public slots:
    void start();
    // Disconnects and waits for the console to confirm it, so it advertises again for other apps.
    void stop();

signals:
    // Emitted on the link's thread. Distance is the raw 15-bit counter, in the console's own unit.
    void readingReceived(quint16 distance, quint16 strokes);
    void linkLost();

private:
    void scan();
    void scheduleScan();
    void onDeviceDiscovered(const QBluetoothDeviceInfo& info);
    void onScanFinished();
    void connectToConsole(const QBluetoothDeviceInfo& info);
    void onServicesDiscovered();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& characteristic, const QByteArray& value);
    void startPolling();
    void poll();
    void drop(const QString& reason);
    void closeConnection();

    QBluetoothDeviceDiscoveryAgent* _discoveryAgent { nullptr };
    QLowEnergyController* _controller { nullptr };
    QLowEnergyService* _service { nullptr };
    QTimer* _pollTimer { nullptr };
    QTimer* _scanTimer { nullptr };
    QTimer* _replyTimer { nullptr };
    QString _consoleName;
    int _numFailedScans { 0 };
    bool _isRunning { false };
};

#endif // hifi_FitShowLink_h
