// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VRMInterchangeSettings.generated.h"


/**
 * Project Settings > Plugins > VRM Interchange: which assets VRM imports generate, and the prompt to
 * register the import pipelines. The pipelines read these as their defaults; each setting's tooltip
 * says what it does. Game thread.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VRM Interchange"))
class VRMINTERCHANGEEDITOR_API UVRMInterchangeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
	/** Sets the defaults (config values then override them). */
	UVRMInterchangeSettings();

    // Spring Bones ---------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Parse and generate spring bone data assets during import."))
    bool bGenerateSpringBoneData = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Make a Post-Process AnimBlueprint that runs the springs."))
    bool bGeneratePostProcessAnimBP = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Assign the generated Post-Process AnimBlueprint to the imported skeletal mesh, so the springs run wherever the mesh is used."))
    bool bAssignPostProcessABP = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, an existing spring data asset with the same name is updated in place, so what refers to it keeps working. If false, the new one gets a unique name."))
    bool bOverwriteExistingSpringAssets = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, an existing Post-Process AnimBlueprint with the same name is reused and given the new spring data. If false, a new one is created with a unique name."))
    bool bOverwriteExistingPostProcessABP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, attempt to reuse an existing Post-Process AnimBP when re-importing. If false, the importer will offer to overwrite or create a new ABP."))
    bool bReusePostProcessABPOnReimport = true;

    // IK Rig ---------------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="IK Rig", meta=(ToolTip="Generate an IK Rig (UIKRigDefinition) from a template for each imported character."))
    bool bGenerateIKRigAssets = true;

    // Live Link / Character Scaffold --------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Live Link", meta=(DisplayName="Generate Live Link Actor Scaffold", ToolTip="Generate a Live Link enabled Character Actor BP + AnimBP scaffold inside <Character>/LiveLink/."))
    bool bGenerateLiveLinkEnabledActor = true;

    // Avatar description ----------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Avatar Description", meta=(ToolTip="Make a VRM avatar description asset (humanoid map, expressions, licence) next to each imported character."))
    bool bGenerateAvatarDescription = true;

    // Import pipelines -------------------------------------------------------------
    UPROPERTY(EditAnywhere, config, Category="Import Pipelines", meta=(ToolTip="When the VRM import pipelines are not registered in the Interchange project settings, offer to register them when the editor starts."))
    bool bPromptToRegisterImportPipelines = true;

    /**
     * Adds the VRM import pipelines (spring bones, IK Rig, Live Link, materials, avatar description) to the VRM
     * translator's pipeline list in Project Settings > Interchange, and saves those settings.
     */
    UFUNCTION(CallInEditor, Category="Import Pipelines", meta=(DisplayName="Register VRM Import Pipelines"))
    void RegisterImportPipelines();

    /** Applies an edited setting to the pipeline assets already loaded, so the next import uses it. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    /** Shown under Plugins in Project Settings. */
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};