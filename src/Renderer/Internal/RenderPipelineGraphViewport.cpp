#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"
#include "Renderer/ScopedGLObjects.h"

#include <stdexcept>

using namespace Stack::Renderer;

unsigned int RenderPipeline::SampleGraphViewportTexture(unsigned int texture,
    const ViewportPreviewGeometry& source, const ViewportPreviewGeometry& target) {
    if (!texture || !source.Valid() || !target.Valid() ||
        !ContainsImageRegion(source.resident, target.resident)) return 0;
    m_LastGraphExecutionStats.viewportInputSamplePixels += std::uint64_t(target.storageWidth) * target.storageHeight;
    const GLState::TextureBinding savedTexture(GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    savedTexture.Restore();
    const bool sameGrid = source.resident.fullWidth == target.resident.fullWidth &&
        source.resident.fullHeight == target.resident.fullHeight;
    // An integer crop must not insert a lower-precision intermediate between
    // an 8-bit source and an otherwise identical pointwise pass.
    const auto targetFormat = sameGrid && (format == GL_RGBA8 || format == GL_RGB8) ? static_cast<GLenum>(format) :
        (format == GL_R32F ? GL_R32F : (m_GraphFloat32Targets || format == GL_RGBA32F ? GL_RGBA32F : GL_RGBA16F));
    ScopedGLTexture output(GLHelpers::CreateStorageTexture(target.storageWidth, target.storageHeight,
        targetFormat));
    if (!output) return 0;
    if (sameGrid) {
        const GLState::FramebufferState saved(true);
        unsigned int read = GLHelpers::CreateFBO(texture);
        unsigned int draw = GLHelpers::CreateFBO(output.Get());
        if (!read || !draw) {
            saved.Restore(true);
            if (read) glDeleteFramebuffers(1, &read);
            if (draw) glDeleteFramebuffers(1, &draw);
            return 0;
        }
        const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glReadBuffer(GL_COLOR_ATTACHMENT0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        const int x = target.resident.x - source.resident.x;
        const int y = source.resident.y + source.resident.height - target.resident.y - target.resident.height;
        glBlitFramebuffer(x, y, x + target.storageWidth, y + target.storageHeight,
            0, 0, target.storageWidth, target.storageHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        const bool copied = glGetError() == GL_NO_ERROR;
        saved.Restore(true);
        glDeleteFramebuffers(1, &read); glDeleteFramebuffers(1, &draw);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        return copied ? output.Release() : 0;
    }
    if (!m_GraphViewportSampleProgram) {
        constexpr const char* vertex = R"(
#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
void main(){ gl_Position=vec4(position,0,1); }
)";
        constexpr const char* fragment = R"(
#version 330 core
out vec4 FragColor;
uniform sampler2D inputTexture;
uniform vec2 sourceFull;
uniform vec2 sourceOrigin;
uniform vec2 targetFull;
uniform vec2 targetOrigin;
void main() {
    // Pixel centers refer to the complete chosen grid, not the crop extent.
    vec2 globalUv = (targetOrigin + gl_FragCoord.xy) / targetFull;
    vec2 p = globalUv * sourceFull - sourceOrigin - vec2(0.5);
    ivec2 q = ivec2(floor(p));
    vec2 f = fract(p);
    ivec2 hi = textureSize(inputTexture,0) - ivec2(1);
    vec4 a = texelFetch(inputTexture,clamp(q,ivec2(0),hi),0);
    vec4 b = texelFetch(inputTexture,clamp(q+ivec2(1,0),ivec2(0),hi),0);
    vec4 c = texelFetch(inputTexture,clamp(q+ivec2(0,1),ivec2(0),hi),0);
    vec4 d = texelFetch(inputTexture,clamp(q+ivec2(1,1),ivec2(0),hi),0);
    FragColor = mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
)";
        m_GraphViewportSampleProgram = GLHelpers::CreateShaderProgram(vertex, fragment);
    }
    if (!m_GraphViewportSampleProgram) return 0;
    const int previousWidth = m_Width, previousHeight = m_Height;
    m_Width = target.storageWidth; m_Height = target.storageHeight;
    const bool rendered = RenderIntoGraphTargetTexture(output.Get(), [&](unsigned int) {
        const auto program = m_GraphViewportSampleProgram;
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(program, "inputTexture"), 0);
        glUniform2f(glGetUniformLocation(program, "sourceFull"), float(source.resident.fullWidth), float(source.resident.fullHeight));
        glUniform2f(glGetUniformLocation(program, "sourceOrigin"), float(source.resident.x),
            float(source.resident.fullHeight - source.resident.y - source.resident.height));
        glUniform2f(glGetUniformLocation(program, "targetFull"), float(target.resident.fullWidth), float(target.resident.fullHeight));
        glUniform2f(glGetUniformLocation(program, "targetOrigin"), float(target.resident.x),
            float(target.resident.fullHeight - target.resident.y - target.resident.height));
        m_Quad.Draw();
    });
    m_Width = previousWidth; m_Height = previousHeight;
    return rendered ? output.Release() : 0;
}

namespace Stack::Renderer::GraphExecution {

GraphExecutionRuntime::~GraphExecutionRuntime() {
    for (auto texture : viewportTemporaryTextures) if (texture) glDeleteTextures(1, &texture);
}

void GraphExecutionRuntime::HashViewportDemand(std::size_t& fingerprint, int nodeId, const std::string& socket) const {
    if (!viewportPlan.valid) return;
    const auto stage = viewportPlan.stages.find({nodeId, socket});
    if (stage == viewportPlan.stages.end() || !stage->second.regionalExecutable) return;
    HashCombine(fingerprint, std::size_t{1}); // Regional execution algorithm version.
    HashCombine(fingerprint, stage->second.demand.Fingerprint());
    HashCombine(fingerprint, HashValue(stage->second.renderWidth));
    HashCombine(fingerprint, HashValue(stage->second.renderHeight));
}

void GraphExecutionRuntime::RecordViewportGeometry(int nodeId, const std::string& socket, bool renderedPass) {
    if (!viewportPlan.valid) { pipeline.m_CurrentGraphImageGeometry = {}; return; }
    const auto found = viewportPlan.stages.find({nodeId, socket});
    if (found == viewportPlan.stages.end()) return;
    const auto& stage = found->second;
    ViewportPreviewGeometry geometry;
    geometry.logicalWidth = int(stage.spatial.fullWindow.width);
    geometry.logicalHeight = int(stage.spatial.fullWindow.height);
    geometry.storageWidth = pipeline.m_Width; geometry.storageHeight = pipeline.m_Height;
    geometry.resident = stage.regionalExecutable
        ? ScaleImageRegion(stage.demand, stage.renderWidth, stage.renderHeight)
        : WholeImageRegion(pipeline.m_Width, pipeline.m_Height);
    if (!geometry.Valid()) throw std::runtime_error("Regional graph output did not match its planned sample grid.");
    viewportGeometry[MakeNodeSocketKey(nodeId, socket)] = geometry;
    if (stage.regionalExecutable && renderedPass) {
        ++pipeline.m_LastGraphExecutionStats.viewportRegionalStages;
        pipeline.m_LastGraphExecutionStats.viewportRegionalPixels += std::uint64_t(geometry.storageWidth) * geometry.storageHeight;
    }
    pipeline.m_CurrentGraphImageGeometry = geometry;
}

unsigned int GraphExecutionRuntime::ViewportInput(unsigned int texture, const std::string& sourceKey,
    const RenderGraphViewportEndpoint& source, const RenderGraphViewportEndpoint& consumer) {
    const auto found = viewportGeometry.find(sourceKey);
    if (!texture || found == viewportGeometry.end() || !found->second.Valid()) return texture;
    const auto sourceGeometry = found->second;
    ViewportPreviewGeometry target = sourceGeometry;
    const auto parent = viewportPlan.stages.find(consumer);
    if (parent != viewportPlan.stages.end() && parent->second.regionalExecutable) {
        const auto& stage = parent->second;
        ImageRegion demand;
        for (const auto& input : stage.inputs)
            if (input.source.nodeId == source.nodeId && input.source.socketId == source.socketId) { demand = input.demand; break; }
        if (!demand.Valid()) demand = stage.demand; // A fused pointwise chain has the same image coordinates.
        if (sourceGeometry.logicalWidth != int(stage.spatial.fullWindow.width) ||
            sourceGeometry.logicalHeight != int(stage.spatial.fullWindow.height))
            throw std::runtime_error("Regional graph inputs have incompatible logical windows.");
        target.resident = ScaleImageRegion(demand, stage.renderWidth, stage.renderHeight);
        target.storageWidth = target.resident.width; target.storageHeight = target.resident.height;
    } else if (consumer.nodeId < 0 && source.nodeId == viewportPlan.output.nodeId && source.socketId == viewportPlan.output.socketId) {
        const auto& stage = viewportPlan.stages.at(source);
        const auto demand = pipeline.m_GraphViewportRequest.visible.Valid()
            ? ScaleImageRegion(pipeline.m_GraphViewportRequest.visible, target.logicalWidth, target.logicalHeight)
            : WholeImageRegion(target.logicalWidth, target.logicalHeight);
        target.resident = ScaleImageRegion(demand, stage.renderWidth, stage.renderHeight);
        target.storageWidth = target.resident.width; target.storageHeight = target.resident.height;
    }
    if (target.resident == sourceGeometry.resident) {
        pipeline.m_Width = sourceGeometry.storageWidth; pipeline.m_Height = sourceGeometry.storageHeight;
        pipeline.m_CurrentGraphImageGeometry = sourceGeometry;
        pipeline.m_RawViewportAppliedRegion = sourceGeometry.resident.Partial() ? sourceGeometry.resident : ImageRegion{};
        return texture;
    }
    if (!ContainsImageRegion(sourceGeometry.resident, target.resident))
        throw std::runtime_error("Regional graph input is missing required pixel coverage.");
    std::size_t fingerprint = HashValue(texture);
    HashCombine(fingerprint, sourceGeometry.resident.Fingerprint());
    HashCombine(fingerprint, target.resident.Fingerprint());
    const std::string localKey = sourceKey + ":viewport:" + std::to_string(fingerprint);
    auto copy = viewportInputCache.find(localKey);
    unsigned int sampled = copy != viewportInputCache.end() ? copy->second : 0;
    if (!sampled) {
        sampled = pipeline.SampleGraphViewportTexture(texture, sourceGeometry, target);
        if (!sampled) {
            pipeline.m_LastGraphExecutionStats.allocationFailed = true;
            throw std::runtime_error("Visible-region input allocation or sampling failed.");
        }
        viewportTemporaryTextures.push_back(sampled);
        viewportInputCache.emplace(localKey, sampled);
    }
    pipeline.m_Width = target.storageWidth; pipeline.m_Height = target.storageHeight;
    pipeline.m_CurrentGraphImageGeometry = target;
    pipeline.m_RawViewportAppliedRegion = target.resident.Partial() ? target.resident : ImageRegion{};
    return sampled;
}

unsigned int GraphExecutionRuntime::EvaluateViewportOutput(int nodeId, const std::string& socket,
    const std::function<unsigned int(int, const std::string&)>& evaluate) {
    if (!viewportPlan.valid) return evaluate(nodeId, socket);
    const auto consumer = viewportConsumer;
    viewportConsumer = {nodeId, socket};
    unsigned int texture = 0;
    try { texture = evaluate(nodeId, socket); }
    catch (...) { viewportConsumer = consumer; throw; }
    viewportConsumer = consumer;
    const auto key = MakeNodeSocketKey(nodeId, socket);
    if (texture && viewportGeometry.find(key) == viewportGeometry.end()) RecordViewportGeometry(nodeId, socket);
    return texture ? ViewportInput(texture, key, {nodeId, socket}, consumer) : 0;
}

void GraphExecutionRuntime::BindViewportEvaluation() {
    if (!viewportPlan.valid) return;
    const auto images = evalImage, masks = evalMask;
    evalImage = [this, images](int id, const std::string& socket) { return EvaluateViewportOutput(id, socket, images); };
    evalMask = [this, masks](int id, const std::string& socket) { return EvaluateViewportOutput(id, socket, masks); };
    const auto scalars = evalScalar;
    evalScalar = [this, scalars](int id, const std::string& socket, double& value) {
        const auto parent = viewportConsumer; viewportConsumer = {id, socket};
        try { const bool result = scalars(id, socket, value); viewportConsumer = parent; return result; }
        catch (...) { viewportConsumer = parent; throw; }
    };
}
}
