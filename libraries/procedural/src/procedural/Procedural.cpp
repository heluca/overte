//
//  Created by Bradley Austin Davis on 2015/09/05
//  Copyright 2013-2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Procedural.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>

#include <gpu/Batch.h>
#include <SharedUtil.h>
#include <NumericalConstants.h>
#include <GLMHelpers.h>
#include <NetworkingConstants.h>
#include <MetaverseAPI.h>
#include <shaders/Shaders.h>
#include <shared/GlobalAppProperties.h>

#include "ShaderConstants.h"
#include "Logging.h"

Q_LOGGING_CATEGORY(proceduralLog, "hifi.gpu.procedural")

bool Procedural::enableProceduralShaders = false;

// User-data parsing constants
static const QString PROCEDURAL_USER_DATA_KEY = "ProceduralEntity";
static const QString VERTEX_URL_KEY = "vertexShaderURL";
static const QString FRAGMENT_URL_KEY = "fragmentShaderURL";
static const QString URL_KEY = "shaderUrl";
static const QString VERSION_KEY = "version";
static const QString UNIFORMS_KEY = "uniforms";
static const QString CHANNELS_KEY = "channels";

// Which iChannelN have a texture URL in the channels array, using the same rules as Procedural::setProceduralData
static std::array<bool, MAX_PROCEDURAL_TEXTURE_CHANNELS> boundChannels(const QJsonArray& channels) {
    std::array<bool, MAX_PROCEDURAL_TEXTURE_CHANNELS> bound{};
    size_t channelCount = std::min(MAX_PROCEDURAL_TEXTURE_CHANNELS, (size_t)channels.size());
    for (size_t channel = 0; channel < channelCount; ++channel) {
        QJsonValue channelValue = channels.at((int)channel);
        if (channelValue.isString()) {
            bound[channel] = true;
        } else if (channelValue.isObject()) {
            QJsonObject channelMap = channelValue.toObject();
            auto urlItr = channelMap.constFind("url");
            bound[channel] = urlItr != channelMap.constEnd() && urlItr->isString();
        }
    }
    return bound;
}

// Shader replace strings
static const std::string PROCEDURAL_BLOCK = "//PROCEDURAL_BLOCK";
static const std::string PROCEDURAL_VERSION = "//PROCEDURAL_VERSION";

namespace {

bool isVulkanBackend() {
    return hifi::properties::getGraphicsAPI() == hifi::properties::GraphicsAPI::Vulkan;
}

const char* CUSTOM_UNIFORM_BLOCK = "proceduralCustomBuffer";

// std140 base alignment and size of a block member of this type; false for a type the block cannot hold
bool std140Member(ProceduralCustomUniform& uniform, size_t& alignment, size_t& size) {
    struct TypeInfo { int baseType; int components; int columns; };
    static const std::unordered_map<std::string, TypeInfo> TYPES {
        { "float", { 0, 1, 0 } }, { "int", { 1, 1, 0 } },   { "uint", { 2, 1, 0 } },  { "bool", { 1, 1, 0 } },
        { "vec2", { 0, 2, 0 } },  { "ivec2", { 1, 2, 0 } }, { "uvec2", { 2, 2, 0 } }, { "bvec2", { 1, 2, 0 } },
        { "vec3", { 0, 3, 0 } },  { "ivec3", { 1, 3, 0 } }, { "uvec3", { 2, 3, 0 } }, { "bvec3", { 1, 3, 0 } },
        { "vec4", { 0, 4, 0 } },  { "ivec4", { 1, 4, 0 } }, { "uvec4", { 2, 4, 0 } }, { "bvec4", { 1, 4, 0 } },
        { "mat2", { 0, 2, 2 } },  { "mat3", { 0, 3, 3 } },  { "mat4", { 0, 4, 4 } },
    };
    auto itr = TYPES.find(uniform.type);
    if (itr == TYPES.end()) {
        return false;
    }
    uniform.baseType = itr->second.baseType;
    uniform.components = itr->second.components;
    uniform.columns = itr->second.columns;
    if (uniform.columns > 0) {
        // Column-major, each column padded to a vec4
        alignment = 16;
        size = 16 * uniform.columns;
    } else {
        static const size_t VECTOR_ALIGNMENT[] = { 0, 4, 8, 16, 16 };
        alignment = VECTOR_ALIGNMENT[uniform.components];
        size = 4 * uniform.components;
    }
    if (uniform.arraySize > 0) {
        // Every array element starts on a vec4
        alignment = 16;
        size = (size + 15) & ~size_t(15);
    }
    return true;
}

// Moves the `uniform TYPE NAME;` declarations out of a shader into `uniforms` (first declaration of a name wins),
// leaving a comment in their place
void extractLooseUniforms(QString& shaderText, std::vector<ProceduralCustomUniform>& uniforms) {
    static const QRegularExpression DECLARATION("^[ \\t]*uniform[ \\t]+(\\w+)[ \\t]+(\\w+)[ \\t]*(?:\\[[ \\t]*(\\d+)[ \\t]*\\])?[ \\t]*;",
                                                QRegularExpression::MultilineOption);
    struct Found { qsizetype start; qsizetype length; };
    std::vector<Found> found;
    auto matches = DECLARATION.globalMatch(shaderText);
    while (matches.hasNext()) {
        auto match = matches.next();
        ProceduralCustomUniform uniform;
        uniform.type = match.captured(1).toStdString();
        uniform.name = match.captured(2).toStdString();
        uniform.arraySize = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
        size_t alignment, size;
        if (!std140Member(uniform, alignment, size)) {
            // Leave it: glslang reports the declaration, which is clearer than a silent drop
            continue;
        }
        bool known = std::any_of(uniforms.begin(), uniforms.end(), [&](const ProceduralCustomUniform& u) { return u.name == uniform.name; });
        if (!known) {
            uniforms.push_back(uniform);
        }
        found.push_back({ match.capturedStart(0), match.capturedLength(0) });
    }
    for (auto itr = found.rbegin(); itr != found.rend(); ++itr) {
        shaderText.replace(itr->start, itr->length, "// uniform moved to " + QString(CUSTOM_UNIFORM_BLOCK));
    }
}

// Assigns std140 offsets, returning the block's size
size_t layoutStd140(std::vector<ProceduralCustomUniform>& uniforms) {
    size_t offset = 0;
    for (auto& uniform : uniforms) {
        size_t alignment, size;
        std140Member(uniform, alignment, size);
        offset = (offset + alignment - 1) & ~(alignment - 1);
        uniform.offset = offset;
        uniform.stride = size;
        offset += size * std::max(1, uniform.arraySize);
    }
    return (offset + 15) & ~size_t(15);
}

// The block declaration that replaces the loose uniforms, for the dialect header's UNIFORM_BUFFER macro.
// It starts with a newline: the scribe source keeps the `//` in front of PROCEDURAL_BLOCK_BEGIN.
QString customUniformBlock(const std::vector<ProceduralCustomUniform>& uniforms) {
    QString block = "\nUNIFORM_BUFFER(" + QString::number(procedural::slot::buffer::Custom) + ", " + CUSTOM_UNIFORM_BLOCK + ") {\n";
    for (const auto& uniform : uniforms) {
        block += "    " + QString::fromStdString(uniform.type) + " " + QString::fromStdString(uniform.name);
        if (uniform.arraySize > 0) {
            block += "[" + QString::number(uniform.arraySize) + "]";
        }
        block += ";\n";
    }
    block += "};\n";
    return block;
}

void writeScalar(uint8_t* destination, int baseType, double value) {
    switch (baseType) {
        case 1: {
            int32_t v = (int32_t)std::lround(value);
            memcpy(destination, &v, sizeof(v));
            break;
        }
        case 2: {
            uint32_t v = (uint32_t)std::lround(std::max(0.0, value));
            memcpy(destination, &v, sizeof(v));
            break;
        }
        default: {
            float v = (float)value;
            memcpy(destination, &v, sizeof(v));
            break;
        }
    }
}

// Writes a userData value into the block, with the shapes setupUniforms accepts for glUniform: a number, an array
// of numbers (vector or matrix, column-major), or an array of those for an array uniform
void writeCustomUniform(std::vector<uint8_t>& block, const ProceduralCustomUniform& uniform, const QJsonValue& value) {
    std::vector<std::vector<double>> elements;
    if (value.isDouble()) {
        elements.push_back({ value.toDouble() });
    } else if (value.isArray()) {
        const QJsonArray values = value.toArray();
        if (!values.isEmpty() && values[0].isArray()) {
            for (const auto& inner : values) {
                std::vector<double> element;
                for (const auto& v : inner.toArray()) {
                    element.push_back(v.toDouble());
                }
                elements.push_back(element);
            }
        } else if (uniform.arraySize > 0 && uniform.components == 1 && uniform.columns == 0) {
            for (const auto& v : values) {
                elements.push_back({ v.toDouble() });
            }
        } else {
            std::vector<double> element;
            for (const auto& v : values) {
                element.push_back(v.toDouble());
            }
            elements.push_back(element);
        }
    }

    const size_t elementCount = std::min(elements.size(), (size_t)std::max(1, uniform.arraySize));
    for (size_t i = 0; i < elementCount; i++) {
        const auto& values = elements[i];
        const size_t base = uniform.offset + i * uniform.stride;
        const int columns = std::max(1, uniform.columns);
        for (int column = 0; column < columns; column++) {
            for (int row = 0; row < uniform.components; row++) {
                const size_t index = column * uniform.components + row;
                if (index >= values.size()) {
                    break;
                }
                const size_t at = base + (uniform.columns > 0 ? column * 16 : 0) + row * 4;
                if (at + 4 <= block.size()) {
                    writeScalar(block.data() + at, uniform.baseType, values[index]);
                }
            }
        }
    }
}

}  // namespace

bool operator==(const ProceduralData& a, const ProceduralData& b) {
    return ((a.version == b.version) &&
            (a.fragmentShaderUrl == b.fragmentShaderUrl) &&
            (a.vertexShaderUrl == b.vertexShaderUrl) &&
            (a.uniforms == b.uniforms) &&
            (a.channels == b.channels));
}

QJsonValue ProceduralData::getProceduralData(const QString& proceduralJson) {
    if (proceduralJson.isEmpty()) {
        return QJsonValue();
    }

    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(proceduralJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QJsonValue();
    }

    auto object = doc.object();
    auto userDataIt = object.find(PROCEDURAL_USER_DATA_KEY);
    if (userDataIt != object.end()) {
        return userDataIt.value();
    }

    return object;
}

ProceduralData ProceduralData::parse(const QString& proceduralData) {
    ProceduralData result;
    result.parse(getProceduralData(proceduralData).toObject());
    return result;
}

void ProceduralData::parse(const QJsonObject& proceduralData) {
    if (proceduralData.isEmpty()) {
        return;
    }

    {
        auto versionJson = proceduralData[VERSION_KEY];
        if (versionJson.isDouble()) {
            version = (uint8_t)(floor(versionJson.toDouble()));
            // invalid version
            if (!(version == 1 || version == 2 || version == 3 || version == 4)) {
                return;
            }
        } else {
            // All unversioned shaders default to V1
            version = 1;
        }
    }

    { // Fragment shader URL (either fragmentShaderURL or shaderUrl)
        auto rawShaderUrl = proceduralData[FRAGMENT_URL_KEY].toString();
        fragmentShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);

        if (fragmentShaderUrl.isEmpty()) {
            rawShaderUrl = proceduralData[URL_KEY].toString();
            fragmentShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);
        }
    }

    {  // Vertex shader URL
        auto rawShaderUrl = proceduralData[VERTEX_URL_KEY].toString();
        vertexShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);
    }

    uniforms = proceduralData[UNIFORMS_KEY].toObject();
    channels = proceduralData[CHANNELS_KEY].toArray();
}

std::function<void(gpu::StatePointer, bool)> Procedural::opaqueStencil = [](gpu::StatePointer state, bool useAA) {};
std::function<void(gpu::StatePointer)> Procedural::transparentStencil = [](gpu::StatePointer state) {};

Procedural::Procedural(bool useAA) {
    _opaqueState->setCullMode(gpu::State::CULL_NONE);
    _opaqueState->setDepthTest(true, true, ComparisonFunction::LESS_EQUAL);
    _opaqueState->setBlendFunction(false,
        gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA,
        gpu::State::FACTOR_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::ONE);
    opaqueStencil(_opaqueState, useAA);

    _transparentState->setCullMode(gpu::State::CULL_NONE);
    _transparentState->setDepthTest(true, false, ComparisonFunction::LESS_EQUAL);
    _transparentState->setBlendFunction(true,
        gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA,
        gpu::State::FACTOR_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::ONE);
    transparentStencil(_transparentState);

    _standardInputsBuffer = std::make_shared<gpu::Buffer>(gpu::Buffer::UniformBuffer, sizeof(StandardInputs), nullptr);
}

void Procedural::setProceduralData(const ProceduralData& proceduralData) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (proceduralData == _data) {
        return;
    }

    _enabled = false;

    if (proceduralData.version != _data.version) {
        _data.version = proceduralData.version;
        _shaderDirty = true;
    }

    if (proceduralData.uniforms != _data.uniforms) {
        // If the uniform keys changed, we need to recreate the whole shader to handle the reflection
        if (proceduralData.uniforms.keys() != _data.uniforms.keys()) {
            _shaderDirty = true;
        }
        _data.uniforms = proceduralData.uniforms;
        _uniformsDirty = true;
    }

    if (proceduralData.channels != _data.channels) {
        // The bound channels are declared in the shader reflection, so a change in which ones are bound needs new pipelines
        if (boundChannels(proceduralData.channels) != boundChannels(_data.channels)) {
            _shaderDirty = true;
        }
        _data.channels = proceduralData.channels;

        static Sampler defaultSampler;
        static std::once_flag once;
        std::call_once(once, [&] {
            defaultSampler = Sampler(Sampler::FILTER_MIN_MAG_MIP_LINEAR);
        });

        // Must happen on the main thread
        auto textureCache = DependencyManager::get<TextureCache>();
        size_t channelCount = std::min(MAX_PROCEDURAL_TEXTURE_CHANNELS, (size_t)proceduralData.channels.size());
        for (size_t channel = 0; channel < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++channel) {
            if (channel < channelCount) {
                QJsonValue channelValue = proceduralData.channels.at((int)channel);
                if (channelValue.isString()) {
                    QString url = channelValue.toString();
                    _channels[channel] = textureCache->getTexture(QUrl(url));
                    _samplers[channel] = defaultSampler;
                    continue;
                } else if (channelValue.isObject()) {
                    QJsonObject channelMap = channelValue.toObject();

                    auto urlItr = channelMap.constFind("url");
                    if (urlItr != channelMap.constEnd() && urlItr->isString()) {
                        _channels[channel] = textureCache->getTexture(QUrl(urlItr->toString()));

                        auto samplerItr = channelMap.constFind("sampler");
                        if (samplerItr != channelMap.constEnd() && samplerItr->isObject()) {
                            _samplers[channel] = Sampler::parseSampler(samplerItr->toObject());
                        } else {
                            _samplers[channel] = defaultSampler;
                        }
                        continue;
                    }
                }
            }

            // Fallthrough case:
            // Release those textures no longer in use
            _channels[channel] = textureCache->getTexture(QUrl());
            _samplers[channel] = defaultSampler;
        }
    }

    if (proceduralData.fragmentShaderUrl != _data.fragmentShaderUrl) {
        _data.fragmentShaderUrl = proceduralData.fragmentShaderUrl;

        _shaderDirty = true;
        _networkFragmentShader.reset();
        _fragmentShaderPath.clear();
        _fragmentShaderSource.clear();

        if (!_data.fragmentShaderUrl.isValid()) {
            qCWarning(proceduralLog) << "Invalid fragment shader URL: " << _data.fragmentShaderUrl;
            return;
        }

        if (_data.fragmentShaderUrl.isLocalFile()) {
            if (!QFileInfo(_data.fragmentShaderUrl.toLocalFile()).exists()) {
                qCWarning(proceduralLog) << "Invalid fragment shader URL, missing local file: " << _data.fragmentShaderUrl;
                return;
            }
            _fragmentShaderPath = _data.fragmentShaderUrl.toLocalFile();
        } else if (_data.fragmentShaderUrl.scheme() == URL_SCHEME_QRC) {
            _fragmentShaderPath = ":" + _data.fragmentShaderUrl.path();
        } else {
            _networkFragmentShader = ShaderCache::instance().getShader(_data.fragmentShaderUrl);
        }
    }

    if (proceduralData.vertexShaderUrl != _data.vertexShaderUrl) {
        _data.vertexShaderUrl = proceduralData.vertexShaderUrl;

        _shaderDirty = true;
        _networkVertexShader.reset();
        _vertexShaderPath.clear();
        _vertexShaderSource.clear();

        if (!_data.vertexShaderUrl.isValid()) {
            qCWarning(proceduralLog) << "Invalid vertex shader URL: " << _data.vertexShaderUrl;
            return;
        }

        if (_data.vertexShaderUrl.isLocalFile()) {
            if (!QFileInfo(_data.vertexShaderUrl.toLocalFile()).exists()) {
                qCWarning(proceduralLog) << "Invalid vertex shader URL, missing local file: " << _data.vertexShaderUrl;
                return;
            }
            _vertexShaderPath = _data.vertexShaderUrl.toLocalFile();
        } else if (_data.vertexShaderUrl.scheme() == URL_SCHEME_QRC) {
            _vertexShaderPath = ":" + _data.vertexShaderUrl.path();
        } else {
            _networkVertexShader = ShaderCache::instance().getShader(_data.vertexShaderUrl);
        }

    }

    _enabled = true;
}

bool Procedural::isReady() const {
    auto backendApi = hifi::properties::getGraphicsAPI();
    // GLES has no runtime shader path. The Vulkan backend compiles the shader with glslang when its pipeline is built.
    if (backendApi == hifi::properties::GraphicsAPI::GLES32) {
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    if (!_enabled) {
        return false;
    }

    // We need to have at least one shader, and whichever ones we have need to be loaded
    bool hasFragmentShader = !_fragmentShaderPath.isEmpty() || _networkFragmentShader;
    bool fragmentShaderLoaded = !_fragmentShaderPath.isEmpty() || (_networkFragmentShader && _networkFragmentShader->isLoaded());
    bool hasVertexShader = !_vertexShaderPath.isEmpty() || _networkVertexShader;
    bool vertexShaderLoaded = !_vertexShaderPath.isEmpty() || (_networkVertexShader && _networkVertexShader->isLoaded());
    if ((!hasFragmentShader && !hasVertexShader) || (hasFragmentShader && !fragmentShaderLoaded) || (hasVertexShader && !vertexShaderLoaded)) {
        return false;
    }

    // Do we have textures, and if so, are they loaded?
    for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
        if (_channels[i] && !_channels[i]->isLoaded()) {
            return false;
        }
    }

    return true;
}

void Procedural::prepare(gpu::Batch& batch,
                         const glm::vec3& position,
                         const glm::vec3& size,
                         const glm::quat& orientation,
                         const uint64_t& created,
                         const ProceduralProgramKey key) {
    std::lock_guard<std::mutex> lock(_mutex);
    _entityDimensions = size;
    _entityPosition = position;
    _entityOrientation = glm::mat3_cast(orientation);
    _entityCreated = created;
    if (!_fragmentShaderPath.isEmpty()) {
        auto lastModified = (uint64_t)QFileInfo(_fragmentShaderPath).lastModified().toMSecsSinceEpoch();
        if (lastModified > _fragmentShaderModified) {
            QFile file(_fragmentShaderPath);
            if (file.open(QIODevice::ReadOnly)) {
                _fragmentShaderSource = QTextStream(&file).readAll();
                _shaderDirty = true;
                _fragmentShaderModified = lastModified;
            }
        }
    } else if (_fragmentShaderSource.isEmpty() && _networkFragmentShader && _networkFragmentShader->isLoaded()) {
        _fragmentShaderSource = _networkFragmentShader->_source;
        _shaderDirty = true;
    }

    if (!_vertexShaderPath.isEmpty()) {
        auto lastModified = (uint64_t)QFileInfo(_vertexShaderPath).lastModified().toMSecsSinceEpoch();
        if (lastModified > _vertexShaderModified) {
            QFile file(_vertexShaderPath);
            if (file.open(QIODevice::ReadOnly)) {
                _vertexShaderSource = QTextStream(&file).readAll();
                _shaderDirty = true;
                _vertexShaderModified = lastModified;
            }
        }
    } else if (_vertexShaderSource.isEmpty() && _networkVertexShader && _networkVertexShader->isLoaded()) {
        _vertexShaderSource = _networkVertexShader->_source;
        _shaderDirty = true;
    }

    if (_shaderDirty) {
        _proceduralPipelines.clear();
    }

    auto pipeline = _proceduralPipelines.find(key);
    bool recompiledShader = false;
    if (pipeline == _proceduralPipelines.end()) {
        gpu::Shader::Source vertexSource;
        if (key.isSkinnedDQ()) {
            vertexSource = _vertexSourceSkinnedDQ;
        } else if (key.isSkinned()) {
            vertexSource = _vertexSourceSkinned;
        } else {
            vertexSource = _vertexSource;
        }

        gpu::Shader::Source& fragmentSource = (key.isTransparent() && _transparentFragmentSource.valid()) ? _transparentFragmentSource : _opaqueFragmentSource;

        // Build the fragment and vertex shaders
        auto versionDefine = "#define PROCEDURAL_V" + std::to_string(_data.version);
        QString fragmentShaderText = _fragmentShaderSource;
        QString vertexShaderText = _vertexShaderSource;
        _customUniforms.clear();
        _customUniformSize = 0;
        if (isVulkanBackend()) {
            // Vulkan GLSL has no loose uniforms: the shader's `uniform` declarations become one block that both
            // stages share, filled from userData in setupUniforms
            extractLooseUniforms(fragmentShaderText, _customUniforms);
            extractLooseUniforms(vertexShaderText, _customUniforms);
            _customUniformSize = layoutStd140(_customUniforms);
            if (!_customUniforms.empty()) {
                const QString block = customUniformBlock(_customUniforms);
                if (!fragmentShaderText.isEmpty()) {
                    fragmentShaderText = block + fragmentShaderText;
                }
                if (!vertexShaderText.isEmpty()) {
                    vertexShaderText = block + vertexShaderText;
                }
            }
        }
        fragmentSource.replacements = _fragmentReplacements;
        fragmentSource.replacements[PROCEDURAL_VERSION] = versionDefine;
        if (!_fragmentShaderSource.isEmpty()) {
            fragmentSource.replacements[PROCEDURAL_BLOCK] = fragmentShaderText.toStdString();
        }
        vertexSource.replacements = _vertexReplacements;
        vertexSource.replacements[PROCEDURAL_VERSION] = versionDefine;
        if (!_vertexShaderSource.isEmpty()) {
            vertexSource.replacements[PROCEDURAL_BLOCK] = vertexShaderText.toStdString();
        }

        // Every dialect/variant reflection of both shaders, for the bindings added at runtime
        auto collectReflections = [](gpu::Shader::Source& source) {
            std::vector<shader::Reflection*> reflections;
            for (auto& dialect : source.dialectSources) {
                for (auto& variant : dialect.second.variantSources) {
                    reflections.push_back(&variant.second.reflection);
                }
            }
            return reflections;
        };
        std::vector<shader::Reflection*> allFragmentReflections = collectReflections(fragmentSource);
        std::vector<shader::Reflection*> allVertexReflections = collectReflections(vertexSource);

        // The Vulkan pipeline layout comes from the reflection: declare the block there, or take it back out
        for (auto reflections : { &allFragmentReflections, &allVertexReflections }) {
            for (auto reflection : *reflections) {
                if (_customUniforms.empty()) {
                    reflection->uniformBuffers.erase(CUSTOM_UNIFORM_BLOCK);
                } else {
                    reflection->uniformBuffers[CUSTOM_UNIFORM_BLOCK] = procedural::slot::buffer::Custom;
                }
                reflection->updateValid();
            }
        }

        // The Vulkan pipeline layout comes from the reflection: without a binding for iChannelN the descriptor set
        // has no slot for the channel, SPIRV-Cross aliases it onto another texture and the pipeline fails to build.
        const auto channelsBound = boundChannels(_data.channels);
        for (auto reflections : { &allFragmentReflections, &allVertexReflections }) {
            for (auto reflection : *reflections) {
                for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
                    const std::string channelName = "iChannel" + std::to_string(i);
                    if (channelsBound[i]) {
                        reflection->textures[channelName] = (int32_t)(procedural::slot::texture::Channel0 + i);
                    } else {
                        reflection->textures.erase(channelName);
                    }
                }
                reflection->updateValid();
            }
        }

        // Set any userdata specified uniforms (if any)
        if (!_data.uniforms.empty()) {
            // Fill in every reflection the new custom bindings
            int customSlot = procedural::slot::uniform::Custom;
            _slotMap.clear();
            for (const auto& key : _data.uniforms.keys()) {
                bool isArrayUniform = false;
                int numSlots = 0;
                const QJsonValue& value = _data.uniforms[key];
                if (value.isDouble()) {
                    numSlots = 1;
                } else if (value.isArray()) {
                    const QJsonArray valueArray = value.toArray();
                    if (valueArray.size() > 0) {
                        if (valueArray[0].isArray()) {
                            const int valueLength = valueArray[0].toArray().size();
                            int count = 0;
                            for (const QJsonValue& value : valueArray) {
                                if (value.isArray()) {
                                    const QJsonArray innerValueArray = value.toArray();
                                    if (innerValueArray.size() == valueLength) {
                                        if (valueLength == 3 || valueLength == 4 || valueLength == 9 || valueLength == 16) {
                                            count++;
                                            isArrayUniform = true;
                                        }
                                    }
                                }
                            }
                            numSlots = count;
                        } else if (valueArray[0].isDouble()) {
                            numSlots = 1;
                        }
                    }
                }

                if (numSlots > 0) {
                    std::string uniformName = key.toLocal8Bit().data();
                    std::string trueUniformName = uniformName;
                    if (isArrayUniform) {
                        trueUniformName += "[0]";
                    }
                    for (auto reflection : allFragmentReflections) {
                        reflection->uniforms[trueUniformName] = customSlot;
                    }
                    for (auto reflection : allVertexReflections) {
                        reflection->uniforms[trueUniformName] = customSlot;
                    }
                    _slotMap[uniformName] = customSlot;
                    customSlot += numSlots;
                }
            }
        }

        // Leave this here for debugging
        //qCDebug(proceduralLog) << "FragmentShader:\n" << fragmentSource.getSource(shader::Dialect::glsl450, shader::Variant::Mono).c_str();
        //qCDebug(proceduralLog) << "VertexShader:\n" << vertexSource.getSource(shader::Dialect::glsl450, shader::Variant::Mono).c_str();

        gpu::ShaderPointer vertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer fragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer program = gpu::Shader::createProgram(vertexShader, fragmentShader);

        _proceduralPipelines[key] = gpu::Pipeline::create(program, key.isTransparent() ? _transparentState : _opaqueState);

        // Error fallback: pink checkerboard
        if (_errorFallbackFragmentSource.isEmpty()) {
            QFile file(_errorFallbackFragmentPath);
            if (file.open(QIODevice::ReadOnly)) {
                _errorFallbackFragmentSource = QTextStream(&file).readAll();
            }
        }
        vertexSource.replacements.erase(PROCEDURAL_BLOCK);
        fragmentSource.replacements[PROCEDURAL_BLOCK] = _errorFallbackFragmentSource.toStdString();
        gpu::ShaderPointer errorVertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer errorFragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer errorProgram = gpu::Shader::createProgram(errorVertexShader, errorFragmentShader);
        _errorPipelines[key] = gpu::Pipeline::create(errorProgram, key.isTransparent() ? _transparentState : _opaqueState);

        // Disabled fallback: the stub blocks, and PROCEDURAL_DISABLED tells the fragment shader to skip the
        // procedural code entirely, since a stub cannot override the emissive output that PROCEDURAL_V1 hardcodes
        vertexSource.replacements.erase(PROCEDURAL_BLOCK);
        fragmentSource.replacements.erase(PROCEDURAL_BLOCK);
        fragmentSource.replacements[PROCEDURAL_VERSION] = versionDefine + "\n#define PROCEDURAL_DISABLED";
        gpu::ShaderPointer disabledVertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer disabledFragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer disabledProgram = gpu::Shader::createProgram(disabledVertexShader, disabledFragmentShader);
        _disabledPipelines[key] = gpu::Pipeline::create(disabledProgram, key.isTransparent() ? _transparentState : _opaqueState);

        _lastCompile = usecTimestampNow();
        if (_firstCompile == 0) {
            _firstCompile = _lastCompile;
        }
        _frameCount = 0;
        recompiledShader = true;
    }

    gpu::PipelinePointer finalPipeline = recompiledShader ? _proceduralPipelines[key] : pipeline->second;
    if (!enableProceduralShaders) {
        finalPipeline = _disabledPipelines[key];
    } else if (!finalPipeline || finalPipeline->getProgram()->compilationHasFailed()) {
        finalPipeline = _errorPipelines[key];
    }

    // FIXME: need to handle forward rendering
    batch.setPipeline(finalPipeline);

    bool recreateUniforms = _shaderDirty || _uniformsDirty || recompiledShader || _prevKey != key;
    if (recreateUniforms) {
        setupUniforms();
    }

    _prevKey = key;
    _shaderDirty = _uniformsDirty = false;

    for (auto lambda : _uniforms) {
        lambda(batch);
    }

    for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
        if (_channels[i] && _channels[i]->isLoaded()) {
            auto gpuTexture = _channels[i]->getGPUTexture();
            if (gpuTexture) {
                gpuTexture->setSampler(_samplers[i]);
                gpuTexture->setAutoGenerateMips(true);
            }
            batch.setResourceTexture((gpu::uint32)(procedural::slot::texture::Channel0 + i), gpuTexture);
        }
    }
}

void Procedural::setupUniforms() {
    _uniforms.clear();
    if (isVulkanBackend()) {
        // The shader's uniforms are one block on Vulkan (see prepare): fill it from userData and bind it
        if (!_customUniforms.empty()) {
            std::vector<uint8_t> block(_customUniformSize, 0);
            for (const auto& uniform : _customUniforms) {
                writeCustomUniform(block, uniform, _data.uniforms.value(QString::fromStdString(uniform.name)));
            }
            if (!_customUniformBuffer) {
                _customUniformBuffer = std::make_shared<gpu::Buffer>(gpu::Buffer::UniformBuffer, block.size(), block.data());
            } else {
                _customUniformBuffer->setData(block.size(), block.data());
            }
            auto buffer = _customUniformBuffer;
            const auto size = block.size();
            _uniforms.push_back([buffer, size](gpu::Batch& batch) { batch.setUniformBuffer(procedural::slot::buffer::Custom, buffer, 0, size); });
        }
    } else {
    // Set any userdata specified uniforms
    for (const auto& key : _data.uniforms.keys()) {
        const std::string uniformName = key.toLocal8Bit().data();
        auto slotItr = _slotMap.find(uniformName);
        if (slotItr == _slotMap.end()) {
            continue;
        }

        const int slot = slotItr->second;
        const QJsonValue& value = _data.uniforms[key];
        if (value.isDouble()) {
            const float v = value.toDouble();
            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform1f(slot, v); });
        } else if (value.isArray()) {
            const QJsonArray valueArray = value.toArray();
            if (valueArray.size() > 0) {
                if (valueArray[0].isArray()) {
                    const int valueLength = valueArray[0].toArray().size();
                    std::vector<float> vs;
                    vs.reserve(valueLength * valueArray.size());
                    int count = 0;
                    for (const QJsonValue& value : valueArray) {
                        if (value.isArray()) {
                            const QJsonArray innerValueArray = value.toArray();
                            if (innerValueArray.size() == valueLength) {
                                if (valueLength == 3 || valueLength == 4 || valueLength == 9 || valueLength == 16) {
                                    for (int i = 0; i < valueLength; i++) {
                                        vs.push_back(innerValueArray[i].toDouble());
                                    }
                                    count++;
                                }
                            }
                        }
                    }
                    if (count > 0) {
                        switch (valueLength) {
                            case 3: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniform3fv(slot, count, vs.data()); });
                                break;
                            }
                            case 4: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniform4fv(slot, count, vs.data()); });
                                break;
                            }
                            case 9: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniformMatrix3fv(slot, count, false, vs.data()); });
                                break;
                            }
                            case 16: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniformMatrix4fv(slot, count, false, vs.data()); });
                                break;
                            }
                            default:
                                break;
                        }
                    }
                } else if (valueArray[0].isDouble()) {
                    switch (valueArray.size()) {
                        case 1: {
                            const float v = valueArray[0].toDouble();
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 2: {
                            const glm::vec2 v{ valueArray[0].toDouble(), valueArray[1].toDouble() };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 3: {
                            const glm::vec3 v{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                            };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 4: {
                            const glm::vec4 v{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                            };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 9: {
                            const glm::mat3 m{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                                valueArray[4].toDouble(),
                                valueArray[5].toDouble(),
                                valueArray[6].toDouble(),
                                valueArray[7].toDouble(),
                                valueArray[8].toDouble(),
                            };
                            _uniforms.push_back([slot, m](gpu::Batch& batch) { batch._glUniform(slot, m); });
                            break;
                        }
                        case 16: {
                            const glm::mat4 m{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                                valueArray[4].toDouble(),
                                valueArray[5].toDouble(),
                                valueArray[6].toDouble(),
                                valueArray[7].toDouble(),
                                valueArray[8].toDouble(),
                                valueArray[9].toDouble(),
                                valueArray[10].toDouble(),
                                valueArray[11].toDouble(),
                                valueArray[12].toDouble(),
                                valueArray[13].toDouble(),
                                valueArray[14].toDouble(),
                                valueArray[15].toDouble(),
                            };
                            _uniforms.push_back([slot, m](gpu::Batch& batch) { batch._glUniform(slot, m); });
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }
    }

    _uniforms.push_back([this](gpu::Batch& batch) {
        _standardInputs.position = vec4(_entityPosition, 1.0f);
        // Minimize floating point error by doing an integer division to milliseconds, before the floating point division to seconds
        auto now = usecTimestampNow();
        _standardInputs.timeSinceLastCompile = (float)((now - _lastCompile) / USECS_PER_MSEC) / MSECS_PER_SECOND;
        _standardInputs.timeSinceFirstCompile = (float)((now - _firstCompile) / USECS_PER_MSEC) / MSECS_PER_SECOND;
        _standardInputs.timeSinceEntityCreation = (float)((now - _entityCreated) / USECS_PER_MSEC) / MSECS_PER_SECOND;


        // Date
        {
            QDateTime now = QDateTime::currentDateTimeUtc();
            QDate date = now.date();
            QTime time = now.time();
            _standardInputs.date.x = date.year();
            // Shadertoy month is 0 based
            _standardInputs.date.y = date.month() - 1;
            // But not the day... go figure
            _standardInputs.date.z = date.day();
            float fractSeconds = (time.msec() / 1000.0f);
            _standardInputs.date.w = (time.hour() * 3600) + (time.minute() * 60) + time.second() + fractSeconds;
        }

        _standardInputs.scale = vec4(_entityDimensions, 1.0f);
        _standardInputs.frameCount = ++_frameCount;
        _standardInputs.orientation = mat4(_entityOrientation);

        for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
            if (_channels[i]) {
                _standardInputs.resolution[i] = vec4(_channels[i]->getWidth(), _channels[i]->getHeight(), 1.0f, 1.0f);
            } else {
                _standardInputs.resolution[i] = vec4(1.0f);
            }
        }

        _standardInputsBuffer->setSubData(0, _standardInputs);
        batch.setUniformBuffer(procedural::slot::buffer::Inputs, _standardInputsBuffer, 0, sizeof(StandardInputs));
    });
}

glm::vec4 Procedural::getColor(const glm::vec4& entityColor) const {
    if (_data.version == 1) {
        return glm::vec4(1);
    }
    return entityColor;
}

bool Procedural::hasVertexShader() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return !_data.vertexShaderUrl.isEmpty();
}


void Procedural::setVertexReplacements(const std::unordered_map<std::string, std::string>& replacements) {
    std::lock_guard<std::mutex> lock(_mutex);
    _vertexReplacements = replacements;
    _shaderDirty = true;
}

void Procedural::setFragmentReplacements(const std::unordered_map<std::string, std::string>& replacements) {
    std::lock_guard<std::mutex> lock(_mutex);
    _fragmentReplacements = replacements;
    _shaderDirty = true;
}

void graphics::ProceduralMaterial::initializeProcedural() {
    _procedural._vertexSource = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural);
    _procedural._vertexSourceSkinned = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural_deformed);
    _procedural._vertexSourceSkinnedDQ = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural_deformeddq);

    // FIXME: Setup proper uniform slots and use correct pipelines for forward rendering
    _procedural._opaqueFragmentSource = gpu::Shader::getFragmentShaderSource(shader::render_utils::fragment::simple_procedural);
    _procedural._transparentFragmentSource = gpu::Shader::getFragmentShaderSource(shader::render_utils::fragment::simple_procedural_translucent);

    _procedural._errorFallbackFragmentPath = ":" + QUrl("qrc:///shaders/errorShader.frag").path();
}
