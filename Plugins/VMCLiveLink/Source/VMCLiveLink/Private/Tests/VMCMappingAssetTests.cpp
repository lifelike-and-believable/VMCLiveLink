// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VMCLiveLinkMappingAsset.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace VMCMappingAssetTests
{
	USkeletalMesh* MakeMesh(std::initializer_list<const TCHAR*> Bones)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
		{
			FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton);
			int32 Parent = INDEX_NONE;
			for (const TCHAR* Bone : Bones)
			{
				Modifier.Add(FMeshBoneInfo(Bone, Bone, Parent), FTransform::Identity);
				Parent = 0;
			}
		}
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCMappingSignatureTest, "VMC.MappingAsset.Signature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCMappingSignatureTest::RunTest(const FString& Parameters)
{
	using Asset = UVMCLiveLinkMappingAsset;
	const TArray<FName> A = { TEXT("root"), TEXT("pelvis"), TEXT("spine_01") };
	const uint32 SigA = Asset::ComputeSignature(A);
	TestNotEqual(TEXT("A signature is never 0"), SigA, 0u);
	TestEqual(TEXT("Bone order doesn't matter"), Asset::ComputeSignature(TArray<FName>{ TEXT("spine_01"), TEXT("root"), TEXT("pelvis") }), SigA);
	TestEqual(TEXT("Case, '_' and '-' don't matter"), Asset::ComputeSignature(TArray<FName>{ TEXT("Root"), TEXT("PELVIS"), TEXT("spine-01") }), SigA);
	TestNotEqual(TEXT("Another skeleton differs"), Asset::ComputeSignature(TArray<FName>{ TEXT("root"), TEXT("pelvis"), TEXT("spine_02") }), SigA);
	TestNotEqual(TEXT("An extra bone differs"), Asset::ComputeSignature(TArray<FName>{ TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("neck") }), SigA);
	TestEqual(TEXT("No bones, no signature"), Asset::ComputeSignature(TArray<FName>()), 0u);
	TestEqual(TEXT("Tag text is 8 hex digits"), Asset::SignatureToTag(0x00ab12cdu), FString(TEXT("00ab12cd")));

	// Capturing a mesh records its signature, lists it in the searchable tag and matches it.
	USkeletalMesh* Mesh = VMCMappingAssetTests::MakeMesh({ TEXT("root"), TEXT("pelvis"), TEXT("spine_01") });
	TestEqual(TEXT("A mesh signs like its bone names"), Asset::ComputeSignature(Mesh), SigA);
	UVMCLiveLinkMappingAsset* Mapping = NewObject<UVMCLiveLinkMappingAsset>();
	Mapping->CaptureSignatureFrom(Mesh);
	TestTrue(TEXT("Signature recorded"), Mapping->SkeletonSignatures.Contains(SigA));
	TestTrue(TEXT("Tag lists it"), Mapping->SignatureTag.Contains(FString::Printf(TEXT(";%s;"), *Asset::SignatureToTag(SigA))));
	TestEqual(TEXT("Current version"), Mapping->SignatureVersion, Asset::CurrentSignatureVersion);
	TestTrue(TEXT("Matches the mesh"), Mapping->MatchesMesh(Mesh));
	TestTrue(TEXT("Matches another mesh with the same bones"), Mapping->MatchesMesh(VMCMappingAssetTests::MakeMesh({ TEXT("Root"), TEXT("Pelvis"), TEXT("Spine_01") })));
	TestFalse(TEXT("Doesn't match a different skeleton"), Mapping->MatchesMesh(VMCMappingAssetTests::MakeMesh({ TEXT("root"), TEXT("hips") })));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVMCMappingOldSignatureTest, "VMC.MappingAsset.OldSignatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FVMCMappingOldSignatureTest::RunTest(const FString& Parameters)
{
	// An asset saved before signature version 2 has hashes the new code can't reproduce; they are
	// rebuilt from its example meshes.
	USkeletalMesh* Mesh = VMCMappingAssetTests::MakeMesh({ TEXT("root"), TEXT("pelvis") });
	UVMCLiveLinkMappingAsset* Old = NewObject<UVMCLiveLinkMappingAsset>();
	Old->SignatureVersion = 0;
	Old->SkeletonSignatures = { 12345u };
	Old->ExampleReferenceMeshes.Add(Mesh);

	Old->UpdateSignaturesIfOld();
	TestEqual(TEXT("Version updated"), Old->SignatureVersion, UVMCLiveLinkMappingAsset::CurrentSignatureVersion);
	TestTrue(TEXT("Old hash dropped"), !Old->SkeletonSignatures.Contains(12345u));
	TestTrue(TEXT("Rebuilt from the example mesh"), Old->SkeletonSignatures.Contains(UVMCLiveLinkMappingAsset::ComputeSignature(Mesh)));
	TestFalse(TEXT("Tag filled in"), Old->SignatureTag.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
