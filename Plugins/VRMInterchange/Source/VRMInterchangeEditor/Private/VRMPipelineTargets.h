// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Where the VRM post-import pipelines put a character's generated assets (PE-02).
 *
 * The pipelines no longer look for "their" skeletal mesh: Interchange hands each pipeline the
 * assets its own import created (UVRMPipelineBase), so these only name folders.
 */
namespace VRMPipeline
{
	/** True when PackagePath is Root or inside it: "/Game/Alice" holds "/Game/Alice/SK", not "/Game/Alice2". */
	bool IsUnderPath(const FString& PackagePath, const FString& Root);

	/** Folder for a character's generated assets: <ContentBasePath>/<source file base name>, or /Game/<name>. */
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath);
}
