// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMPipelineTargets.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace VRMPipeline
{
	static FString NormalizedFile(const FString& Filename)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(Full);
		return Full;
	}

	bool IsUnderPath(const FString& PackagePath, const FString& Root)
	{
		if (Root.IsEmpty())
		{
			return false;
		}
		FString Base = Root;
		Base.RemoveFromEnd(TEXT("/"));
		return PackagePath.Equals(Base, ESearchCase::IgnoreCase) || PackagePath.StartsWith(Base + TEXT("/"), ESearchCase::IgnoreCase);
	}

	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath)
	{
		const FString BaseName = FPaths::GetBaseFilename(SourceFilename);
		return !ContentBasePath.IsEmpty() ? (ContentBasePath / BaseName) : FString::Printf(TEXT("/Game/%s"), *BaseName);
	}

	bool MeshBelongsToImport(const USkeletalMesh* Mesh, const FString& SourceFilename, const FString& CharacterBasePath)
	{
		if (!Mesh)
		{
			return false;
		}
#if WITH_EDITORONLY_DATA
		if (const UAssetImportData* ImportData = Mesh->GetAssetImportData())
		{
			const FString MeshSource = ImportData->GetFirstFilename();
			if (!MeshSource.IsEmpty())
			{
				return NormalizedFile(MeshSource).Equals(NormalizedFile(SourceFilename), ESearchCase::IgnoreCase);
			}
		}
#endif
		return IsUnderPath(Mesh->GetOutermost()->GetName(), CharacterBasePath);
	}

	USkeletalMesh* ResolveImportedMesh(UObject* CreatedObject, const FString& SourceFilename, const FString& CharacterBasePath, const FString& ContentBasePath)
	{
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(CreatedObject))
		{
			return MeshBelongsToImport(Mesh, SourceFilename, CharacterBasePath) ? Mesh : nullptr;
		}

		const USkeleton* Skeleton = Cast<USkeleton>(CreatedObject);
		if (!Skeleton)
		{
			return nullptr;
		}

		// A skeleton was reported first: look for this import's mesh using it, in the character folder
		// (and below) or directly in the content folder. Never in sibling folders.
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		auto Search = [&](const FString& Folder, bool bRecursive) -> USkeletalMesh*
		{
			if (Folder.IsEmpty())
			{
				return nullptr;
			}
			FARFilter Filter;
			Filter.bRecursivePaths = bRecursive;
			Filter.PackagePaths.Add(*Folder);
			Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
			TArray<FAssetData> Found;
			Registry.GetAssets(Filter, Found);
			for (const FAssetData& Asset : Found)
			{
				USkeletalMesh* Candidate = Cast<USkeletalMesh>(Asset.GetAsset());
				if (Candidate && Candidate->GetSkeleton() == Skeleton && MeshBelongsToImport(Candidate, SourceFilename, CharacterBasePath))
				{
					return Candidate;
				}
			}
			return nullptr;
		};
		if (USkeletalMesh* InCharacterFolder = Search(CharacterBasePath, true))
		{
			return InCharacterFolder;
		}
		return Search(ContentBasePath, false);
	}
}
