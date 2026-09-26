// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMPipelineTargets.h"

#include "Misc/Paths.h"

namespace VRMPipeline
{
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
}
