#pragma once
#include "Project/NodeEditTarget.h"
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace Stack::Editor {
// View state only. No authored values or undo history belong in this store.
class NodeEditorState {
    struct Value { virtual ~Value() = default; };
    template<class T> struct TypedValue : Value { T data; };
    using Values = std::unordered_map<std::type_index,std::unique_ptr<Value>>;
    std::unordered_map<std::string,Values> m_Instances;
    std::string m_Document;
public:
    // Call during the editor's GL shutdown, before its context is released.
    void Clear() { m_Instances.clear(); m_Document.clear(); }
    template<class T> T& Get(const Project::NodeEditTarget& target) {
        if (m_Document != target.documentId) { m_Instances.clear(); m_Document = target.documentId; }
        auto& value = m_Instances[target.Key()][typeid(T)];
        if (!value) value = std::make_unique<TypedValue<T>>();
        return static_cast<TypedValue<T>&>(*value).data;
    }
};
}
