// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "VRMDocument.h"

// VRMCore's own access to a document's cgltf data (cgltf is private to this module).
struct FVRMDocumentAccess
{
	/** Null if the document has no geometry (FVRMDocument::HasGeometry). */
	static const cgltf_data* Gltf(const FVRMDocument& Document);
};
