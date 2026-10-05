// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

/**
 * Registers the VRM import pipelines in the project's Interchange settings.
 *
 * Registration only edits the per-translator pipeline list for the VRM translator (and the VRM
 * texture import dialog override). It runs only when the user asks for it; the editor module
 * never writes these settings on its own.
 */
namespace VRMImportPipelineRegistration
{
	/** True when the project's Interchange settings already contain every VRM pipeline entry. Does not modify anything. */
	bool IsUpToDate();

	/** Adds any missing VRM pipeline entries and saves the Interchange project settings. Returns true if anything changed. */
	bool Apply();

	/**
	 * The VRM translator's pipeline list as Apply() would save it, computed on a copy of the
	 * settings (nothing is modified). With SeedPipelines, the VRM translator's current list is
	 * replaced by it first, so tests can check how an older registration is migrated.
	 */
	TArray<FSoftObjectPath> PreviewVRMTranslatorPipelines(const TArray<FSoftObjectPath>* SeedPipelines = nullptr);
}
