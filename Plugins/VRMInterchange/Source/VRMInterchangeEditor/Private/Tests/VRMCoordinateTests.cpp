// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "VRMCoordinateConversion.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesTypes.h"
#include "VRMTranslator.h"

namespace VRMCoordinateTests
{
	static constexpr double Tolerance = 1.e-4;

	static FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm")) : FString();
	}

	/** Minimal VRM 1.0 document: node 0 is the root, node 1 carries the collider and the spring joint. */
	static FString MakeVRM1Json(const TCHAR* Node1, const TCHAR* ColliderShape, const TCHAR* GravityDir)
	{
		return FString::Printf(TEXT(R"JSON({
  "asset": {"version": "2.0"},
  "nodes": [ {"name": "Root", "children": [1]}, %s ],
  "extensions": { "VRMC_springBone": {
    "specVersion": "1.0",
    "colliders": [ {"node": 1, "shape": %s} ],
    "colliderGroups": [ {"name": "G", "colliders": [0]} ],
    "springs": [ {"name": "S", "colliderGroups": [0],
      "joints": [ {"node": 1, "hitRadius": 0.02, "stiffness": 1.0, "dragForce": 0.4, "gravityPower": 0.1, "gravityDir": %s} ] } ]
  } }
})JSON"), Node1, ColliderShape, GravityDir);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMCoordinateConversionTest, "VRM.Coordinates.Conversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMCoordinateConversionTest::RunTest(const FString& Parameters)
{
	using namespace VRM::Coord;
	using VRMCoordinateTests::Tolerance;

	// Net mapping (x, y, z) -> (x, z, y), in centimetres.
	TestTrue(TEXT("Position mapping"), ToUEPosition(FVector(1, 2, 3), MetersToCentimeters).Equals(FVector(100, 300, 200), Tolerance));
	TestTrue(TEXT("Translator uses the same mapping"), VRM::GltfPositionToUE(FVector(1, 2, 3), 100.f).Equals(FVector(100, 300, 200), Tolerance));
	TestTrue(TEXT("glTF up is UE up"), ToUEDirection(FVector(0, 1, 0)).Equals(FVector(0, 0, 1), Tolerance));

	// The mapping is a reflection: a triangle's cross product comes out negated, which is what
	// turns glTF's counter-clockwise front faces into UE's clockwise ones with the indices unchanged.
	const FVector A(0.3, -1.2, 0.7), B(-0.5, 0.25, 2.0);
	TestTrue(TEXT("Handedness flips"), FVector::CrossProduct(ToUEDirection(A), ToUEDirection(B)).Equals(-ToUEDirection(FVector::CrossProduct(A, B)), Tolerance));

	// Rotations: converting after rotating equals rotating the converted vector by the converted rotation.
	const FQuat Rotations[] = {
		FQuat(FVector(0, 1, 0), UE_HALF_PI),
		FQuat(FVector(1, 0, 0), -0.7),
		FQuat(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 2.1),
	};
	const FVector Vectors[] = { FVector(1, 0, 0), FVector(0, 0, 1), FVector(0.2, -1.5, 0.9) };
	for (const FQuat& Q : Rotations)
	{
		for (const FVector& V : Vectors)
		{
			const FVector Expected = ToUEDirection(Q.RotateVector(V));
			const FVector Actual = ToUERotation(Q).RotateVector(ToUEDirection(V));
			TestTrue(FString::Printf(TEXT("Rotation %s applied to %s"), *Q.ToString(), *V.ToString()), Actual.Equals(Expected, Tolerance));
		}
	}

	TestTrue(TEXT("Scale axes swap"), ToUEScale(FVector(1, 2, 3)).Equals(FVector(1, 3, 2), Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringCoordinatesTest, "VRM.SpringBones.Coordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringCoordinatesTest::RunTest(const FString& Parameters)
{
	using namespace VRM::Coord;
	using VRMCoordinateTests::Tolerance;
	FString Error;

	// vrm0_minimal: head collider authored at offset {"x":0,"y":0.1,"z":0} (object form, SP-08) with
	// radius 0.08. It must end up 10 cm above the head bone, and gravity {"x":0,"y":-1,"z":0} must point down.
	{
		FVRMSpringConfig Config;
		if (TestTrue(TEXT("vrm0_minimal parses"), VRM::ParseSpringBonesFromFile(VRMCoordinateTests::FixturePath(TEXT("vrm0_minimal")), Config, Error))
			&& TestTrue(TEXT("vrm0_minimal has a sphere collider"), Config.Colliders.Num() > 0 && Config.Colliders[0].Spheres.Num() > 0)
			&& TestTrue(TEXT("vrm0_minimal has a spring"), Config.Springs.Num() > 0))
		{
			const FVRMSpringColliderSphere& Sphere = Config.Colliders[0].Spheres[0];
			TestTrue(FString::Printf(TEXT("VRM 0.x head collider is 10 cm above the head (%s)"), *Sphere.Offset.ToString()), Sphere.Offset.Equals(FVector(0, 0, 10), Tolerance));
			TestEqual(TEXT("VRM 0.x collider radius in cm"), Sphere.Radius, 8.f, float(Tolerance));
			TestTrue(TEXT("VRM 0.x gravity points down"), Config.Springs[0].GravityDir.Equals(FVector(0, 0, -1), Tolerance));
		}
	}

	// Gravity (1, 0, 0) in glTF maps to the same UE axis as the mesh's +X.
	{
		FVRMSpringConfig Config;
		const FString Json = VRMCoordinateTests::MakeVRM1Json(TEXT(R"({"name": "Bone"})"), TEXT(R"({"sphere": {"offset": [0, 0, 0], "radius": 0.05}})"), TEXT("[1, 0, 0]"));
		if (TestTrue(TEXT("gravity document parses"), VRM::ParseSpringBonesFromJson(Json, Config, Error)) && TestTrue(TEXT("has a spring"), Config.Springs.Num() == 1))
		{
			const FVector MeshX = VRM::GltfPositionToUE(FVector(1, 0, 0), 1.f);
			TestTrue(FString::Printf(TEXT("Gravity +X follows the mesh (%s vs %s)"), *Config.Springs[0].GravityDir.ToString(), *MeshX.ToString()), Config.Springs[0].GravityDir.Equals(MeshX, Tolerance));
		}
	}

	// A collider on a node rotated 90 degrees about Y and scaled by 2: the node-local offset
	// (0, 0, 0.1) is (0.2, 0, 0) in glTF model space, so (20, 0, 0) cm in UE; the radius doubles too.
	{
		FVRMSpringConfig Config;
		const FString Json = VRMCoordinateTests::MakeVRM1Json(
			TEXT(R"({"name": "Bone", "rotation": [0, 0.70710678, 0, 0.70710678], "scale": [2, 2, 2]})"),
			TEXT(R"({"sphere": {"offset": [0, 0, 0.1], "radius": 0.05}})"),
			TEXT("[0, -1, 0]"));
		if (TestTrue(TEXT("rotated-node document parses"), VRM::ParseSpringBonesFromJson(Json, Config, Error))
			&& TestTrue(TEXT("has a sphere"), Config.Colliders.Num() == 1 && Config.Colliders[0].Spheres.Num() == 1))
		{
			const FVRMSpringColliderSphere& Sphere = Config.Colliders[0].Spheres[0];
			TestTrue(FString::Printf(TEXT("Offset follows the node's rotation and scale (%s)"), *Sphere.Offset.ToString()), Sphere.Offset.Equals(FVector(20, 0, 0), Tolerance));
			TestEqual(TEXT("Radius follows the node's scale"), Sphere.Radius, 10.f, float(Tolerance));
		}
	}

	// A plane on the same rotated node: normal (0, 0, 1) is +X in model space, so +X in UE.
	{
		FVRMSpringConfig Config;
		const FString Json = VRMCoordinateTests::MakeVRM1Json(
			TEXT(R"({"name": "Bone", "rotation": [0, 0.70710678, 0, 0.70710678]})"),
			TEXT(R"({"plane": {"offset": [0, 0.1, 0], "normal": [0, 0, 1]}})"),
			TEXT("[0, -1, 0]"));
		if (TestTrue(TEXT("plane document parses"), VRM::ParseSpringBonesFromJson(Json, Config, Error))
			&& TestTrue(TEXT("has a plane"), Config.Colliders.Num() == 1 && Config.Colliders[0].Planes.Num() == 1))
		{
			const FVRMSpringColliderPlane& Plane = Config.Colliders[0].Planes[0];
			TestTrue(FString::Printf(TEXT("Plane normal (%s)"), *Plane.Normal.ToString()), Plane.Normal.Equals(FVector(1, 0, 0), Tolerance));
			TestTrue(FString::Printf(TEXT("Plane offset (%s)"), *Plane.Offset.ToString()), Plane.Offset.Equals(FVector(0, 0, 10), Tolerance));
		}
	}

	// VRM 0.x collider offsets have Z negated relative to glTF (as three-vrm's VRM 0.x loader treats them).
	{
		FVRMSpringConfig Config;
		const FString Json = TEXT(R"JSON({
  "asset": {"version": "2.0"},
  "nodes": [ {"name": "Root", "children": [1]}, {"name": "Head"} ],
  "extensions": { "VRM": { "secondaryAnimation": {
    "colliderGroups": [ {"node": 1, "colliders": [ {"offset": {"x": 0, "y": 0, "z": 0.1}, "radius": 0.05} ]} ],
    "boneGroups": [ {"comment": "g", "stiffiness": 1.0, "dragForce": 0.4, "gravityPower": 0, "gravityDir": {"x": 0, "y": -1, "z": 0},
                     "center": -1, "hitRadius": 0.02, "bones": [1], "colliderGroups": [0]} ]
  } } }
})JSON");
		if (TestTrue(TEXT("VRM 0.x document parses"), VRM::ParseSpringBonesFromJson(Json, Config, Error))
			&& TestTrue(TEXT("VRM 0.x sphere"), Config.Colliders.Num() == 1 && Config.Colliders[0].Spheres.Num() == 1))
		{
			const FVector Offset = Config.Colliders[0].Spheres[0].Offset;
			TestTrue(FString::Printf(TEXT("VRM 0.x z offset is negated (%s)"), *Offset.ToString()), Offset.Equals(ToUEPosition(FVector(0, 0, -0.1), MetersToCentimeters), Tolerance));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
