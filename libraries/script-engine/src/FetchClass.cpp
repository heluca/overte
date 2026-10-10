//
//  FetchClass.cpp
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "FetchClass.h"

#include <QNetworkRequest>

#include <AccountManager.h>
#include <DependencyManager.h>
#include <MetaverseAPI.h>
#include <NetworkAccessManager.h>
#include <NetworkingConstants.h>
#include <ResourceManager.h>
#include <ResourceRequest.h>

#include "ResourceRequestObserver.h"
#include "ScriptContext.h"
#include "ScriptEngineLogging.h"
#include "ScriptManager.h"
#include "ScriptValue.h"
#include "v8/FastScriptValueUtils.h"

static ScriptValue throwTypeError(ScriptContext* context, ScriptEngine* engine, const QString& message) {
    return context->throwValue(engine->makeError(engine->newValue(message), "TypeError"));
}

static QByteArray bodyBytes(const ScriptValue& body) {
    QByteArray bytes;
    if (body.isString()) {
        bytes = body.toString().toUtf8();
    } else if (body.isObject()) {
        qBytearrayFromScriptValue(body, bytes);
    }
    return bytes;
}

static QNetworkRequest httpRequest(const QUrl& url, const ScriptValue& headerPairs) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, NetworkingConstants::OVERTE_USER_AGENT);

    // The same metaverse token XMLHttpRequest.open() adds
    const QString METAVERSE_API_URL = MetaverseAPI::getCurrentMetaverseServerURL().toString() + "/api/";
    if (url.toString().toLower().startsWith(METAVERSE_API_URL)) {
        auto accountManager = DependencyManager::get<AccountManager>();
        if (accountManager->hasValidAccessToken()) {
            QString bearer = "Bearer " + accountManager->getAccountInfo().getAccessToken().token;
            request.setRawHeader("Authorization", bearer.toLocal8Bit());
        }
    }

    const quint32 headerCount = headerPairs.property("length").toUInt32();
    for (quint32 i = 0; i < headerCount; i++) {
        ScriptValue pair = headerPairs.property(i);
        request.setRawHeader(pair.property(0u).toString().toLatin1(), pair.property(1u).toString().toLatin1());
    }
    return request;
}

// native.start(method, url, [[name, value], ...], body) -> { promise, control }. Throws a TypeError for what can never
// succeed, which the Promise executor in fetch() turns into a rejection.
static ScriptValue startTransfer(ScriptContext* context, ScriptEngine* engine, bool allowLocalFiles) {
    const QByteArray method = context->argument(0).toString().toLatin1();
    const QUrl url(context->argument(1).toString());
    const bool isHead = method == "HEAD";
    const bool isHttp = url.scheme() == "http" || url.scheme() == "https";
    if (!isHttp && method != "GET" && !isHead) {
        return throwTypeError(context, engine, "fetch: " + method + " is only supported for http and https URLs");
    }
    if (!isHttp && !allowLocalFiles) {
        // After normalisation, since ResourceManager rewrites unknown schemes and URL prefix overrides to local paths
        const QString scheme = DependencyManager::get<ResourceManager>()->normalizeURL(url).scheme();
        if (scheme == HIFI_URL_SCHEME_FILE || scheme == URL_SCHEME_QRC) {
            return throwTypeError(context, engine, "fetch: " + url.toString() +
                ": file: and qrc: URLs are only available to Interface and agent scripts, not entity scripts");
        }
    }

    ScriptPromise promise = engine->newPromise();
    auto transfer = new FetchTransfer(engine, promise.resolver, url, isHead);
    if (isHttp) {
        transfer->startHttp(httpRequest(url, context->argument(2)), method, bodyBytes(context->argument(3)));
    } else {
        ResourceRequest* request = DependencyManager::get<ResourceManager>()->createResourceRequest(
            transfer, url, ResourceRequest::IS_OBSERVABLE, -1, "fetch");
        if (!request) {
            delete transfer;
            return throwTypeError(context, engine, "fetch: unsupported URL: " + url.toString());
        }
        transfer->startResource(request);
    }
    ScriptValue result = engine->newObject();
    result.setProperty("promise", promise.promise);
    result.setProperty("control", engine->newQObject(transfer, ScriptEngine::QtOwnership));
    return result;
}

static ScriptValue startWithLocalFiles(ScriptContext* context, ScriptEngine* engine) {
    return startTransfer(context, engine, true);
}

static ScriptValue startWithoutLocalFiles(ScriptContext* context, ScriptEngine* engine) {
    return startTransfer(context, engine, false);
}

// native.abort(control): the proxy holds the transfer weakly, so this does nothing once the transfer is gone
static ScriptValue abortTransfer(ScriptContext* context, ScriptEngine* engine) {
    if (auto transfer = qobject_cast<FetchTransfer*>(context->argument(0).toQObject())) {
        transfer->abort();
    }
    return engine->undefinedValue();
}

static ScriptValue resolveUrl(ScriptContext* context, ScriptEngine* engine) {
    const QString input = context->argument(0).toString();
    QUrl url(input);
    if (url.isRelative() && engine->manager()) {
        url = engine->manager()->resolvePath(input);
    }
    if (!url.isValid() || url.isRelative()) {
        return throwTypeError(context, engine, "fetch: invalid URL: " + input);
    }
    return engine->newValue(url.toString());
}

static ScriptValue decodeText(ScriptContext* context, ScriptEngine* engine) {
    QByteArray bytes;
    qBytearrayFromScriptValue(context->argument(0), bytes);
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }
    return engine->newValue(QString::fromUtf8(bytes));
}

static ScriptValue encodeText(ScriptContext* context, ScriptEngine* engine) {
    return engine->newArrayBuffer(context->argument(0).toString().toUtf8());
}

void registerFetchGlobals(ScriptEngine* engine, bool allowLocalFiles) {
    ScriptValue natives = engine->newObject();
    natives.setProperty("start", engine->newFunction(allowLocalFiles ? startWithLocalFiles : startWithoutLocalFiles, 4));
    natives.setProperty("abort", engine->newFunction(abortTransfer, 1));
    natives.setProperty("resolveUrl", engine->newFunction(resolveUrl, 1));
    natives.setProperty("decodeText", engine->newFunction(decodeText, 1));
    natives.setProperty("encodeText", engine->newFunction(encodeText, 1));

    ScriptValue install = engine->evaluate(fetchPrelude(), "(fetch)");
    if (!install.isFunction()) {
        qCCritical(scriptengine) << "fetch prelude did not evaluate to a function, fetch() is unavailable:" << install.toString();
        return;
    }
    ScriptValue globals = install.call(engine->undefinedValue(), ScriptValueList({ natives }));
    for (const char* name : { "fetch", "Headers", "Request", "Response", "AbortController", "AbortSignal" }) {
        engine->globalObject().setProperty(name, globals.property(name));
    }
}

FetchTransfer::FetchTransfer(ScriptEngine* engine, const ScriptPromiseResolverPointer& resolver, const QUrl& url,
                             bool isHead) :
    QObject(engine),
    _engine(engine),
    _resolver(resolver),
    _url(url),
    _isHead(isHead) {
    // Like timers, a request does not outlive its script
    if (auto manager = engine->manager()) {
        connect(manager, &ScriptManager::scriptEnding, this, &FetchTransfer::abort);
    }
}

FetchTransfer::~FetchTransfer() {
    cancel();
}

void FetchTransfer::startHttp(const QNetworkRequest& request, const QByteArray& method, const QByteArray& body) {
    DependencyManager::get<ResourceRequestObserver>()->update(_url, -1, "fetch");
    auto& networkAccessManager = NetworkAccessManager::getInstance();
    if (method == "GET") {
        _reply = networkAccessManager.get(request);
    } else if (_isHead) {
        _reply = networkAccessManager.head(request);
    } else {
        _reply = networkAccessManager.sendCustomRequest(request, method, body);
    }
    connect(_reply, &QNetworkReply::finished, this, &FetchTransfer::httpFinished);
}

void FetchTransfer::startResource(ResourceRequest* request) {
    _resourceRequest = request;
    // The request lives on the ResourceManager thread, so this is a queued connection back to the script thread
    connect(request, &ResourceRequest::finished, this, &FetchTransfer::resourceFinished);
    request->send();
}

void FetchTransfer::abort() {
    cancel();
    deleteLater();
}

// A queued finished() can still arrive after cancel(), before the deferred delete
void FetchTransfer::httpFinished() {
    if (!_resolver) {
        return;
    }
    const QNetworkReply::NetworkError error = _reply->error();
    const QVariant status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    // Qt turns HTTP error statuses into errors of several ranges (400 and 418 are ProtocolInvalidOperationError, 407 is
    // ProxyAuthenticationRequiredError), so any status is a response; only the connection range (1-99, which includes
    // too many and insecure redirects) means there is none
    const bool isConnectionError = error >= QNetworkReply::ConnectionRefusedError && error <= QNetworkReply::UnknownNetworkError;
    if (!status.isValid() || isConnectionError) {
        reject("fetch: " + _url.toString() + ": " + _reply->errorString());
        return;
    }
    resolve(status.toInt(), _reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(), _reply->url(),
            _reply->url() != _url, _reply->rawHeaderPairs(), _isHead ? QByteArray() : _reply->readAll());
}

void FetchTransfer::resourceFinished() {
    if (!_resolver) {
        return;
    }
    if (_resourceRequest->getResult() != ResourceRequest::Success) {
        reject("fetch: " + _url.toString() + ": " + _resourceRequest->getResultString());
        return;
    }
    QList<QPair<QByteArray, QByteArray>> headers;
    if (!_resourceRequest->getWebMediaType().isEmpty()) {
        headers.append({ "content-type", _resourceRequest->getWebMediaType().toLatin1() });
    }
    resolve(200, "OK", _url, false, headers, _isHead ? QByteArray() : _resourceRequest->getData());
}

void FetchTransfer::resolve(int status, const QString& statusText, const QUrl& url, bool redirected,
                            const QList<QPair<QByteArray, QByteArray>>& headers, const QByteArray& body) {
    auto scopeGuard = _engine->getScopeGuard();
    {
        ScriptValue headerList = _engine->newArray((uint)headers.size());
        for (quint32 i = 0; i < (quint32)headers.size(); i++) {
            ScriptValue pair = _engine->newArray(2);
            pair.setProperty(0u, _engine->newValue(QString::fromLatin1(headers[i].first)));
            pair.setProperty(1u, _engine->newValue(QString::fromLatin1(headers[i].second)));
            headerList.setProperty(i, pair);
        }
        ScriptValue record = _engine->newObject();
        record.setProperty("status", _engine->newValue(status));
        record.setProperty("statusText", _engine->newValue(statusText));
        record.setProperty("url", _engine->newValue(url.toString()));
        record.setProperty("redirected", _engine->newValue(redirected));
        record.setProperty("headers", headerList);
        record.setProperty("body", _engine->newArrayBuffer(body));
        auto resolver = std::move(_resolver);
        abort();
        resolver->resolve(record);
    }
}

void FetchTransfer::reject(const QString& message) {
    auto resolver = std::move(_resolver);
    abort();
    resolver->reject(message);
}

void FetchTransfer::cancel() {
    _resolver.reset();
    if (_reply) {
        disconnect(_reply, nullptr, this, nullptr);
        if (_reply->isRunning()) {
            _reply->abort();
        }
        _reply->deleteLater();
        _reply = nullptr;
    }
    if (_resourceRequest) {
        disconnect(_resourceRequest, nullptr, this, nullptr);
        _resourceRequest->deleteLater();
        _resourceRequest = nullptr;
    }
}
