// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for spring data versioning (P1.16): old assets are flagged for reimport, new ones aren't.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBoneData.h"
#include "VRMSpringDataCustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace VRMSpringDataVersionTests
{
	UVRMSpringBoneData* MakeSpringData()
	{
		UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>();
		FVRMSpringCollider Collider;
		Collider.BoneName = TEXT("Head");
		FVRMSpringColliderSphere Sphere;
		Sphere.Offset = FVector(0, 0, 10);
		Sphere.Radius = 8.f;
		Collider.Spheres.Add(Sphere);
		Data->SpringConfig.Colliders.Add(Collider);
		Data->SourceFilename = TEXT("C:/Avatars/Test.vrm");
		return Data;
	}

	TArray<uint8> Save(UVRMSpringBoneData* Data)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Ar(Writer, /*bInLoadIfFindFails*/ false);
		Data->Serialize(Ar);
		return Bytes;
	}

	/** Loads Bytes as if they had been saved at DataVersion, then runs PostLoad like the linker does. */
	UVRMSpringBoneData* Load(const TArray<uint8>& Bytes, int32 DataVersion)
	{
		UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>();
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		Reader.SetCustomVersion(FVRMSpringDataCustomVersion::GUID, DataVersion, TEXT("VRMSpringDataVer"));
		FObjectAndNameAsStringProxyArchive Ar(Reader, /*bInLoadIfFindFails*/ false);
		Ar.SetCustomVersion(FVRMSpringDataCustomVersion::GUID, DataVersion, TEXT("VRMSpringDataVer"));
		Data->Serialize(Ar);
		Data->PostLoad();
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringDataVersionRule, "VRM.SpringBones.DataVersion.Rule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringDataVersionRule::RunTest(const FString& Parameters)
{
	FVRMSpringConfig Empty;
	FVRMSpringConfig WithCollider;
	WithCollider.Colliders.AddDefaulted();
	FVRMSpringConfig WithSpring;
	WithSpring.Springs.AddDefaulted();

	TestTrue(TEXT("Colliders saved before the version existed need a reimport"),
		UVRMSpringBoneData::RequiresReimport(FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded, WithCollider));
	TestTrue(TEXT("Springs (gravity) saved before the version existed need a reimport"),
		UVRMSpringBoneData::RequiresReimport(FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded, WithSpring));
	TestFalse(TEXT("Empty data never needs a reimport"),
		UVRMSpringBoneData::RequiresReimport(FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded, Empty));
	TestFalse(TEXT("Data at the converted-axes version doesn't need a reimport"),
		UVRMSpringBoneData::RequiresReimport(FVRMSpringDataCustomVersion::ConvertedColliderAxes, WithCollider));
	TestFalse(TEXT("Data at the latest version doesn't need a reimport"),
		UVRMSpringBoneData::RequiresReimport(FVRMSpringDataCustomVersion::LatestVersion, WithCollider));

	const UVRMSpringBoneData* Fresh = NewObject<UVRMSpringBoneData>();
	TestEqual(TEXT("A new asset is at the latest version"), Fresh->GetLoadedDataVersion(),
		static_cast<int32>(FVRMSpringDataCustomVersion::LatestVersion));
	TestFalse(TEXT("A new asset isn't flagged"), Fresh->bNeedsReimport);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringDataVersionLoad, "VRM.SpringBones.DataVersion.Load",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringDataVersionLoad::RunTest(const FString& Parameters)
{
	using namespace VRMSpringDataVersionTests;
	const TArray<uint8> Bytes = Save(MakeSpringData());

	AddExpectedError(TEXT("holds spring data from an older VRMInterchange version"), EAutomationExpectedErrorFlags::Contains, 1);
	const UVRMSpringBoneData* Old = Load(Bytes, FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded);
	TestEqual(TEXT("Old asset reports the version it was saved with"), Old->GetLoadedDataVersion(),
		static_cast<int32>(FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded));
	TestTrue(TEXT("Old asset is flagged for reimport"), Old->bNeedsReimport);
	TestEqual(TEXT("Old asset keeps its data"), Old->SpringConfig.Colliders.Num(), 1);
	TestEqual(TEXT("Old asset keeps its source file"), Old->SourceFilename, FString(TEXT("C:/Avatars/Test.vrm")));

	const UVRMSpringBoneData* Current = Load(Bytes, FVRMSpringDataCustomVersion::LatestVersion);
	TestEqual(TEXT("Current asset reports the latest version"), Current->GetLoadedDataVersion(),
		static_cast<int32>(FVRMSpringDataCustomVersion::LatestVersion));
	TestFalse(TEXT("Current asset isn't flagged"), Current->bNeedsReimport);
	if (TestEqual(TEXT("Current asset keeps its colliders"), Current->SpringConfig.Colliders.Num(), 1)
		&& TestEqual(TEXT("Current asset keeps its spheres"), Current->SpringConfig.Colliders[0].Spheres.Num(), 1))
	{
		TestEqual(TEXT("Collider offset round-trips"), Current->SpringConfig.Colliders[0].Spheres[0].Offset, FVector(0, 0, 10));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringDataVersionResave, "VRM.SpringBones.DataVersion.Resave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringDataVersionResave::RunTest(const FString& Parameters)
{
	using namespace VRMSpringDataVersionTests;

	// Saving an old asset stamps the latest version on it, but its data is still old, so the
	// flag has to survive the next load.
	AddExpectedError(TEXT("holds spring data from an older VRMInterchange version"), EAutomationExpectedErrorFlags::Contains, 2);
	UVRMSpringBoneData* Old = Load(Save(MakeSpringData()), FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded);
	TestTrue(TEXT("Old asset is flagged"), Old->bNeedsReimport);

	const UVRMSpringBoneData* Resaved = Load(Save(Old), FVRMSpringDataCustomVersion::LatestVersion);
	TestTrue(TEXT("Re-saved old asset is still flagged"), Resaved->bNeedsReimport);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringDataVersionPerJoint, "VRM.SpringBones.DataVersion.PerJointParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringDataVersionPerJoint::RunTest(const FString& Parameters)
{
	using namespace VRMSpringDataVersionTests;

	// Before PerJointParameters the solver read the spring's parameters. Loading such data must give
	// every joint its spring's values, so the asset behaves as it did.
	UVRMSpringBoneData* Data = NewObject<UVRMSpringBoneData>();
	FVRMSpring Spring;
	Spring.Stiffness = 0.3f;
	Spring.Drag = 0.7f;
	Spring.GravityDir = FVector(1, 0, 0);
	Spring.GravityPower = 20.f;
	Spring.HitRadius = 4.f;
	Spring.JointIndices = { 0, 1 };
	Data->SpringConfig.Springs.Add(Spring);
	Data->SpringConfig.Joints.AddDefaulted(2);
	const TArray<uint8> Bytes = Save(Data);

	const UVRMSpringBoneData* Old = Load(Bytes, FVRMSpringDataCustomVersion::ConvertedColliderAxes);
	TestFalse(TEXT("Axes were already converted, so no reimport"), Old->bNeedsReimport);
	for (const FVRMSpringJoint& Joint : Old->SpringConfig.Joints)
	{
		TestEqual(TEXT("Joint gets spring stiffness"), Joint.Stiffness, 0.3f);
		TestEqual(TEXT("Joint gets spring drag"), Joint.Drag, 0.7f);
		TestEqual(TEXT("Joint gets spring gravity dir"), Joint.GravityDir, FVector(1, 0, 0));
		TestEqual(TEXT("Joint gets spring gravity power"), Joint.GravityPower, 20.f);
		TestEqual(TEXT("Joint gets spring hit radius"), Joint.HitRadius, 4.f);
	}

	const UVRMSpringBoneData* Current = Load(Bytes, FVRMSpringDataCustomVersion::LatestVersion);
	for (const FVRMSpringJoint& Joint : Current->SpringConfig.Joints)
	{
		TestEqual(TEXT("Current data keeps its own joint stiffness"), Joint.Stiffness, 1.f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
