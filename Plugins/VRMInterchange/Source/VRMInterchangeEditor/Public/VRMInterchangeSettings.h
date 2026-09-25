// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VRMInterchangeSettings.generated.h"


UCLASS(config=Game, defaultconfig, meta=(DisplayName="VRM Interchange"))
class VRMINTERCHANGEEDITOR_API UVRMInterchangeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
	UVRMInterchangeSettings();

    // Spring Bones ---------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Parse and generate spring bone data assets during import."))
    bool bGenerateSpringBoneData = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Duplicate and assign a Post-Process AnimBP to drive springs."))
    bool bGeneratePostProcessAnimBP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Assign generated Post-Process AnimBP to the imported SkeletalMesh."))
    bool bAssignPostProcessABP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, overwrite existing generated spring assets. If false, create with a suffix."))
    bool bOverwriteExistingSpringAssets = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, overwrite existing generated Post-Process AnimBlueprints. If false, create with a unique name."))
    bool bOverwriteExistingPostProcessABP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, attempt to reuse an existing Post-Process AnimBP when re-importing. If false, the importer will offer to overwrite or create a new ABP."))
    bool bReusePostProcessABPOnReimport = true;

    // IK Rig ---------------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="IK Rig", meta=(ToolTip="Generate an IK Rig (UIKRigDefinition) from a template for each imported character."))
    bool bGenerateIKRigAssets = true;

    // Live Link / Character Scaffold --------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Live Link", meta=(DisplayName="Generate Live Link Actor Scaffold", ToolTip="Generate a Live Link enabled Character Actor BP + AnimBP scaffold inside <Character>/LiveLink/."))
    bool bGenerateLiveLinkEnabledActor = true;

    // Import pipelines -------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Import Pipelines", meta=(ToolTip="When the VRM import pipelines are not registered in the Interchange project settings, offer to register them when the editor starts."))
    bool bPromptToRegisterImportPipelines = true;

    /**
     * Adds the VRM import pipelines (spring bones, IK Rig, Live Link, materials) to the VRM
     * translator's pipeline list in Project Settings > Interchange, and saves those settings.
     */
    UFUNCTION(CallInEditor, Category="Import Pipelines", meta=(DisplayName="Register VRM Import Pipelines"))
    void RegisterImportPipelines();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};