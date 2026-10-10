//
//  FetchClass.h
//  libraries/script-engine/src/
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

/// @addtogroup ScriptEngine
/// @{

#ifndef overte_FetchClass_h
#define overte_FetchClass_h

#include <QByteArray>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QUrl>

#include "ScriptEngine.h"

class QNetworkRequest;
class ResourceRequest;

/*@jsdoc
 * Fetches a resource, the same as the WHATWG <code>fetch()</code>. <code>http:</code> and <code>https:</code> URLs take
 * any method; <code>file:</code>, <code>qrc:</code> and <code>atp:</code> URLs take <code>GET</code> and
 * <code>HEAD</code>. A relative URL is resolved the same as {@link Script.resolvePath}.
 * <p>The promise is rejected with a <code>TypeError</code> when there is no response at all: the host is not found, the
 * connection is refused, the scheme is not supported, or the file or asset is missing. An HTTP error status such as
 * <code>404</code> still resolves, with <code>response.ok</code> set to <code>false</code>. Aborting through
 * <code>init.signal</code> rejects with the signal's reason, by default an <code>Error</code> named
 * <code>"AbortError"</code>.</p>
 * @function fetch
 * @param {Request|string} input - The URL, or a request to send.
 * @param {Request.Init} [init] - Request settings, overriding those of <code>input</code>.
 * @returns {Promise<Response>} The response, once its headers and the whole body have arrived.
 * @example <caption>Get JSON from a web service.</caption>
 * fetch("https://example.com/api/status")
 *     .then(function (response) {
 *         if (!response.ok) {
 *             throw new Error("HTTP " + response.status);
 *         }
 *         return response.json();
 *     })
 *     .then(function (status) {
 *         print("Status:", JSON.stringify(status));
 *     })
 *     .catch(function (error) {
 *         print("Failed:", error.name, error.message);
 *     });
 */

/*@jsdoc
 * A set of HTTP headers, the same as the WHATWG <code>Headers</code>. Names are case-insensitive and are reported in lower
 * case, in sorted order.
 * <p>Create using <code>new Headers(...)</code>.</p>
 * @class Headers
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {Headers|Object<string,string>|Array<Array<string>>} [init] - The initial headers: another set, an object of
 *     names and values, or an array of <code>[name, value]</code> pairs.
 */
/*@jsdoc
 * Adds a value to a header, after any value it already has.
 * @function Headers.append
 * @param {string} name - The header name.
 * @param {string} value - The value to add.
 */
/*@jsdoc
 * Removes a header.
 * @function Headers.delete
 * @param {string} name - The header name.
 */
/*@jsdoc
 * Gets a header's value. Several values are joined with <code>", "</code>.
 * @function Headers.get
 * @param {string} name - The header name.
 * @returns {string|null} The value, or <code>null</code> if the header is not set.
 */
/*@jsdoc
 * Checks whether a header is set.
 * @function Headers.has
 * @param {string} name - The header name.
 * @returns {boolean} <code>true</code> if the header is set.
 */
/*@jsdoc
 * Sets a header, replacing any value it had.
 * @function Headers.set
 * @param {string} name - The header name.
 * @param {string} value - The value.
 */
/*@jsdoc
 * Calls a function for each header.
 * @function Headers.forEach
 * @param {Headers~forEachCallback} callback - The function to call.
 * @param {object} [thisArg] - The <code>this</code> of each call.
 */
/*@jsdoc
 * Called by {@link Headers.forEach} for each header.
 * @callback Headers~forEachCallback
 * @param {string} value - The header value.
 * @param {string} name - The header name, in lower case.
 * @param {Headers} headers - The headers being iterated.
 */
/*@jsdoc
 * Gets the <code>[name, value]</code> pairs, sorted by name. The WHATWG API returns an iterator; an array iterates the same
 * way, and a <code>Headers</code> object is itself iterable over the same pairs.
 * @function Headers.entries
 * @returns {Array<Array<string>>} The pairs.
 */
/*@jsdoc
 * Gets the header names, sorted.
 * @function Headers.keys
 * @returns {Array<string>} The names, in lower case.
 */
/*@jsdoc
 * Gets the header values, in the order of their sorted names.
 * @function Headers.values
 * @returns {Array<string>} The values.
 */

/*@jsdoc
 * A request for {@link fetch}, the same as the WHATWG <code>Request</code> but with only its URL, method and headers.
 * <p>Create using <code>new Request(...)</code>.</p>
 * @class Request
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {Request|string} input - The URL, or a request to copy.
 * @param {Request.Init} [init] - Settings, overriding those of <code>input</code>.
 * @property {string} url - The absolute URL. <em>Read-only.</em>
 * @property {string} method - The method, such as <code>"GET"</code>. <em>Read-only.</em>
 * @property {Headers} headers - The request headers. <em>Read-only.</em>
 */
/*@jsdoc
 * Settings for a {@link Request} or a {@link fetch} call.
 * @typedef {object} Request.Init
 * @property {string} [method="GET"] - The method. Methods other than <code>GET</code> and <code>HEAD</code> only work
 *     for <code>http:</code> and <code>https:</code> URLs.
 * @property {Headers|Object<string,string>|Array<Array<string>>} [headers] - The request headers.
 * @property {string|ArrayBuffer|ArrayBufferView} [body] - The request body. A string is sent as UTF-8, with a
 *     <code>Content-Type</code> of <code>text/plain;charset=UTF-8</code> unless one is set. Not allowed for
 *     <code>GET</code> and <code>HEAD</code>.
 * @property {AbortSignal|null} [signal] - A signal that aborts the request.
 */

/*@jsdoc
 * The response to a {@link fetch} call, the same as the WHATWG <code>Response</code>. The body has already arrived in full;
 * it can be read once, by one of {@link Response.text}, {@link Response.json} or {@link Response.arrayBuffer}.
 * <p>Create using <code>new Response(...)</code>, or get one from {@link fetch}.</p>
 * @class Response
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {string|ArrayBuffer|ArrayBufferView|null} [body=null] - The body.
 * @param {Response.Init} [init] - The status and headers.
 * @property {number} status - The HTTP status code. <code>200</code> for a <code>file:</code>, <code>qrc:</code> or
 *     <code>atp:</code> URL. <em>Read-only.</em>
 * @property {string} statusText - The HTTP status message. <em>Read-only.</em>
 * @property {boolean} ok - <code>true</code> if the status is in the range <code>200</code> &ndash; <code>299</code>.
 *     <em>Read-only.</em>
 * @property {string} url - The URL of the response, after any redirects. <em>Read-only.</em>
 * @property {boolean} redirected - <code>true</code> if the request was redirected. <em>Read-only.</em>
 * @property {Headers} headers - The response headers. <em>Read-only.</em>
 * @property {boolean} bodyUsed - <code>true</code> once the body has been read. <em>Read-only.</em>
 */
/*@jsdoc
 * Settings for a {@link Response} made by a script.
 * @typedef {object} Response.Init
 * @property {number} [status=200] - The status, in the range <code>200</code> &ndash; <code>599</code>.
 * @property {string} [statusText=""] - The status message.
 * @property {Headers|Object<string,string>|Array<Array<string>>} [headers] - The headers.
 */
/*@jsdoc
 * Reads the body as UTF-8 text.
 * @function Response.text
 * @returns {Promise<string>} The text. Rejected with a <code>TypeError</code> if the body was already read.
 */
/*@jsdoc
 * Reads the body as JSON.
 * @function Response.json
 * @returns {Promise<*>} The parsed value. Rejected with a <code>SyntaxError</code> if the body is not valid JSON, or a
 *     <code>TypeError</code> if the body was already read.
 */
/*@jsdoc
 * Reads the body as bytes.
 * @function Response.arrayBuffer
 * @returns {Promise<ArrayBuffer>} The bytes. Rejected with a <code>TypeError</code> if the body was already read.
 */

/*@jsdoc
 * Aborts {@link fetch} calls through its {@link AbortSignal}, the same as the WHATWG <code>AbortController</code>.
 * <p>Create using <code>new AbortController()</code>.</p>
 * @class AbortController
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @property {AbortSignal} signal - The signal to pass as <code>init.signal</code>. <em>Read-only.</em>
 */
/*@jsdoc
 * Aborts the signal. Does nothing if it was already aborted.
 * @function AbortController.abort
 * @param {*} [reason] - The reason. The default is an <code>Error</code> named <code>"AbortError"</code>.
 */

/*@jsdoc
 * Tells a {@link fetch} call to abort; get one from {@link AbortController}.
 * @class AbortSignal
 * @hideconstructor
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @property {boolean} aborted - <code>true</code> once aborted. <em>Read-only.</em>
 * @property {*} reason - Why it was aborted, or <code>undefined</code>. <em>Read-only.</em>
 * @property {AbortSignal~abortCallback|null} onabort - Called when the signal is aborted.
 */
/*@jsdoc
 * Adds a function to call when the signal is aborted.
 * @function AbortSignal.addEventListener
 * @param {string} type - <code>"abort"</code>; other types are ignored.
 * @param {AbortSignal~abortCallback} listener - The function to call.
 */
/*@jsdoc
 * Removes a function added by {@link AbortSignal.addEventListener}.
 * @function AbortSignal.removeEventListener
 * @param {string} type - <code>"abort"</code>.
 * @param {AbortSignal~abortCallback} listener - The function to remove.
 */
/*@jsdoc
 * Throws the signal's reason if it has been aborted.
 * @function AbortSignal.throwIfAborted
 */
/*@jsdoc
 * Called when an {@link AbortSignal} is aborted.
 * @callback AbortSignal~abortCallback
 * @param {object} event - <code>{ type: "abort", target: signal }</code>.
 */

/// One fetch() request in flight. http(s) goes through the script thread's QNetworkAccessManager, the same as
/// XMLHttpRequest; any other scheme goes through ResourceManager. Settles its promise on the script thread.
class FetchTransfer : public QObject {
    Q_OBJECT
public:
    FetchTransfer(ScriptEngine* engine, const ScriptPromiseResolverPointer& resolver, const QUrl& url, bool isHead);
    ~FetchTransfer() override;

    void startHttp(const QNetworkRequest& request, const QByteArray& method, const QByteArray& body);
    void startResource(ResourceRequest* request);

    /// Stops the request and leaves the promise pending; fetch() has already rejected its own promise
    Q_INVOKABLE void abort();

private:
    void httpFinished();
    void resourceFinished();
    void resolve(int status, const QString& statusText, const QUrl& url, bool redirected,
                 const QList<QPair<QByteArray, QByteArray>>& headers, const QByteArray& body);
    void reject(const QString& message);
    void cancel();

    ScriptEngine* _engine;
    ScriptPromiseResolverPointer _resolver;
    QUrl _url;
    bool _isHead;
    QPointer<QNetworkReply> _reply;
    QPointer<ResourceRequest> _resourceRequest;
};

/// Adds fetch, Headers, Request, Response, AbortController and AbortSignal to the engine's global object
void registerFetchGlobals(ScriptEngine* engine);

/// The script half of fetch(), in FetchPrelude.cpp: a function that takes the native transport and returns the globals
extern const char FETCH_PRELUDE[];

#endif // overte_FetchClass_h

/// @}
