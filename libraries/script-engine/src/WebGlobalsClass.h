//
//  WebGlobalsClass.h
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

#ifndef overte_WebGlobalsClass_h
#define overte_WebGlobalsClass_h

#include <QString>

#include "ScriptEngine.h"

/*@jsdoc
 * Encodes strings as UTF-8, the same as the WHATWG <code>TextEncoder</code>. A lone surrogate is encoded as U+FFFD.
 * <p>In a module loaded with {@link Script.require}, which runs in a script context of its own, the arrays it returns
 * come from the main script context, so <code>instanceof Uint8Array</code> is <code>false</code> there: use
 * <code>ArrayBuffer.isView()</code> instead. Entity scripts run in the main context and are not affected.</p>
 * <p>Create using <code>new TextEncoder()</code>.</p>
 * @class TextEncoder
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @property {string} encoding - <code>"utf-8"</code>. <em>Read-only.</em>
 */
/*@jsdoc
 * Encodes a string.
 * @function TextEncoder.encode
 * @param {string} [input=""] - The string.
 * @returns {Uint8Array} The UTF-8 bytes.
 */
/*@jsdoc
 * Encodes as much of a string as fits into an array, without splitting a character.
 * @function TextEncoder.encodeInto
 * @param {string} source - The string.
 * @param {Uint8Array} destination - Where to write the bytes.
 * @returns {TextEncoder.EncodeIntoResult} How much was read and written.
 */
/*@jsdoc
 * What {@link TextEncoder.encodeInto} read and wrote.
 * @typedef {object} TextEncoder.EncodeIntoResult
 * @property {number} read - The number of UTF-16 code units of the string that were encoded.
 * @property {number} written - The number of bytes written.
 */

/*@jsdoc
 * Decodes UTF-8 bytes into a string, the same as the WHATWG <code>TextDecoder</code>, but for UTF-8 only. Each invalid
 * sequence becomes U+FFFD, the same as in a browser, unless <code>fatal</code> is set.
 * <p>Create using <code>new TextDecoder(...)</code>.</p>
 * @class TextDecoder
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {string} [label="utf-8"] - The encoding: <code>"utf-8"</code> or one of its other labels, such as
 *     <code>"utf8"</code>. Any other encoding throws a <code>RangeError</code>.
 * @param {TextDecoder.Options} [options] - How to decode.
 * @property {string} encoding - <code>"utf-8"</code>. <em>Read-only.</em>
 * @property {boolean} fatal - <code>true</code> if invalid bytes throw a <code>TypeError</code>. <em>Read-only.</em>
 * @property {boolean} ignoreBOM - <code>true</code> if a leading byte order mark is kept. <em>Read-only.</em>
 */
/*@jsdoc
 * How a {@link TextDecoder} decodes.
 * @typedef {object} TextDecoder.Options
 * @property {boolean} [fatal=false] - <code>true</code> to throw a <code>TypeError</code> on invalid bytes instead of
 *     replacing them with U+FFFD.
 * @property {boolean} [ignoreBOM=false] - <code>true</code> to keep a leading byte order mark in the text.
 */
/*@jsdoc
 * Decodes bytes. With <code>stream</code>, a character split across calls is decoded once the rest of it arrives.
 * @function TextDecoder.decode
 * @param {ArrayBuffer|ArrayBufferView} [input] - The bytes.
 * @param {TextDecoder.DecodeOptions} [options] - Whether more bytes follow.
 * @returns {string} The text.
 */
/*@jsdoc
 * Options for {@link TextDecoder.decode}.
 * @typedef {object} TextDecoder.DecodeOptions
 * @property {boolean} [stream=false] - <code>true</code> if more bytes follow in another call.
 */

/*@jsdoc
 * Decodes base64 into a binary string, the same as the WHATWG <code>atob()</code>: each character of the result is one
 * byte, from U+0000 to U+00FF. Whitespace is ignored and padding is optional.
 * <p>{@link Script.atob} differs: it returns an <code>ArrayBuffer</code>.</p>
 * @function atob
 * @param {string} data - The base64 text.
 * @returns {string} The binary string.
 * @throws {DOMException} An <code>InvalidCharacterError</code> if the text is not base64.
 */
/*@jsdoc
 * Encodes a binary string as base64, the same as the WHATWG <code>btoa()</code>. Each character must be from U+0000 to
 * U+00FF; to encode other text, encode it with {@link TextEncoder} first.
 * <p>{@link Script.btoa} differs: it encodes an <code>ArrayBuffer</code>.</p>
 * @function btoa
 * @param {string} data - The binary string.
 * @returns {string} The base64 text.
 * @throws {DOMException} An <code>InvalidCharacterError</code> if a character is outside U+0000 &ndash; U+00FF.
 */

/*@jsdoc
 * Cryptographic random numbers and hashes, the same as the WHATWG <code>crypto</code>.
 * @namespace crypto
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 */
/*@jsdoc
 * Fills an integer typed array with random values from the system's cryptographic random number generator.
 * @function crypto.getRandomValues
 * @param {ArrayBufferView} array - An <code>Int8Array</code>, <code>Uint8Array</code>, <code>Uint8ClampedArray</code>,
 *     <code>Int16Array</code>, <code>Uint16Array</code>, <code>Int32Array</code>, <code>Uint32Array</code>,
 *     <code>BigInt64Array</code> or <code>BigUint64Array</code> of at most 65536 bytes.
 * @returns {ArrayBufferView} The same array.
 * @throws {DOMException} A <code>TypeMismatchError</code> for another kind of array, or a
 *     <code>QuotaExceededError</code> if the array is over 65536 bytes.
 */
/*@jsdoc
 * Makes a random (version 4) UUID. Unlike {@link Uuid.generate}, it has no braces.
 * @function crypto.randomUUID
 * @returns {string} The UUID, such as <code>"3b241101-e2bb-4255-8caf-4136c566a962"</code>.
 */
/*@jsdoc
 * The part of the WHATWG <code>SubtleCrypto</code> that hashes data. There are no keys, so no encryption or signing.
 * @namespace crypto.subtle
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 */
/*@jsdoc
 * Hashes data.
 * <p>In a module loaded with {@link Script.require} the hash is an <code>ArrayBuffer</code> of the main script context,
 * so <code>instanceof ArrayBuffer</code> is <code>false</code> there.</p>
 * @function crypto.subtle.digest
 * @param {string|crypto.subtle.Algorithm} algorithm - <code>"SHA-1"</code>, <code>"SHA-256"</code>,
 *     <code>"SHA-384"</code> or <code>"SHA-512"</code>, in any case, or an object with one of them as its name.
 * @param {ArrayBuffer|ArrayBufferView} data - The data. To hash a string, encode it with {@link TextEncoder} first.
 * @returns {Promise<ArrayBuffer>} The hash. Rejected with a <code>DOMException</code> named
 *     <code>"NotSupportedError"</code> for any other algorithm, or a <code>TypeError</code> if the data is not bytes.
 * @example <caption>Print the SHA-256 hash of a string in hex.</caption>
 * crypto.subtle.digest("SHA-256", new TextEncoder().encode("abc")).then(function (hash) {
 *     print(Array.from(new Uint8Array(hash), function (byte) {
 *         return byte.toString(16).padStart(2, "0");
 *     }).join(""));
 * });
 */
/*@jsdoc
 * A hash algorithm for {@link crypto.subtle.digest}.
 * @typedef {object} crypto.subtle.Algorithm
 * @property {string} name - <code>"SHA-1"</code>, <code>"SHA-256"</code>, <code>"SHA-384"</code> or
 *     <code>"SHA-512"</code>.
 */

/*@jsdoc
 * Makes a deep copy of a value, the same as the WHATWG <code>structuredClone()</code>: objects, arrays,
 * <code>Map</code>, <code>Set</code>, <code>Date</code>, <code>RegExp</code>, errors, <code>ArrayBuffer</code>s and typed
 * arrays are copied, with shared and circular references kept. Vec3, Quat and entity properties are plain objects and
 * copy too. Functions, symbols and Overte API objects such as <code>Script</code> throw.
 * <p>The copy is made in the main script context. A module loaded with {@link Script.require} runs in a script context
 * of its own, so there <code>instanceof Array</code>, <code>Date</code> and the like are <code>false</code> for the
 * copy, both while the module loads and in later callbacks: use <code>Array.isArray()</code> and
 * <code>Object.prototype.toString.call()</code> instead. Entity scripts run in the main context and are not
 * affected.</p>
 * @function structuredClone
 * @param {*} value - The value to copy.
 * @param {object} [options] - Options.
 * @param {Array<ArrayBuffer>} [options.transfer] - <code>ArrayBuffer</code>s to move into the copy instead of copying.
 *     Afterwards they are detached, with a <code>byteLength</code> of <code>0</code>.
 * @returns {*} The copy.
 * @throws {DOMException} A <code>DataCloneError</code> if the value or the transfer list cannot be copied.
 */

/*@jsdoc
 * A monotonic clock, the same as the WHATWG <code>performance</code> object but with only its clock.
 * @namespace performance
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @property {number} timeOrigin - When the script started, in ms since the Unix epoch. <em>Read-only.</em>
 */
/*@jsdoc
 * Gets the time since the script started, from a clock that is not changed by changes to the system time. It is
 * rounded down to 0.1 ms, as in a browser.
 * @function performance.now
 * @returns {number} The time, in ms.
 */
/*@jsdoc
 * Gets the clock's settings as an object, for <code>JSON.stringify()</code>.
 * @function performance.toJSON
 * @returns {object} <code>{ timeOrigin }</code>.
 */

/*@jsdoc
 * An error raised by a web API, the same as the WHATWG <code>DOMException</code>. It is an <code>Error</code>, with a name
 * that says what went wrong.
 * <p>Create using <code>new DOMException(...)</code>.</p>
 * @class DOMException
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * <p>In a module loaded with {@link Script.require} <code>instanceof DOMException</code> works, but
 * <code>instanceof Error</code> is <code>false</code>, since it extends the main script context's
 * <code>Error</code>.</p>
 * @param {string} [message=""] - The message.
 * @param {string} [name="Error"] - The name, such as <code>"DataCloneError"</code>.
 * @property {string} name - The name. <em>Read-only.</em>
 * @property {string} message - The message. <em>Read-only.</em>
 * @property {number} code - The legacy code of the name: <code>5</code> for <code>"InvalidCharacterError"</code>,
 *     <code>9</code> for <code>"NotSupportedError"</code>, <code>17</code> for <code>"TypeMismatchError"</code>,
 *     <code>22</code> for <code>"QuotaExceededError"</code>, <code>25</code> for <code>"DataCloneError"</code>, otherwise
 *     <code>0</code>. <em>Read-only.</em>
 */

/*@jsdoc
 * A parsed URL, the same as the WHATWG <code>URL</code> but built on Qt's URL parser, so a few unusual inputs come out
 * differently from a browser (see below). Setting a part reparses the whole URL; a value that would not parse is ignored,
 * except for <code>href</code>, which throws a <code>TypeError</code>.
 * <p>A relative URL needs a base, or it throws a <code>TypeError</code>. In a script use
 * <code>new URL(relative, Script.resolvePath(""))</code>, or <code>new URL(relative, import.meta.url)</code> in a
 * module.</p>
 * <p>Default ports, dot segments, IPv4 and IPv6 hosts, international host names, backslashes and missing slashes in
 * <code>http:</code> URLs and the like are handled as in a browser. Where the result differs:</p>
 * <ul>
 *     <li>A path that is not hierarchical, as in <code>data:</code> and <code>mailto:</code> URLs, has its spaces
 *     percent-encoded: <code>"data:text/plain,hello world"</code> becomes <code>"data:text/plain,hello%20world"</code>.
 *     </li>
 *     <li>The host of a scheme other than <code>http:</code>, <code>https:</code>, <code>ws:</code>, <code>wss:</code>,
 *     <code>ftp:</code> and <code>file:</code> is made lower case: <code>"foo://Host/x"</code> becomes
 *     <code>"foo://host/x"</code>.</li>
 *     <li>A <code>%</code> that does not start a percent-encoding is encoded: <code>"/a%zzb"</code> becomes
 *     <code>"/a%25zzb"</code>.</li>
 *     <li>A <code>|</code> in a path is percent-encoded as <code>%7C</code>.</li>
 * </ul>
 * <p>Create using <code>new URL(...)</code>.</p>
 * @class URL
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {string|URL} url - The URL, absolute or relative to <code>base</code>.
 * @param {string|URL} [base] - The absolute URL that a relative <code>url</code> is resolved against.
 * @property {string} href - The whole URL.
 * @property {string} origin - The scheme, host and port, such as <code>"https://example.com:8080"</code>, or
 *     <code>"null"</code> for a scheme without an origin, such as <code>file:</code>. <em>Read-only.</em>
 * @property {string} protocol - The scheme followed by <code>":"</code>, such as <code>"https:"</code>.
 * @property {string} username - The user name, percent-encoded.
 * @property {string} password - The password, percent-encoded.
 * @property {string} host - The host name and, if not the scheme's default, the port.
 * @property {string} hostname - The host name, lower case and with international names in punycode.
 * @property {string} port - The port, or <code>""</code> for the scheme's default.
 * @property {string} pathname - The path.
 * @property {string} search - The query with a leading <code>"?"</code>, or <code>""</code>.
 * @property {URLSearchParams} searchParams - The query's parameters. Editing them updates the URL. <em>Read-only.</em>
 * @property {string} hash - The fragment with a leading <code>"#"</code>, or <code>""</code>.
 */
/*@jsdoc
 * Checks whether a URL parses, without throwing.
 * @function URL.canParse
 * @static
 * @param {string|URL} url - The URL.
 * @param {string|URL} [base] - The base URL.
 * @returns {boolean} <code>true</code> if <code>new URL(url, base)</code> would succeed.
 */
/*@jsdoc
 * Gets the whole URL.
 * @function URL.toString
 * @returns {string} The same as <code>href</code>.
 */
/*@jsdoc
 * Gets the whole URL, for <code>JSON.stringify()</code>.
 * @function URL.toJSON
 * @returns {string} The same as <code>href</code>.
 */

/*@jsdoc
 * The parameters of a URL query, the same as the WHATWG <code>URLSearchParams</code>. They are kept in order, and written
 * in <code>application/x-www-form-urlencoded</code> form, with <code>+</code> for a space.
 * <p>Create using <code>new URLSearchParams(...)</code>, or get one from {@link URL}.</p>
 * @class URLSearchParams
 * @hifi-interface
 * @hifi-client-entity
 * @hifi-avatar
 * @hifi-server-entity
 * @hifi-assignment-client
 * @param {string|Object<string,string>|Array<Array<string>>|URLSearchParams} [init] - A query, with or without its
 *     leading <code>"?"</code>, an object of names and values, an array of <code>[name, value]</code> pairs, or other
 *     parameters to copy.
 * @property {number} size - The number of parameters. <em>Read-only.</em>
 */
/*@jsdoc
 * Adds a parameter after the others, even if one of the same name exists.
 * @function URLSearchParams.append
 * @param {string} name - The name.
 * @param {string} value - The value.
 */
/*@jsdoc
 * Removes every parameter of a name, or only those with a value too.
 * @function URLSearchParams.delete
 * @param {string} name - The name.
 * @param {string} [value] - The value.
 */
/*@jsdoc
 * Gets the first value of a parameter.
 * @function URLSearchParams.get
 * @param {string} name - The name.
 * @returns {string|null} The value, or <code>null</code> if there is no such parameter.
 */
/*@jsdoc
 * Gets every value of a parameter.
 * @function URLSearchParams.getAll
 * @param {string} name - The name.
 * @returns {Array<string>} The values, in order.
 */
/*@jsdoc
 * Checks whether there is a parameter of a name, or with a value too.
 * @function URLSearchParams.has
 * @param {string} name - The name.
 * @param {string} [value] - The value.
 * @returns {boolean} <code>true</code> if there is.
 */
/*@jsdoc
 * Sets a parameter: the first of the name gets the value and the others are removed, or it is added if there is none.
 * @function URLSearchParams.set
 * @param {string} name - The name.
 * @param {string} value - The value.
 */
/*@jsdoc
 * Sorts the parameters by name, keeping the order of those with the same name.
 * @function URLSearchParams.sort
 */
/*@jsdoc
 * Gets the parameters as a query, without a leading <code>"?"</code>.
 * @function URLSearchParams.toString
 * @returns {string} The query.
 */
/*@jsdoc
 * Calls a function for each parameter, in order.
 * @function URLSearchParams.forEach
 * @param {URLSearchParams~forEachCallback} callback - The function to call.
 * @param {object} [thisArg] - The <code>this</code> of each call.
 */
/*@jsdoc
 * Called by {@link URLSearchParams.forEach} for each parameter.
 * @callback URLSearchParams~forEachCallback
 * @param {string} value - The value.
 * @param {string} name - The name.
 * @param {URLSearchParams} params - The parameters being iterated.
 */
/*@jsdoc
 * Gets the <code>[name, value]</code> pairs. The WHATWG API returns an iterator; an array iterates the same way, and a
 * <code>URLSearchParams</code> object is itself iterable over the same pairs.
 * @function URLSearchParams.entries
 * @returns {Array<Array<string>>} The pairs, in order.
 */
/*@jsdoc
 * Gets the names, as an array in place of the WHATWG iterator.
 * @function URLSearchParams.keys
 * @returns {Array<string>} The names, in order.
 */
/*@jsdoc
 * Gets the values, as an array in place of the WHATWG iterator.
 * @function URLSearchParams.values
 * @returns {Array<string>} The values, in order.
 */

/// Adds TextEncoder, TextDecoder, atob, btoa, crypto, structuredClone, performance, URL, URLSearchParams and DOMException
/// to the engine's global object. They are pure computation, so every context gets them.
void registerWebGlobals(ScriptEngine* engine);

/// The script half of the web globals, in WebGlobalsPrelude.cpp: a function that takes the natives and returns the globals
QString webGlobalsPrelude();

/// The end of webGlobalsPrelude(), in WebGlobalsUrlPrelude.cpp: URLSearchParams and URL
QString webGlobalsUrlPrelude();

#endif // overte_WebGlobalsClass_h

/// @}
