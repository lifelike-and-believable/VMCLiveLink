// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkMappingAsset.h"
#include "Misc/Crc.h"

namespace
{
	FString NormalizeBoneName(FString S)
	{
		S = S.ToLower();
		S.ReplaceInline(TEXT("_"), TEXT(""));
		S.ReplaceInline(TEXT("-"), TEXT(""));
		return S;
	}
}

uint32 UVMCLiveLinkMappingAsset::ComputeSignature(TConstArrayView<FName> BoneNames)
{
	if (BoneNames.Num() == 0)
	{
		return 0u;
	}
	TArray<FString> Normalized;
	Normalized.Reserve(BoneNames.Num());
	for (const FName& Name : BoneNames)
	{
		Normalized.Add(NormalizeBoneName(Name.ToString()));
	}
	Normalized.Sort();
	// One string, names separated by '|', so the CRC is over a well-defined byte sequence.
	const FString Joined = FString::Printf(TEXT("%d|%s"), Normalized.Num(), *FString::Join(Normalized, TEXT("|")));
	const FTCHARToUTF8 Utf8(*Joined);
	const uint32 Signature = FCrc::MemCrc32(Utf8.Get(), Utf8.Length());
	return Signature != 0u ? Signature : 1u; // 0 means "no signature"
}

uint32 UVMCLiveLinkMappingAsset::ComputeSignature(const USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return 0u;
	}
	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	TArray<FName> Names;
	Names.Reserve(RefSkel.GetNum());
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		Names.Add(RefSkel.GetBoneName(i));
	}
	return ComputeSignature(Names);
}

FString UVMCLiveLinkMappingAsset::SignatureToTag(uint32 Signature)
{
	return FString::Printf(TEXT("%08x"), Signature);
}

void UVMCLiveLinkMappingAsset::RefreshSignatureTag()
{
	TArray<FString> Parts;
	for (uint32 Signature : SkeletonSignatures)
	{
		Parts.Add(SignatureToTag(Signature));
	}
	SignatureTag = Parts.Num() > 0 ? FString::Printf(TEXT(";%s;"), *FString::Join(Parts, TEXT(";"))) : FString();
}

void UVMCLiveLinkMappingAsset::CaptureSignatureFrom(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}
	Modify();
	UpdateSignaturesIfOld();
	const uint32 Signature = ComputeSignature(Mesh);
	if (Signature != 0u)
	{
		SkeletonSignatures.AddUnique(Signature);
	}
	// Keep an example mesh reference too; helps user readability and alternate matching
	ExampleReferenceMeshes.AddUnique(Mesh);
	SignatureVersion = CurrentSignatureVersion;
	RefreshSignatureTag();
}

void UVMCLiveLinkMappingAsset::UpdateSignaturesIfOld()
{
	if (SignatureVersion >= CurrentSignatureVersion)
	{
		return;
	}
	// Signatures from before version 2 used a different hash; rebuild them from the example meshes.
	SkeletonSignatures.Reset();
	for (const TSoftObjectPtr<USkeletalMesh>& Soft : ExampleReferenceMeshes)
	{
		if (const USkeletalMesh* Mesh = Soft.LoadSynchronous())
		{
			SkeletonSignatures.AddUnique(ComputeSignature(Mesh));
		}
	}
	SignatureVersion = CurrentSignatureVersion;
	RefreshSignatureTag();
}

void UVMCLiveLinkMappingAsset::PostLoad()
{
	Super::PostLoad();
	// Loading other assets from PostLoad isn't safe, so old signatures are rebuilt the first time
	// the asset is matched (MatchesMesh) or edited. Until then its tag doesn't list them, and
	// auto-detect loads it to check.
	if (SignatureVersion >= CurrentSignatureVersion)
	{
		RefreshSignatureTag();
	}
}

#if WITH_EDITOR
void UVMCLiveLinkMappingAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	UpdateSignaturesIfOld();
	RefreshSignatureTag();
}
#endif

bool UVMCLiveLinkMappingAsset::MatchesMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}
	// Direct references win
	for (const TSoftObjectPtr<USkeletalMesh>& Soft : ExampleReferenceMeshes)
	{
		if (Soft.ToSoftObjectPath() == FSoftObjectPath(Mesh))
		{
			return true;
		}
	}
	UpdateSignaturesIfOld();
	const uint32 Signature = ComputeSignature(Mesh);
	return Signature != 0u && SkeletonSignatures.Contains(Signature);
}
