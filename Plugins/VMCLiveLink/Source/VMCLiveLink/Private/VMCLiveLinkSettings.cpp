// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSettings.h"
#include "VMCLiveLinkRemapper.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "VMCLog.h"

namespace VMCLiveLinkSettings
{
    /** Live Link's (LiveLinkTimedDataInput.cpp): the frames of delay its smooth offset adds. */
    const TCHAR* SmoothOffsetVariable = TEXT("LiveLink.TimedDataInput.NumFramesForSmoothOffset");
    /** The setting has set the variable (at project-setting priority) and not yet unset it. */
    bool bApplied = false;
}

UVMCLiveLinkSettings::UVMCLiveLinkSettings()
{
    // helps where it appears in the Settings tree (optional)
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("VMC Live Link");

    // Sensible defaults
    DefaultRemapperClass = UVMCLiveLinkRemapper::StaticClass();
    // DefaultReferenceSkeleton left unset; project can configure it.
}

void UVMCLiveLinkSettings::ApplyLiveLinkSmoothing() const
{
    using namespace VMCLiveLinkSettings;
    IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(SmoothOffsetVariable);
    if (!Variable)
    {
        if (bFixedLiveLinkDelay)
        {
            UE_LOG(LogVMCLiveLink, Warning, TEXT("Fixed Live Link Delay: Live Link has no %s; nothing changed."), SmoothOffsetVariable);
        }
        return;
    }
    // Project-setting priority: a value from an ini file or the console still wins. Don't try to set
    // it then (the engine would warn that the set was ignored).
    if (bFixedLiveLinkDelay && !bApplied)
    {
        if ((Variable->GetFlags() & ECVF_SetByMask) > ECVF_SetByProjectSetting)
        {
            UE_LOG(LogVMCLiveLink, Log, TEXT("Fixed Live Link Delay: %s is set elsewhere (an ini file or the console) to %g; that value stays."),
                SmoothOffsetVariable, Variable->GetFloat());
            return;
        }
        Variable->Set(0.f, ECVF_SetByProjectSetting);
        bApplied = true;
        UE_LOG(LogVMCLiveLink, Log, TEXT("Fixed Live Link Delay: %s is now %g."), SmoothOffsetVariable, Variable->GetFloat());
    }
    else if (!bFixedLiveLinkDelay && bApplied)
    {
        // Removes this setting's value: the variable goes back to whatever set it before.
        Variable->Unset(ECVF_SetByProjectSetting);
        bApplied = false;
        UE_LOG(LogVMCLiveLink, Log, TEXT("Fixed Live Link Delay off: %s is now %g."), SmoothOffsetVariable, Variable->GetFloat());
    }
}

#if WITH_EDITOR
void UVMCLiveLinkSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVMCLiveLinkSettings, bFixedLiveLinkDelay))
    {
        ApplyLiveLinkSmoothing();
    }
}
#endif
