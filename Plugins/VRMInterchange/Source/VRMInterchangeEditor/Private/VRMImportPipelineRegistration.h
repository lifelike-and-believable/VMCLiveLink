// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

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
}
