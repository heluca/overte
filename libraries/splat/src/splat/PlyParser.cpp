//
//  PlyParser.cpp
//  libraries/splat/src/splat
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "PlyParser.h"

#include <cmath>
#include <cstring>

#include <QtCore/QList>
#include <QtCore/QtGlobal>

#include "SplatLogging.h"

static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "The PLY body is read with memcpy, which assumes a little-endian host");

namespace splat {

namespace {

enum class ScalarType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

struct Property {
    QByteArray name;
    ScalarType type { ScalarType::Float32 };
    size_t offset { 0 };
};

struct Element {
    QByteArray name;
    quint64 count { 0 };
    std::vector<Property> properties;
    size_t stride { 0 };
    bool hasListProperty { false };
};

bool parseScalarType(const QByteArray& name, ScalarType& type, size_t& size) {
    if (name == "char" || name == "int8") {
        type = ScalarType::Int8;
        size = 1;
    } else if (name == "uchar" || name == "uint8") {
        type = ScalarType::UInt8;
        size = 1;
    } else if (name == "short" || name == "int16") {
        type = ScalarType::Int16;
        size = 2;
    } else if (name == "ushort" || name == "uint16") {
        type = ScalarType::UInt16;
        size = 2;
    } else if (name == "int" || name == "int32") {
        type = ScalarType::Int32;
        size = 4;
    } else if (name == "uint" || name == "uint32") {
        type = ScalarType::UInt32;
        size = 4;
    } else if (name == "float" || name == "float32") {
        type = ScalarType::Float32;
        size = 4;
    } else if (name == "double" || name == "float64") {
        type = ScalarType::Float64;
        size = 8;
    } else {
        return false;
    }
    return true;
}

template <typename T>
float readAs(const char* p) {
    T value;
    memcpy(&value, p, sizeof(T));
    return (float)value;
}

float readScalar(const char* p, ScalarType type) {
    switch (type) {
        case ScalarType::Int8:
            return readAs<qint8>(p);
        case ScalarType::UInt8:
            return readAs<quint8>(p);
        case ScalarType::Int16:
            return readAs<qint16>(p);
        case ScalarType::UInt16:
            return readAs<quint16>(p);
        case ScalarType::Int32:
            return readAs<qint32>(p);
        case ScalarType::UInt32:
            return readAs<quint32>(p);
        case ScalarType::Float32:
            return readAs<float>(p);
        case ScalarType::Float64:
            return readAs<double>(p);
    }
    return 0.0f;
}

float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

const char* const REQUIRED_PROPERTIES[] = {
    "x", "y", "z",
    "f_dc_0", "f_dc_1", "f_dc_2",
    "opacity",
    "scale_0", "scale_1", "scale_2",
    "rot_0", "rot_1", "rot_2", "rot_3",
};
constexpr size_t NUM_REQUIRED_PROPERTIES = sizeof(REQUIRED_PROPERTIES) / sizeof(REQUIRED_PROPERTIES[0]);

// Indices into REQUIRED_PROPERTIES.
enum Field { X, Y, Z, DC0, DC1, DC2, OPACITY, SCALE0, SCALE1, SCALE2, ROT0, ROT1, ROT2, ROT3 };

} // namespace

bool isPly(const QByteArray& data) {
    return data.startsWith("ply\n") || data.startsWith("ply\r\n");
}

bool parsePly(const QByteArray& data, SplatCloud& out, QString& error) {
    if (!isPly(data)) {
        error = "not a PLY file (missing \"ply\" magic line)";
        return false;
    }

    std::vector<Element> elements;
    bool sawFormat = false;
    bool sawEndHeader = false;
    int position = 0;
    while (position < data.size()) {
        int newline = data.indexOf('\n', position);
        if (newline < 0) {
            break;
        }
        const QList<QByteArray> tokens = data.mid(position, newline - position).simplified().split(' ');
        position = newline + 1;
        const QByteArray& keyword = tokens[0];

        if (keyword == "end_header") {
            sawEndHeader = true;
            break;
        } else if (keyword == "format") {
            if (tokens.size() < 2) {
                error = "malformed format line";
                return false;
            }
            if (tokens[1] == "ascii") {
                error = "ASCII PLY bodies are not supported; re-export as binary_little_endian";
                return false;
            }
            if (tokens[1] == "binary_big_endian") {
                error = "big-endian PLY bodies are not supported; re-export as binary_little_endian";
                return false;
            }
            if (tokens[1] != "binary_little_endian") {
                error = QString("unknown PLY format \"%1\"").arg(QString(tokens[1]));
                return false;
            }
            sawFormat = true;
        } else if (keyword == "element") {
            bool ok = false;
            Element element;
            if (tokens.size() == 3) {
                element.name = tokens[1];
                element.count = tokens[2].toULongLong(&ok);
            }
            if (!ok) {
                error = "malformed element line";
                return false;
            }
            elements.push_back(element);
        } else if (keyword == "property") {
            if (elements.empty()) {
                error = "property line before any element";
                return false;
            }
            Element& element = elements.back();
            if (tokens.size() >= 2 && tokens[1] == "list") {
                element.hasListProperty = true;
                continue;
            }
            Property property;
            size_t size = 0;
            if (tokens.size() != 3 || !parseScalarType(tokens[1], property.type, size)) {
                error = QString("malformed property line in element \"%1\"").arg(QString(element.name));
                return false;
            }
            property.name = tokens[2];
            property.offset = element.stride;
            element.stride += size;
            element.properties.push_back(property);
        }
        // "ply", "comment", "obj_info" and blank lines carry nothing we need.
    }

    if (!sawEndHeader) {
        error = "truncated PLY header (no end_header)";
        return false;
    }
    if (!sawFormat) {
        error = "PLY header has no format line";
        return false;
    }

    // The body starts right after the header, with the elements in header order.
    quint64 bodyOffset = (quint64)position;
    const Element* vertex = nullptr;
    for (const auto& element : elements) {
        if (element.name == "chunk") {
            error = "compressed PLY (chunk element) is not supported yet";
            return false;
        }
        if (element.name == "vertex") {
            vertex = &element;
            break;
        }
        if (element.hasListProperty) {
            error = QString("cannot skip list element \"%1\" before the vertex element").arg(QString(element.name));
            return false;
        }
        bodyOffset += element.count * element.stride;
    }
    if (!vertex) {
        error = "PLY has no vertex element";
        return false;
    }
    if (vertex->hasListProperty) {
        error = "list properties in the vertex element are not supported";
        return false;
    }
    if (vertex->count == 0) {
        error = "PLY has no splats";
        return false;
    }

    const Property* fields[NUM_REQUIRED_PROPERTIES] = {};
    for (size_t f = 0; f < NUM_REQUIRED_PROPERTIES; f++) {
        for (const auto& property : vertex->properties) {
            if (property.name == REQUIRED_PROPERTIES[f]) {
                fields[f] = &property;
                break;
            }
        }
        if (!fields[f]) {
            error = QString("vertex element is missing property \"%1\"; not a 3D Gaussian Splatting PLY?")
                        .arg(REQUIRED_PROPERTIES[f]);
            return false;
        }
    }

    const quint64 available = (quint64)data.size() > bodyOffset ? (quint64)data.size() - bodyOffset : 0;
    if (available / vertex->stride < vertex->count) {
        error = QString("truncated PLY body: header declares %1 splats of %2 bytes, %3 bytes present")
                    .arg(vertex->count).arg(vertex->stride).arg(available);
        return false;
    }

    const size_t count = (size_t)vertex->count;
    const char* base = data.constData() + bodyOffset;
    out = SplatCloud();
    out.reserve(count);
    size_t dropped = 0;
    float v[NUM_REQUIRED_PROPERTIES];
    for (size_t i = 0; i < count; i++) {
        const char* record = base + i * vertex->stride;
        for (size_t f = 0; f < NUM_REQUIRED_PROPERTIES; f++) {
            v[f] = readScalar(record + fields[f]->offset, fields[f]->type);
        }

        if (!std::isfinite(v[X]) || !std::isfinite(v[Y]) || !std::isfinite(v[Z])) {
            dropped++;
            continue;
        }

        const glm::vec3 color = glm::vec3(0.5f) + SH_C0 * glm::vec3(v[DC0], v[DC1], v[DC2]);
        const float alpha = sigmoid(v[OPACITY]);
        const glm::vec3 scale(std::exp(v[SCALE0]), std::exp(v[SCALE1]), std::exp(v[SCALE2]));

        // RDF to RUB with F = diag(1, -1, -1). Sigma' = F R S^2 R^T F = (F R F) S^2 (F R F)^T because F S^2 F = S^2,
        // and F R F is the same rotation about the mirrored axis (ax, -ay, -az): q' = (w, x, -y, -z).
        // Scale keeps its per-axis meaning.
        const glm::quat rotation = normalizedRotation(v[ROT0], v[ROT1], -v[ROT2], -v[ROT3]);

        out.append(glm::vec3(v[X], -v[Y], -v[Z]), color, alpha, scale, rotation);
    }

    if (out.count() == 0) {
        error = "every splat in the PLY has a non-finite position";
        return false;
    }
    if (dropped > 0) {
        qCWarning(splat_logging) << "Dropped" << dropped << "of" << count << "splats with a non-finite position";
    }

    out.computeBounds();
    return true;
}

} // namespace splat
