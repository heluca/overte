//
//  XMLHttpRequestClass.cpp
//  libraries/script-engine/src/
//
//  Created by Ryan Huffman on 5/2/14.
//  Copyright (c) 2014 High Fidelity, Inc. All rights reserved.
//  Copyright 2023 Overte e.V.
//
//  This class is an implementation of the XMLHttpRequest object for scripting use.  It provides a near-complete implementation
//  of the class described in the Mozilla docs: https://developer.mozilla.org/en-US/docs/Web/API/XMLHttpRequest
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "XMLHttpRequestClass.h"

#include <QEventLoop>
#include <qurlquery.h>

#include <AccountManager.h>
#include <NetworkAccessManager.h>
#include <NetworkingConstants.h>
#include <MetaverseAPI.h>
#include <ScriptCredentials.h>

#include "ResourceRequestObserver.h"
#include "ScriptContext.h"
#include "ScriptEngine.h"
#include "ScriptEngineCast.h"
#include "ScriptEnvironment.h"
#include "ScriptManager.h"
#include "ScriptValue.h"

Q_DECLARE_METATYPE(QByteArray*)

XMLHttpRequestClass::XMLHttpRequestClass(ScriptEngine* engine) :
    _engine(engine),
    _onTimeout(engine->nullValue()),
    _onReadyStateChange(engine->nullValue()),
    _timer(this) {

    _request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    _timer.setSingleShot(true);
}

XMLHttpRequestClass::~XMLHttpRequestClass() {
    if (_reply) { _reply->deleteLater(); }
    Q_ASSERT(_engine);
    {
        auto guard = _engine->getScopeGuard();
        _responseData = ScriptValue();
        _onTimeout = ScriptValue();
        _onReadyStateChange = ScriptValue();
    }
}

ScriptValue XMLHttpRequestClass::constructor(ScriptContext* context, ScriptEngine* engine) {
    return engine->newQObject(new XMLHttpRequestClass(engine), ScriptEngine::ScriptOwnership);
}

QByteArray XMLHttpRequestClass::metaverseAuthorization(ScriptEngine* engine, const QUrl& url) {
    ScriptManager* manager = engine->manager();
    if (!manager || !(manager->isClientScript() || manager->isAgentScript())) {
        return QByteArray();
    }
    const QString METAVERSE_API_URL = MetaverseAPI::getCurrentMetaverseServerURL().toString() + "/api/";
    if (!url.toString().toLower().startsWith(METAVERSE_API_URL)) {
        return QByteArray();
    }
    auto accountManager = DependencyManager::get<AccountManager>();
    if (!accountManager->hasValidAccessToken()) {
        return QByteArray();
    }
    return ("Bearer " + accountManager->getAccountInfo().getAccessToken().token).toLocal8Bit();
}

bool XMLHttpRequestClass::isSameOrigin(const QUrl& a, const QUrl& b) {
    const int defaultPort = a.scheme() == "https" ? 443 : 80;
    return a.scheme() == b.scheme() && a.host() == b.host() && a.port(defaultPort) == b.port(defaultPort);
}

ScriptValue XMLHttpRequestClass::getStatus() const {
    if (_reply) {
        return _engine->newValue(_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    } 
    return ScriptValue(_engine->newValue(0));
}

QString XMLHttpRequestClass::getStatusText() const {
    if (_reply) {
        return _reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    }
    return "";
}

void XMLHttpRequestClass::abort() {
    abortRequest();
}

void XMLHttpRequestClass::setRequestHeader(const QString& name, const QString& value) {
    _request.setHeader(QNetworkRequest::UserAgentHeader, NetworkingConstants::OVERTE_USER_AGENT);
    _request.setRawHeader(QByteArray(name.toLatin1()), QByteArray(value.toLatin1()));
}

void XMLHttpRequestClass::requestDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (_readyState == OPENED && bytesReceived > 0) {
        setReadyState(HEADERS_RECEIVED);
        setReadyState(LOADING);
    }
}

ScriptValue XMLHttpRequestClass::getAllResponseHeaders() const {
    if (_reply) {
        QList<QNetworkReply::RawHeaderPair> headerList = _reply->rawHeaderPairs();
        QByteArray headers;
        for (int i = 0; i < headerList.size(); i++) {
            headers.append(headerList[i].first);
            headers.append(": ");
            headers.append(headerList[i].second);
            headers.append("\n");
        }
        return _engine->newValue(QString(headers.data()));
    }
    return _engine->newValue("");
}

ScriptValue XMLHttpRequestClass::getResponseHeader(const QString& name) const {
    if (_reply && _reply->hasRawHeader(name.toLatin1())) {
        return _engine->newValue(QString(_reply->rawHeader(name.toLatin1())));
    }
    return _engine->nullValue();
}

void XMLHttpRequestClass::setOnTimeout(const ScriptValue& function) {
    _onTimeout = function;
    _onTimeoutEnvironment = _engine->currentEnvironment();
}

void XMLHttpRequestClass::setOnReadyStateChange(const ScriptValue& function) {
    _onReadyStateChange = function;
    _onReadyStateChangeEnvironment = _engine->currentEnvironment();
}

/*@jsdoc
 * Called when the request's ready state changes.
 * @callback XMLHttpRequest~onReadyStateChangeCallback
 */
void XMLHttpRequestClass::setReadyState(ReadyState readyState) {
    if (readyState != _readyState) {
        _readyState = readyState;
        if (_onReadyStateChange.isFunction()) {
            ScriptEnvironmentScope environmentScope(_engine, _onReadyStateChangeEnvironment);
            _onReadyStateChange.call(_onReadyStateChange.engine()->nullValue());
        }
    }
}

void XMLHttpRequestClass::open(const QString& method, const QString& url, bool async, const QString& username,
                               const QString& password) {
    if (_readyState == UNSENT) {
        _method = method;
        _url.setUrl(url);
        _async = async;

        const QByteArray metaverseToken = metaverseAuthorization(_engine, _url);
        if (!metaverseToken.isEmpty()) {
            _request.setRawHeader("Authorization", metaverseToken);
        }

        if (auto manager = _engine->manager()) {
            _credentialScope = manager->scriptCredentialScope();
        }

        if (!username.isEmpty()) {
            _url.setUserName(username);
        }
        if (!password.isEmpty()) {
            _url.setPassword(password);
        }
        _request.setUrl(_url);
        setReadyState(OPENED);
    }
}

void XMLHttpRequestClass::send() {
    send(_engine->nullValue());
}

void XMLHttpRequestClass::send(const ScriptValue& data) {
    if (_readyState == OPENED && !_reply) {

        if (!data.isNull()) {
            if (data.isObject()) {
                _sendData = scriptvalue_cast<QByteArray>(data);
            } else {
                _sendData = data.toString().toUtf8();
            }
        }

        // After the metaverse token and the script's own header, either of which wins
        if (!_request.hasRawHeader("Authorization")) {
            _authorization = ScriptCredentials::find(_request.url(), _credentialScope);
            if (!_authorization.isEmpty()) {
                _request.setRawHeader("Authorization", _authorization);
            }
        }

        doSend();

        if (!_async) {
            QEventLoop loop;
            connect(this, SIGNAL(requestComplete()), &loop, SLOT(quit()));
            loop.exec();
        }
    }
}

void XMLHttpRequestClass::doSend() {
    // Qt sends every request header on to a redirect target; requestRedirected() decides instead
    _request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, _request.hasRawHeader("Authorization")
        ? QNetworkRequest::UserVerifiedRedirectPolicy : QNetworkRequest::NoLessSafeRedirectPolicy);

    DependencyManager::get<ResourceRequestObserver>()->update(_url, -1, "XMLHttpRequestClass::doSend");
    _reply = NetworkAccessManager::getInstance().sendCustomRequest(_request, _method.toLatin1(), _sendData);
    connectToReply(_reply);

    if (_timeout > 0) {
        _timer.start(_timeout);
        connect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));
    }
}

/*@jsdoc
 * Called when the request times out.
 * @callback XMLHttpRequest~onTimeoutCallback 
 */
void XMLHttpRequestClass::requestTimeout() {
    if (_onTimeout.isFunction()) {
        ScriptEnvironmentScope environmentScope(_engine, _onTimeoutEnvironment);
        _onTimeout.call(_engine->nullValue());
    }
    abortRequest();
    _errorCode = QNetworkReply::TimeoutError;
    setReadyState(DONE);
    emit requestComplete();
}

void XMLHttpRequestClass::requestError(QNetworkReply::NetworkError code) {
}

// Only a request with an Authorization header waits for this. A script credential follows only while the same credential
// applies at the target, which also refuses https to http, as no https prefix covers http. Any other header, the
// metaverse token or the script's own, follows within the origin and is dropped beyond it, as browsers do.
void XMLHttpRequestClass::requestRedirected(const QUrl& target) {
    if (!_request.hasRawHeader("Authorization")) {
        return;
    }
    if (_authorization.isEmpty() ? isSameOrigin(_request.url(), target)
                                 : ScriptCredentials::find(target, _credentialScope) == _authorization) {
        emit _reply->redirectAllowed();
        return;
    }
    if (_authorization.isEmpty() && !(_request.url().scheme() == "https" && target.scheme() == "http")) {
        const int status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        abortRequest();
        _request.setUrl(target);
        _request.setRawHeader("Authorization", QByteArray());
        // As Qt follows a redirect
        if (status != 307 && status != 308 && _method.toUpper() != "HEAD") {
            _method = "GET";
            _sendData.clear();
            _request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
            _request.setHeader(QNetworkRequest::ContentLengthHeader, QVariant());
        }
        doSend();
        return;
    }
    abortRequest();
    _errorCode = QNetworkReply::InsecureRedirectError;
    setReadyState(DONE);
    emit requestComplete();
}

void XMLHttpRequestClass::requestFinished() {
    disconnect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));

    _errorCode = _reply->error();

    if (_errorCode == QNetworkReply::NoError) {
        _rawResponseData.append(_reply->readAll());
        auto scopeGuard = _engine->getScopeGuard();
        if (_responseType == "json") {
            // V8TODO: V8 JSON parser needs to be used instead
            // V8TODO: make sure this always happens on script engine thread on script engines that have a script manager
            _responseData = _engine->evaluate("(" + QString(_rawResponseData.data()) + ")");
            if (_responseData.isError()) {
                _engine->clearExceptions();
                _responseData = _engine->nullValue();
            }
        } else if (_responseType == "arraybuffer") {
            _responseData = _engine->newArrayBuffer(_rawResponseData);
        } else {
            _responseData = _engine->newValue(QString(_rawResponseData.data()));
        }
    }

    setReadyState(DONE);
    emit requestComplete();

    disconnectFromReply(_reply);
    _reply->deleteLater();
    _reply = nullptr;
}

void XMLHttpRequestClass::abortRequest() {
    // Disconnect from signals we don't want to receive any longer.
    disconnect(&_timer, SIGNAL(timeout()), this, SLOT(requestTimeout()));
    if (_reply) {
        disconnectFromReply(_reply);
        _reply->abort();
        _reply->deleteLater();
        _reply = nullptr;
    }
}

void XMLHttpRequestClass::connectToReply(QNetworkReply* reply) {
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(requestError(QNetworkReply::NetworkError)));
    connect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(requestDownloadProgress(qint64, qint64)));
    connect(reply, SIGNAL(redirected(const QUrl&)), this, SLOT(requestRedirected(const QUrl&)));
}

void XMLHttpRequestClass::disconnectFromReply(QNetworkReply* reply) {
    disconnect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    disconnect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(requestError(QNetworkReply::NetworkError)));
    disconnect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(requestDownloadProgress(qint64, qint64)));
    disconnect(reply, SIGNAL(redirected(const QUrl&)), this, SLOT(requestRedirected(const QUrl&)));
}
