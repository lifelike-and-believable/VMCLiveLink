// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for morph target import (P4.6): unnamed targets stay per mesh, NORMAL deltas are read, and
// a name shared by two meshes is reported. Uses the morph_targets fixture (scripts/make_vrm_fixtures.py).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Templates/Function.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMMorphTargetsTest, "VRM.MorphTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMMorphTargetsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
	if (!TestTrue(TEXT("VRMInterchange plugin found"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), TEXT("morph_targets.vrm"));
	FString Error;
	const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, Error);
	if (!TestTrue(FString::Printf(TEXT("morph_targets loads (%s)"), *Error), Document.IsValid()))
	{
		return false;
	}

	// Face and Hair both have a target named "Shared": merged, with one warning.
	AddExpectedError(TEXT("morph target named 'Shared'"), EAutomationExpectedErrorFlags::Contains, 1);
	FVRMParsedModel Model;
	if (!TestTrue(TEXT("Model builds"), VRM::BuildParsedModel(*Document, Model)))
	{
		return false;
	}
	// Vertices 0-5 are Face's (0-2 at Hips, 3-5 at Head), 6-8 are Hair's.
	if (!TestEqual(TEXT("Vertices"), Model.Mesh.Positions.Num(), 9))
	{
		return false;
	}

	auto Find = [&Model](const TCHAR* Name)
	{
		return Model.Mesh.Morphs.FindByPredicate([Name](const FVRMParsedMorph& Morph) { return Morph.Name == Name; });
	};
	const FVRMParsedMorph* Shared = Find(TEXT("Shared"));
	const FVRMParsedMorph* FaceUnnamed = Find(TEXT("Face_morph_1"));
	const FVRMParsedMorph* HairUnnamed = Find(TEXT("Hair_morph_1"));
	TestEqual(TEXT("Morph targets"), Model.Mesh.Morphs.Num(), 3);
	TestNull(TEXT("Unnamed targets are not merged under an index name"), Find(TEXT("morph_1")));
	if (!TestNotNull(TEXT("Shared"), Shared) || !TestNotNull(TEXT("Face_morph_1"), FaceUnnamed) || !TestNotNull(TEXT("Hair_morph_1"), HairUnnamed))
	{
		return false;
	}

	// Expression binds resolve through each mesh's own names (mesh 0 is Face, mesh 1 is Hair).
	TestEqual(TEXT("Face's target names"), Model.MeshMorphNames.FindRef(0), TArray<FString>{ TEXT("Shared"), TEXT("Face_morph_1") });
	TestEqual(TEXT("Hair's target names"), Model.MeshMorphNames.FindRef(1), TArray<FString>{ TEXT("Shared"), TEXT("Hair_morph_1") });

	auto ToUE = [&Model](const FVector& Gltf, float Scale) { return VRM::GltfPositionToUE(Gltf, Scale, Model.Version); };
	auto TestDeltas = [this, &Model](const TCHAR* Name, const FVRMParsedMorph& Morph, const FVector& Moved, TFunctionRef<bool(int32)> IsMoved)
	{
		if (!TestEqual(FString::Printf(TEXT("%s: one delta per vertex"), Name), Morph.DeltaPositions.Num(), Model.Mesh.Positions.Num()))
		{
			return;
		}
		for (int32 V = 0; V < Morph.DeltaPositions.Num(); ++V)
		{
			TestEqual(*FString::Printf(TEXT("%s: vertex %d"), Name, V), FVector(Morph.DeltaPositions[V]), IsMoved(V) ? Moved : FVector::ZeroVector, 1e-3f);
		}
	};
	const float Scale = Model.GlobalScale;
	TestDeltas(TEXT("Shared"), *Shared, ToUE(FVector(0, 0.01, 0), Scale), [](int32 V) { return V >= 3; });
	TestDeltas(TEXT("Face_morph_1"), *FaceUnnamed, ToUE(FVector(0.01, 0, 0), Scale), [](int32 V) { return V < 3; });
	TestDeltas(TEXT("Hair_morph_1"), *HairUnnamed, ToUE(FVector(-0.01, 0, 0), Scale), [](int32 V) { return V >= 6; });

	// Shared's NORMAL deltas turn Face's head normals from glTF +Z to +Y; other vertices keep theirs.
	if (TestEqual(TEXT("Shared has normal deltas"), Shared->DeltaNormals.Num(), Model.Mesh.Normals.Num()))
	{
		const FVector Up = ToUE(FVector(0, 1, 0), 1.f).GetSafeNormal();
		for (int32 V = 0; V < Shared->DeltaNormals.Num(); ++V)
		{
			const FVector Base(Model.Mesh.Normals[V]);
			const FVector Morphed = (Base + FVector(Shared->DeltaNormals[V])).GetSafeNormal();
			TestEqual(*FString::Printf(TEXT("Shared normal, vertex %d"), V), Morphed, (V >= 3 && V < 6) ? Up : Base, 1e-4f);
		}
	}
	TestEqual(TEXT("No normal deltas without NORMAL in the file"), FaceUnnamed->DeltaNormals.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
