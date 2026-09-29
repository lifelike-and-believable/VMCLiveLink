// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once
#include "Engine/DeveloperSettings.h"
#include "VMCLiveLinkSettings.generated.h"

class ULiveLinkSubjectRemapper;
class UVMCLiveLinkRemapper;
class USkeletalMesh;

/** Project Settings > Plugins > VMC Live Link: defaults for new VMC subjects. Game thread. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "VMC Live Link"))
class UVMCLiveLinkSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** Sets DefaultRemapperClass to UVMCLiveLinkRemapper. */
    UVMCLiveLinkSettings();

    /** The remapper class a new VMC subject gets (a class, not an asset). Default: UVMCLiveLinkRemapper. */
    UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (AllowAbstract = "false"))
    TSoftClassPtr<ULiveLinkSubjectRemapper> DefaultRemapperClass;

    /** Reference skeleton used by remappers that have none of their own: for the Live Mapping table,
     *  rest translations and Map Bones From Humanoid Metadata. */
    UPROPERTY(EditAnywhere, Config, Category = "Defaults")
    TSoftObjectPtr<USkeletalMesh> DefaultReferenceSkeleton;
};
