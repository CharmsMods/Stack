#pragma once

// Interface for high-level application modules (Editor, Library, etc.)
class IAppModule {
public:
    virtual ~IAppModule() = default;
    virtual void Initialize() = 0;
    virtual void RenderUI() = 0;
    virtual const char* GetName() = 0;
};

