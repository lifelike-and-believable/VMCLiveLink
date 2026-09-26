// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

class FVRMDocument;

namespace VRM
{
    // Parse from a top-level JSON string (GLB chunk or .gltf text)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError);
    // Also produces a node index -> node name map (glTF node names)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);

    // From a document that is already loaded (P3.3): the spring config, node names and the node
    // parent/children graph. What the import uses; the file is not read again.
    VRMINTERCHANGE_API bool ParseSpringBonesFromDocument(const FVRMDocument& Document, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, TMap<int32, int32>& OutNodeParent, TMap<int32, FVRMNodeChildren>& OutNodeChildren, FString& OutError);

    // Convenience: load a .vrm/.glb/.gltf file into a document and parse it as above.
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError);
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, TMap<int32, int32>& OutNodeParent, TMap<int32, FVRMNodeChildren>& OutNodeChildren, FString& OutError);
}
