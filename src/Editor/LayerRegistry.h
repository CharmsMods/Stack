#pragma once

#include "Layers/LayerBase.h"
#include "Graph/LayerIdentity.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct LayerDescriptor {
    LayerType type;
    const char* typeId;
    const char* displayName;
    const char* libraryDisplayName;
    const char* categoryName;
    const char* description;
    std::vector<const char*> legacyTypeIds;
    std::function<std::shared_ptr<LayerBase>()> create;
    LayerLifecycleStatus lifecycleStatus = LayerLifecycleStatus::Stable;
    LayerChannelPolicy channelPolicy = LayerChannelPolicy::FullImagePreferred;
    bool visibleInNodeBrowser = true;
    const char* lifecycleNote = "";
    const char* channelNote = "";
    std::vector<const char*> tags;

    LayerDescriptor(
        LayerType type,
        const char* typeId,
        const char* displayName,
        const char* libraryDisplayName,
        const char* categoryName,
        const char* description,
        std::vector<const char*> legacyTypeIds,
        std::function<std::shared_ptr<LayerBase>()> create,
        LayerLifecycleStatus lifecycleStatus = LayerLifecycleStatus::Stable,
        LayerChannelPolicy channelPolicy = LayerChannelPolicy::FullImagePreferred,
        bool visibleInNodeBrowser = true,
        const char* lifecycleNote = "",
        const char* channelNote = "",
        std::vector<const char*> tags = {})
        : type(type),
          typeId(typeId),
          displayName(displayName),
          libraryDisplayName(libraryDisplayName),
          categoryName(categoryName),
          description(description),
          legacyTypeIds(std::move(legacyTypeIds)),
          create(std::move(create)),
          lifecycleStatus(lifecycleStatus),
          channelPolicy(channelPolicy),
          visibleInNodeBrowser(visibleInNodeBrowser),
          lifecycleNote(lifecycleNote),
          channelNote(channelNote),
          tags(std::move(tags)) {}
};

namespace LayerRegistry {

std::shared_ptr<LayerBase> CreateLayer(LayerType type);
std::shared_ptr<LayerBase> CreateLayerFromTypeId(const std::string& typeId);

const LayerDescriptor* GetDescriptor(LayerType type);
const LayerDescriptor* FindDescriptorByTypeId(const std::string& typeId);
const std::vector<LayerDescriptor>& GetAllDescriptors();
std::map<std::string, std::vector<const LayerDescriptor*>> GetDescriptorsByCategory();

std::string GetDisplayNameFromTypeId(const std::string& typeId);
std::string GetLibraryDisplayNameFromTypeId(const std::string& typeId);
const char* LifecycleStatusLabel(LayerLifecycleStatus status);
const char* ChannelPolicyLabel(LayerChannelPolicy policy);
bool ShouldShowInNodeBrowser(const LayerDescriptor& descriptor);

bool ValidateRegistry(std::vector<std::string>* errors = nullptr);

} // namespace LayerRegistry
