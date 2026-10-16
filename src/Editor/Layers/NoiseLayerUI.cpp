#include "NoiseLayer.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <cstdint>
#include <algorithm>

NodeSurfaceSpec NoiseLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    if (m_Type == 13) {
        spec.inlineWidth = 720.f;
        spec.maxWidth = 720.f;
    }
    return spec;
}

void NoiseLayer::RenderUI(){
    const char* types[]={"Color White","Grayscale White","Blend (Sat)","Blue Noise","Blue Noise (Color)",
        "Perlin (Cloudy)","Worley (Cellular)","Scanlines","Speckle (Dust)","Glitch","Anisotropic (Fiber)",
        "Legacy White (type 11)","Legacy White (type 12)","Film Grain (Particles)"};
    ImGuiExtras::NodeCombo("Noise model","##NoiseType",&m_Type,types,IM_ARRAYSIZE(types));
    if(m_Type==13){
        auto& p=m_Film;
        const Stack::Renderer::FilmGrain::Parameters defaults;
        const float scale=std::max(.01f,ImGui::GetFontSize()/ImGui::GetStyle().FontSizeBase);
        const float width=std::max(1.f,ImGui::CalcItemWidth());
        const float gap=20.f*scale;
        const bool columns=width>=560.f*scale;
        const float lane=columns ? (width-gap)*.5f : width;
        const auto section=[&](const char* label){
            ImGui::Dummy(ImVec2(0,7.f*scale));
            ImGuiExtras::RichSectionLabel(label,2.f*scale);
        };
        const auto number=[&](const char* label,const char* id,float* value,float lo,float hi,
                float initial,const char* format="%.3f",const char* tip=nullptr){
            ImGuiExtras::SetNextNodeNumericDefault(initial);
            ImGuiExtras::NodeSliderFloat(label,id,value,lo,hi,format,lane);
            if(tip && ImGui::IsItemHovered())ImGui::SetTooltip("%s",tip);
        };
        const auto integer=[&](const char* label,const char* id,int* value,int lo,int hi,int initial){
            ImGuiExtras::SetNextNodeNumericDefault(initial);
            ImGuiExtras::NodeSliderInt(label,id,value,lo,hi,"%d",lane);
        };
        const auto structure=[&]{
            section("Emulsion");
            number("Density (OD)","##GrainDensity",&p.densityDeviation,0.f,.3f,defaults.densityDeviation,"%.4f",
                "Optical-density standard deviation before filtering and tonal/channel response. OD 0.1 changes transmission by a factor of 10^0.1.");
            number("Mix","##GrainMix",&p.mix,0.f,1.f,defaults.mix);
            number("Film width (mm)","##GrainFilmWidth",&p.filmWidthMm,8.f,70.f,defaults.filmWidthMm,"%.2f");
            number("Grain radius (um)","##GrainRadius",&p.radiusMicrons,.25f,25.f,defaults.radiusMicrons,"%.2f",
                "Mean of the lognormal distribution before the maximum-radius cutoff. Physical size is measured on the film plane.");
            number("Size variation","##GrainVariation",&p.radiusVariation,0.f,.8f,defaults.radiusVariation,"%.3f",
                "Coefficient of variation of the lognormal radii. Zero gives equal radii; larger values mix fine and coarse grains.");
            number("Radius limit (x)","##GrainMaxRadius",&p.maximumRadiusFactor,1.f,4.f,defaults.maximumRadiusFactor,"%.2f",
                "Maximum radius as a multiple of the nominal mean radius.");
            number("Coverage","##GrainCoverage",&p.coverage,.05f,.9f,defaults.coverage,"%.3f",
                "Expected fraction of film covered by overlapping grains. Controls particle packing and texture skew independently of strength.");
            section("Optics");
            number("Blur sigma (um)","##GrainOptics",&p.opticalBlurMicrons,0.f,20.f,defaults.opticalBlurMicrons,"%.2f");
            integer("Samples","##GrainSamples",&p.samples,4,64,defaults.samples);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Integration samples: more improve the optical/pixel integral and increase field-generation cost.");
            section("Output and realization");
            const char* encodings[]={"Auto (metadata)","Linear light","sRGB encoded"};
            const char* outputs[]={"Film density","Additive grain","Grain only"};
            ImGuiExtras::NodeCombo("Encoding","##GrainEncoding",&p.encoding,encodings,3,lane);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Auto follows declared linear or sRGB transfer. Undeclared inputs use the linear convention; choose sRGB for undeclared display-encoded inputs.");
            ImGuiExtras::NodeCombo("Output","##GrainOutput",&p.output,outputs,3,lane);
            integer("Seed","##GrainSeed",&p.seed,0,16777215,defaults.seed);
            integer("Grain frame","##GrainFrame",&p.frame,0,1000000,defaults.frame);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Deterministic realization index. Animate for moving-image grain; redraws and camera movement never advance it.");
            if(ImGui::Button("New grain seed",ImVec2(lane,0)))
                p.seed=int((std::uint32_t(p.seed)*1664525u+1013904223u)&0xffffffu);
        };
        const auto response=[&]{
            section("Color layers");
            number("Color amount","##GrainColor",&p.colorAmount,0.f,1.f,defaults.colorAmount);
            number("RGB correlation","##GrainCorrelation",&p.channelCorrelation,0.f,1.f,defaults.channelCorrelation);
            const char* radiusLabels[]={"R radius x","G radius x","B radius x"};
            const char* radiusIds[]={"##GrainRadiusR","##GrainRadiusG","##GrainRadiusB"};
            const char* strengthLabels[]={"R strength x","G strength x","B strength x"};
            const char* strengthIds[]={"##GrainStrengthR","##GrainStrengthG","##GrainStrengthB"};
            for(int i=0;i<3;++i)number(radiusLabels[i],radiusIds[i],&p.channelSize[i],.5f,2.f,1.f);
            for(int i=0;i<3;++i)number(strengthLabels[i],strengthIds[i],&p.channelStrength[i],0.f,3.f,1.f);
            section("Tonal response");
            number("Shadows","##GrainShadows",&p.shadows,0.f,3.f,defaults.shadows);
            number("Midtones","##GrainMidtones",&p.midtones,0.f,3.f,defaults.midtones);
            number("Highlights","##GrainHighlights",&p.highlights,0.f,3.f,defaults.highlights);
            number("Pivot (linear)","##GrainPivot",&p.tonalPivot,.01f,.9f,defaults.tonalPivot);
            number("Shadow floor","##GrainFloor",&p.exposureFloor,0.f,.1f,defaults.exposureFloor,"%.4f");
            ImGuiExtras::NodeCheckbox("Mean bias","##GrainCompensate",&p.compensateMean,lane);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Approximate moment compensation reduces density mode's exposure bias; it does not force the exact image mean to remain unchanged.");
        };
        // Groups share the host's measurement/interaction capture. Keep item
        // widths and wrapping inside each lane, including at low graph zoom.
        const float left=ImGui::GetCursorPosX();
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(left+lane);
        structure();
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if(columns)ImGui::SameLine(0.f,gap);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+lane);
        response();
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        p.Sanitize();
        return;
    }
    ImGuiExtras::NodeSliderFloat("Strength","##Strength",&m_Strength,0.f,150.f,"%.1f");
    const char* blendModes[]={"Normal","Overlay","Screen","Multiply","Add","Difference"};
    ImGuiExtras::NodeCombo("Blend mode","##BlendMode",&m_BlendMode,blendModes,6);
    ImGuiExtras::NodeSliderFloat("Blurriness","##Blurriness",&m_Blurriness,0.f,100.f,"%.0f");
    ImGuiExtras::NodeSliderFloat("Saturation strength","##SatStrength",&m_SatStrength,0.f,4.f,"%.2f");
    ImGuiExtras::NodeSliderFloat("Saturation impact","##SatImpact",&m_SatImpact,-100.f,100.f,"%.0f");
    ImGuiExtras::NodeSliderFloat("Scale","##Scale",&m_Scale,1.f,20.f,"%.1f");
    ImGuiExtras::NodeSliderFloat("Opacity","##Opacity",&m_Opacity,0.f,1.f,"%.2f");
    if(m_Type>=5 && m_Type<=10){
        const char* a[]={"Octave control","Point jitter","Line threshold","Dust sparsity","Block height","Fiber stretch"};
        const char* b[]={"Feature spacing","Cell spacing","Line jitter","Dust softness","Horizontal shift","Fiber angle"};
        const char* c[]={"Persistence","Distance metric","Line randomness","Dust size variation","Color separation","Fiber roughness"};
        ImGuiExtras::NodeSliderFloat(a[m_Type-5],"##ParamA",&m_ParamA,0.f,1.f,"%.3f");
        ImGuiExtras::NodeSliderFloat(b[m_Type-5],"##ParamB",&m_ParamB,0.f,1.f,"%.3f");
        ImGuiExtras::NodeSliderFloat(c[m_Type-5],"##ParamC",&m_ParamC,0.f,1.f,"%.3f");
    }
    ImGuiExtras::NodeSliderFloat("Seed","##LegacyNoiseSeed",&m_Seed,0.f,1.f,"%.6f");
    if(ImGui::Button("Randomize seed"))m_Seed=float((std::uint32_t(m_Seed*16777215.f)*1664525u+1013904223u)&0xffffffu)/16777215.f;
}
