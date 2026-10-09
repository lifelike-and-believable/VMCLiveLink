// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Modules/ModuleManager.h"
#include "VMCLog.h"
#include "VMCLiveLinkSettings.h"

class FVMCLiveLinkModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMCLiveLink runtime module started"));
        // Live Link (a dependency) is loaded, so its console variables exist.
        GetDefault<UVMCLiveLinkSettings>()->ApplyLiveLinkSmoothing();
    }
    virtual void ShutdownModule() override
    {
        UE_LOG(LogVMCLiveLink, Log, TEXT("VMCLiveLink runtime module shutdown"));
    }
};

IMPLEMENT_MODULE(FVMCLiveLinkModule, VMCLiveLink)
