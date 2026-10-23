#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include "Editor/LayerRegistry.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/Layers/NoiseLayer.h"
#include "NodeMath/ImageOperationInput.h"
#include "NodeMath/StylizeInput.h"

#include <functional>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

using namespace Stack::Renderer::GraphExecution;

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderLayerGraphNode(
    const GraphExecutionContext& executionContext,
    const RenderGraphNode& node,
    const std::function<unsigned int(int, const std::string&)>& evalImage,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    GraphNodeRenderResult result;
    const auto fail = [&](const char* message, bool allocationFailed = false) {
        m_LastGraphExecutionStats.allocationFailed |= allocationFailed;
        m_LastGraphExecutionStats.lastSpecializedFailureNodeId = node.nodeId;
        m_LastGraphExecutionStats.lastSpecializedFailure = message;
        return GraphNodeRenderResult{};
    };

    const RenderGraphLink* input = executionContext.FindInputLink(node.nodeId, "imageIn");
    const unsigned int inputTexture = input ? evalImage(input->fromNodeId, input->fromSocketId) : 0;
    if (inputTexture == 0 || !node.layerJson.is_object()) {
        return result;
    }
    if (!node.layerJson.value("enabled", true) || !node.layerJson.value("visible", true)) {
        result.texture = inputTexture;
        result.owned = false;
        return result;
    }
    const int inputWidth = m_Width;
    const int inputHeight = m_Height;

    const std::string type = node.layerJson.value("type", std::string());
    if (type == "RawOutputCrop")
        return RenderRawSpatialLayer(executionContext, node, inputTexture);
    const bool stylize=type=="CellShading" || type=="PaletteReconstructor" || type=="Halftoning";
    auto executionParameters=node.layerJson;
    if (stylize) {
        if (type=="Halftoning" && m_CurrentGraphImageGeometry.resident.Partial())
            return fail("Halftone requires the complete logical input grid.");
        const auto capability=Stack::NodeMath::DescribeImageOperationInput(input->semanticDescriptor);
        std::string reason;
        if (!Stack::NodeMath::ValidateStylizeInput(type,executionParameters,capability,reason)) {
            m_LastGraphExecutionStats.lastSpecializedFailureNodeId=node.nodeId;
            m_LastGraphExecutionStats.lastSpecializedFailure=reason;return {};
        }
        executionParameters["executionScalar"]=capability.scalar;
        executionParameters["executionLumaWeights"]=capability.lumaWeights;
        std::array<float,3> present{};
        for (int i=0;i<3;++i) present[i]=capability.scalar ? (i==0 ? 1.f : 0.f) : Stack::NodeMath::HasImageComponent(capability.components,
            static_cast<Stack::NodeMath::ImageComponent>(1u<<i)) ? 1.f : 0.f;
        executionParameters["executionComponents"]=present;
        const auto native=ResolveGraphNativeExtent(executionContext,input->fromNodeId,input->fromSocketId);
        executionParameters["executionSourceSize"]={native.first,native.second};
    }
    const bool blurLayer=type=="GaussianBlur" || type=="BoxBlur";
    if (blurLayer && executionParameters.value("sigmaMode", false)) {
        const auto native = ResolveGraphNativeExtent(executionContext,input->fromNodeId,input->fromSocketId);
        const double scale = native.first > 0 ? double(inputWidth)/native.first : 1.0;
        const double sigma = executionParameters.contains("sigmaFraction")
            ? executionParameters.value("sigmaFraction", 0.0015) * std::min(native.first,native.second)
            : executionParameters.value("amount", 2.0);
        if (!std::isfinite(sigma) || sigma < 0) return fail("Gaussian sigma must be finite and nonnegative.");
        if (sigma == 0) { result.texture=inputTexture; return result; }
        executionParameters["amount"] = sigma * scale;
    }
    const bool noiseLayer=type=="Noise";
    Stack::Renderer::FilmGrain::Input noiseInput;
    if(noiseLayer){
        if(m_CurrentGraphImageGeometry.resident.Partial())return fail("Film grain requires the complete logical input grid.");
        const auto capability=Stack::NodeMath::DescribeImageOperationInput(input->semanticDescriptor);
        noiseInput.scalar=capability.scalar;
        noiseInput.luma=capability.lumaWeights;
        if(input->semanticDescriptor.transfer.state==Stack::NodeMath::KnowledgeState::Known){
            const auto transfer=input->semanticDescriptor.transfer.value.kind;
            noiseInput.encodingHint=transfer==Stack::NodeMath::TransferKind::Linear ? 0 :
                transfer==Stack::NodeMath::TransferKind::Srgb ? 1 : -2;
        }
        std::array<float,3> present{};
        for(int i=0;i<3;++i)present[i]=capability.scalar ? (i==0 ? 1.f : 0.f) :
            Stack::NodeMath::HasImageComponent(capability.components,static_cast<Stack::NodeMath::ImageComponent>(1u<<i)) ? 1.f : 0.f;
        // Unspecified image component metadata keeps the existing RGB path.
        if(!capability.scalar && present==std::array<float,3>{0,0,0})present={1,1,1};
        noiseInput.components=present;
        noiseInput.premultiplied=input->semanticDescriptor.alpha.state==Stack::NodeMath::KnowledgeState::Known &&
            input->semanticDescriptor.alpha.value==Stack::NodeMath::AlphaMode::Premultiplied;
        const auto native=ResolveGraphNativeExtent(executionContext,input->fromNodeId,input->fromSocketId);
        noiseInput.nativeWidth=native.first;noiseInput.nativeHeight=native.second;
        noiseInput.shouldCancel=[this]{return m_ShouldCancelRender && m_ShouldCancelRender();};
    }
    std::shared_ptr<LayerBase> layer;
    if(blurLayer || noiseLayer) {
        // Blur has no feedback state, and graph execution is serial within
        // this renderer. Share its prepared program/kernel by effect type;
        // each node still supplies its own parameters and cached output. Noise
        // also has no feedback: retain one prepared program/field per pipeline,
        // rather than compiling per edit or retaining a native field per node.
        const auto runtimeType=noiseLayer ? LayerType::Noise : type=="GaussianBlur" ? LayerType::GaussianBlur : LayerType::BoxBlur;
        auto& instance=m_GraphLayerInstances[static_cast<int>(runtimeType)];
        if (!instance.layer || instance.type!=type) {
            instance={};instance.type=type;
            instance.layer=LayerRegistry::CreateLayerFromTypeId(type);
            if(instance.layer) {
                instance.defaultPayload=instance.layer->Serialize();
                instance.layer->InitializeGL();
            }
        }
        layer=instance.layer;
        if(layer && instance.appliedPayload!=executionParameters) {
            layer->Deserialize(instance.defaultPayload);
            layer->Deserialize(executionParameters);
            instance.appliedPayload=executionParameters;
        }
    } else if (stylize || (type=="ToneCurve" && node.definitionId=="stack:tone/curve")) {
        const auto key=node.authoredDocumentId+"/"+node.authoredGraphId+"/"+(node.authoredNodeUuid.empty() ? std::string("#")+std::to_string(node.nodeId) : node.authoredNodeUuid)+"/"+node.definitionHash+"/"+type;
        auto& runtime=m_NodeOperationInstances[key];
        runtime.lastUse=++m_GraphResourceUseSerial;
        auto& instance=runtime.instance;
        if (!instance.layer) {
            instance.layer=type=="ToneCurve" ? std::static_pointer_cast<LayerBase>(std::make_shared<ToneCurveLayer>()) : LayerRegistry::CreateLayerFromTypeId(type);
            if (!instance.layer) return fail("The operation executor is unavailable.");
            instance.defaultPayload=instance.layer->Serialize();
            instance.layer->InitializeGL();
        }
        const auto inputKey=MakeNodeSocketKey(input->fromNodeId,input->fromSocketId);
        const auto inputStamp=executionContext.imageFingerprintCache.find(inputKey);
        const auto inputFingerprint=inputStamp==executionContext.imageFingerprintCache.end() ? 0 : inputStamp->second;
        const bool pendingAuto=executionParameters.value("autoCalibratePending",false);
        const bool autoInputChanged=pendingAuto && (runtime.authoredRevision!=node.authoredRevision ||
            runtime.sourceIdentity!=node.uniformSourceIdentity || runtime.inputFingerprint!=inputFingerprint);
        if (instance.appliedPayload!=executionParameters || autoInputChanged) {
            instance.layer->Deserialize(instance.defaultPayload);
            instance.layer->Deserialize(executionParameters);
            instance.appliedPayload=executionParameters;
        }
        runtime.authoredRevision=node.authoredRevision;runtime.sourceIdentity=node.uniformSourceIdentity;
        runtime.inputFingerprint=inputFingerprint;
        layer=instance.layer;
        // Bound retained GL resources even during repeated create/delete/undo.
        if (m_NodeOperationInstances.size()>128) {
            auto oldest=m_NodeOperationInstances.end();
            for (auto it=m_NodeOperationInstances.begin();it!=m_NodeOperationInstances.end();++it)
                if (it->first!=key && (oldest==m_NodeOperationInstances.end() || it->second.lastUse<oldest->second.lastUse)) oldest=it;
            if (oldest!=m_NodeOperationInstances.end()) m_NodeOperationInstances.erase(oldest);
        }
    } else {
        // Other legacy effects can contain feedback state beyond parameters.
        // Keep their lifetime contract until each is deliberately migrated.
        layer=LayerRegistry::CreateLayerFromTypeId(type);
        if(layer) {layer->InitializeGL();layer->Deserialize(node.layerJson);}
    }
    if (!layer) {
        return result;
    }
    if(auto* noise=dynamic_cast<NoiseLayer*>(layer.get()))noise->SetFilmGrainInput(noiseInput);

    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get())) {
        toneCurve->SetAutoRewriteRenderContext(node.nodeId, node.requestRevision);
    }

    const bool scalarBlur=blurLayer && IsScalarRenderSocket(executionContext,input->fromNodeId,input->fromSocketId);
    const bool canonicalCurve=type=="ToneCurve" && node.definitionId=="stack:tone/curve";
    const bool scalarCurve=canonicalCurve && Stack::NodeMath::DescribeImageOperationInput(input->semanticDescriptor).scalar;
    const bool scalarNoise=noiseLayer && Stack::NodeMath::DescribeImageOperationInput(input->semanticDescriptor).scalar;
    const auto createTarget=[&] {
        if (scalarBlur || scalarCurve || scalarNoise) return GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_R32F);
        if (canonicalCurve) return GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RGBA32F);
        return CreateGraphRenderTargetTexture();
    };
    Stack::Renderer::ScopedGLTexture processed(createTarget());
    const unsigned int sourceTexture = m_SourceTexture != 0 ? m_SourceTexture : inputTexture;
    const bool renderedLayer = RenderIntoGraphTargetTexture(processed.Get(), [&](unsigned int) {
        layer->ExecuteWithSource(inputTexture, sourceTexture, m_Width, m_Height, m_Quad);
    });
    if (!renderedLayer || !processed) {
        return fail("Layer rendering failed; previous presentation retained.", !processed);
    }

    if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(layer.get());
        toneCurve && toneCurve->HasPendingAutoRewriteFeedback()) {
        auto feedback=toneCurve->TakePendingAutoRewriteFeedback();
        feedback.documentId=node.authoredDocumentId;feedback.graphId=node.authoredGraphId;
        feedback.nodeUuid=node.authoredNodeUuid;feedback.definitionHash=node.definitionHash;
        feedback.authoredRevision=node.authoredRevision;feedback.sourceIdentity=node.uniformSourceIdentity;
        feedback.evaluatedFrame=node.evaluatedFrame;
        const auto stamp=executionContext.imageFingerprintCache.find(MakeNodeSocketKey(input->fromNodeId,input->fromSocketId));
        feedback.inputFingerprint=stamp==executionContext.imageFingerprintCache.end() ? 0 : stamp->second;
        m_ToneCurveAutoRewriteFeedback.push_back(std::move(feedback));
    }

    if (type == "ToneCurve" || type == "ViewTransform") {
        const QuickTextureStats inputStats = ProbeTextureStats(inputTexture, m_Width, m_Height);
        const QuickTextureStats outputStats = ProbeTextureStats(processed.Get(), m_Width, m_Height);
        if (!LayerPayloadExplicitlyRequestsCollapsedOutput(node.layerJson) &&
            IsImplausiblyDamagedTextureOutput(inputStats, outputStats)) {
            return fail("Layer produced invalid output; previous presentation retained.");
        }
    }

    const RenderGraphLink* maskLink = executionContext.FindInputLink(node.nodeId, "maskIn");
    const unsigned int maskTexture = maskLink ? evalMask(maskLink->fromNodeId, maskLink->fromSocketId) : 0;
    m_Width = inputWidth;
    m_Height = inputHeight;
    if (maskLink && !maskTexture) return fail("The connected mask is unavailable; previous presentation retained.");
    if (maskTexture) {
        EnsureMaskPrograms();
        Stack::Renderer::ScopedGLTexture blended(createTarget());
        bool blendPassExecuted = false;
        const bool renderedBlend =
            m_MaskBlendProgram != 0 &&
            RenderIntoGraphTargetTexture(blended.Get(), [&](unsigned int fbo) {
                blendPassExecuted = RenderMaskBlend(
                    inputTexture, processed.Get(), maskTexture, fbo);
            }) &&
            blendPassExecuted;
        if (renderedBlend && blended) {
            result.texture = blended.Release();
            result.owned = true;
        } else {
            return fail("Layer mask blending failed; previous presentation retained.", !blended);
        }
    } else {
        result.texture = processed.Release();
        result.owned = true;
    }

    return result;
}
