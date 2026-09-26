// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UObject;
class USkeletalMesh;

/**
 * How the VRM post-import pipelines decide which imported skeletal mesh is theirs (PE-02).
 *
 * The pipelines run before the assets exist and finish in an OnAssetPostImport callback, which
 * fires for every asset any import creates. Matching on a path prefix alone let "Alice" claim
 * "Alice2"'s mesh, and a fallback search of the parent folder could pick up any character there.
 */
namespace VRMPipeline
{
	/** True when PackagePath is Root or inside it: "/Game/Alice" holds "/Game/Alice/SK", not "/Game/Alice2". */
	bool IsUnderPath(const FString& PackagePath, const FString& Root);

	/** Folder for a character's generated assets: <ContentBasePath>/<source file base name>, or /Game/<name>. */
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath);

	/**
	 * True when Mesh was imported from SourceFilename. A mesh whose import data records a source file
	 * must record this one. A mesh without one must be inside CharacterBasePath.
	 */
	bool MeshBelongsToImport(const USkeletalMesh* Mesh, const FString& SourceFilename, const FString& CharacterBasePath);

	/**
	 * The skeletal mesh this import created, given an object the import subsystem reported: the mesh
	 * itself, or its skeleton (then the mesh is looked up in the character folder and directly in
	 * ContentBasePath). Null when the object belongs to another import or the mesh doesn't exist yet.
	 */
	USkeletalMesh* ResolveImportedMesh(UObject* CreatedObject, const FString& SourceFilename, const FString& CharacterBasePath, const FString& ContentBasePath);
}
