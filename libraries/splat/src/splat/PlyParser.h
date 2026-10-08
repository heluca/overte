//
//  PlyParser.h
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#ifndef overte_PlyParser_h
#define overte_PlyParser_h

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "SplatCloud.h"

namespace splat {

// True when data starts with the PLY magic line.
bool isPly(const QByteArray& data);

// Parses an INRIA 3D Gaussian Splatting .ply: an ASCII header and a binary_little_endian body whose "vertex"
// element carries x y z, f_dc_0..2, opacity, scale_0..2 and rot_0..3, in any order and of any scalar type.
// Other vertex properties (nx ny nz, f_rest_*, extras) are skipped. ASCII and big-endian bodies are rejected.
//
// Conversions, per splat:
//   color    = clamp(0.5 + SH_C0 * f_dc, 0, 1)
//   alpha    = sigmoid(opacity)
//   scale    = exp(scale_i)
//   rotation = normalize(rot_0..3), rot_0 is w
// Axes: INRIA/COLMAP files are right/down/forward; Overte is right/up/back. F = diag(1, -1, -1), a 180 degree
// rotation about X, is applied to every splat: positions become (x, -y, -z) and the quaternion (w, x, y, z) becomes
// (w, x, -y, -z), so the covariance the shader builds is F * Sigma * F. Scale is unchanged.
//
// Splats with a non-finite position are dropped. On success, out holds the cloud with bounds computed.
// On failure, returns false and sets error.
bool parsePly(const QByteArray& data, SplatCloud& out, QString& error);

} // namespace splat

#endif // overte_PlyParser_h
