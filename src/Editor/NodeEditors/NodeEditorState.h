#pragma once
#include "Project/NodeEditTarget.h"
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Stack::Editor {
// View state only. No authored values or undo history belong in this store.
class NodeEditorState {
    struct Value { virtual ~Value() = default; virtual void EndInteraction() = 0; };
    template<class T> struct TypedValue : Value {
        T data;
        template<class U> static auto End(U& value,int) -> decltype(value.EndInteraction(),void()) { value.EndInteraction(); }
        template<class U> static void End(U&,long) {}
        void EndInteraction() override { End(data,0); }
    };
    using Values = std::unordered_map<std::type_index,std::unique_ptr<Value>>;
    std::unordered_map<std::string,Values> m_Instances;
    std::string m_Document;
public:
    // Call during the editor's GL shutdown, before its context is released.
    void Clear() { m_Instances.clear(); m_Document.clear(); }
    // Owner transitions end gestures without erasing selection, caches or values.
    // State types opt in with their own EndInteraction() implementation.
    void EndInteraction(const Project::NodeEditTarget& target) {
        if (m_Document!=target.documentId) return;
        const auto found=m_Instances.find(target.Key());
        if (found!=m_Instances.end()) for (auto& value:found->second) value.second->EndInteraction();
    }
    template<class T> T& Get(const Project::NodeEditTarget& target) {
        if (m_Document != target.documentId) { m_Instances.clear(); m_Document = target.documentId; }
        auto& value = m_Instances[target.Key()][typeid(T)];
        if (!value) value = std::make_unique<TypedValue<T>>();
        return static_cast<TypedValue<T>&>(*value).data;
    }
};
}
