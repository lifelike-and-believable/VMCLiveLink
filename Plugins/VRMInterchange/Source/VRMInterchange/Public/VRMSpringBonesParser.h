// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

namespace VRM
{
    // Parse from a top-level JSON string (GLB chunk or .gltf text)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError);

    // Convenience: read file (.vrm/.glb/.gltf), extract top-level JSON, parse
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError);

    // New overloads that also produce a node index -> bone name map (glTF node names)
    VRMINTERCHANGE_API bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError);
    // Also returns the node parent/children graph (currently left empty; see P1.12)
    VRMINTERCHANGE_API bool ParseSpringBonesFromFile(const FString& Filename,FVRMSpringConfig& OutConfig,TMap<int32, FName>& OutNodeMap,TMap<int32, int32>& OutNodeParent,TMap<int32, FVRMNodeChildren>& OutNodeChildren,FString& OutError);
}
