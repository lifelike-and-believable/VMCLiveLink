// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Phase 5 benchmarks: the import payloads and the spring solver on the sizes the plan's targets name.
// They report timings (test info and "PERF" log lines) and only fail on wrong results, never on
// time, since the CI runner is shared.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Math/RandomStream.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Modules/ModuleManager.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "VRMSpringSolver.h"
#include "VRMTranslator.h"

namespace VRMPerfTests
{
	void Report(FAutomationTestBase& Test, const FString& What, double Value, const TCHAR* Unit)
	{
		const FString Line = FString::Printf(TEXT("PERF %s: %.3f %s"), *What, Value, Unit);
		Test.AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	}

	/** A grid mesh of Side x Side vertices, weighted to NumBones bones, with NumMorphs full targets. */
	FVRMParsedModel MakeGridModel(int32 Side, int32 NumBones, int32 NumMorphs, int32 NumMorphsWithNormals)
	{
		FVRMParsedModel Model;
		for (int32 b = 0; b < NumBones; ++b)
		{
			FVRMParsedBone& Bone = Model.Bones.AddDefaulted_GetRef();
			Bone.Name = FString::Printf(TEXT("Bone_%d"), b);
			Bone.Parent = b - 1;
		}

		FVRMParsedMesh& Mesh = Model.Mesh;
		const int32 NumVerts = Side * Side;
		Mesh.Positions.SetNumUninitialized(NumVerts);
		Mesh.Normals.Init(FVector3f(0.f, 0.f, 1.f), NumVerts);
		Mesh.UV0.SetNumUninitialized(NumVerts);
		Mesh.SkinWeights.SetNum(NumVerts);
		for (int32 y = 0; y < Side; ++y)
		{
			for (int32 x = 0; x < Side; ++x)
			{
				const int32 v = y * Side + x;
				Mesh.Positions[v] = FVector3f(float(x), float(y), 0.f);
				Mesh.UV0[v] = FVector2f(float(x) / Side, float(y) / Side);
				FVRMParsedMesh::FWeight& W = Mesh.SkinWeights[v];
				for (int32 k = 0; k < 4; ++k)
				{
					W.BoneIndex[k] = uint16((v + k * 7) % NumBones);
					W.Weight[k] = 0.25f;
				}
			}
		}
		for (int32 y = 0; y + 1 < Side; ++y)
		{
			for (int32 x = 0; x + 1 < Side; ++x)
			{
				const uint32 v = uint32(y * Side + x);
				Mesh.Indices.Append({ v, v + 1, v + uint32(Side), v + 1, v + uint32(Side) + 1, v + uint32(Side) });
				Mesh.TriMaterialIndex.Append({ x % 2, x % 2 });
			}
		}
		for (int32 m = 0; m < NumMorphs; ++m)
		{
			FVRMParsedMorph& Morph = Mesh.Morphs.AddDefaulted_GetRef();
			Morph.Name = FString::Printf(TEXT("Morph_%d"), m);
			Morph.DeltaPositions.Init(FVector3f(0.f, 0.f, 0.01f * (m + 1)), NumVerts);
			if (m < NumMorphsWithNormals)
			{
				Morph.DeltaNormals.Init(FVector3f(0.1f, 0.f, 0.f), NumVerts);
			}
		}
		return Model;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPerfMeshPayload, "VRM.Perf.MeshPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPerfMeshPayload::RunTest(const FString& Parameters)
{
	using namespace VRMPerfTests;
	// ~50k vertices (the P5.4 target) and 60 morph targets (the P5.5 target), 10 with normals.
	constexpr int32 Side = 224;
	constexpr int32 NumMorphs = 60;
	const FVRMParsedModel Model = MakeGridModel(Side, 50, NumMorphs, 10);
	const int32 NumVerts = Model.Mesh.Positions.Num();
	const int32 NumTris = Model.Mesh.Indices.Num() / 3;

	double BaseBest = TNumericLimits<double>::Max();
	for (int32 Run = 0; Run < 3; ++Run)
	{
		UE::Interchange::FMeshPayloadData Data;
		const double Start = FPlatformTime::Seconds();
		const bool bBuilt = VRM::BuildMeshPayload(Model, INDEX_NONE, Data);
		BaseBest = FMath::Min(BaseBest, FPlatformTime::Seconds() - Start);
		if (!TestTrue(TEXT("Base payload builds"), bBuilt))
		{
			return false;
		}
		TestEqual(TEXT("Base vertices"), Data.MeshDescription.Vertices().Num(), NumVerts);
		TestEqual(TEXT("Base triangles"), Data.MeshDescription.Triangles().Num(), NumTris);
	}

	const double MorphStart = FPlatformTime::Seconds();
	for (int32 m = 0; m < NumMorphs; ++m)
	{
		UE::Interchange::FMeshPayloadData Data;
		if (!TestTrue(TEXT("Morph payload builds"), VRM::BuildMeshPayload(Model, m, Data)))
		{
			return false;
		}
		if (m == 0)
		{
			TestEqual(TEXT("Morph vertices"), Data.MeshDescription.Vertices().Num(), NumVerts);
		}
	}
	const double MorphTotal = FPlatformTime::Seconds() - MorphStart;

	UE::Interchange::FMeshPayloadData Bad;
	TestFalse(TEXT("Out-of-range morph"), VRM::BuildMeshPayload(Model, NumMorphs, Bad));

	Report(*this, FString::Printf(TEXT("mesh payload, base (%d vertices, %d triangles)"), NumVerts, NumTris), BaseBest * 1000.0, TEXT("ms"));
	Report(*this, FString::Printf(TEXT("mesh payload, %d morph targets"), NumMorphs), MorphTotal * 1000.0, TEXT("ms"));
	Report(*this, TEXT("mesh payload, per morph target"), MorphTotal * 1000.0 / NumMorphs, TEXT("ms"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPerfTextureDecode, "VRM.Perf.TextureDecode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPerfTextureDecode::RunTest(const FString& Parameters)
{
	using namespace VRMPerfTests;
	constexpr int32 Size = 2048;
	TArray<uint8> RGBA;
	RGBA.SetNumUninitialized(Size * Size * 4);
	FRandomStream Random(7);
	for (int32 i = 0; i < Size * Size; ++i)
	{
		// A gradient with a little noise, compressible like a painted texture
		RGBA[i * 4 + 0] = uint8((i % Size) * 255 / Size);
		RGBA[i * 4 + 1] = uint8((i / Size) * 255 / Size);
		RGBA[i * 4 + 2] = uint8(Random.RandRange(0, 15));
		RGBA[i * 4 + 3] = 255;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Writer = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!TestTrue(TEXT("PNG writer"), Writer.IsValid() && Writer->SetRaw(RGBA.GetData(), RGBA.Num(), Size, Size, ERGBFormat::RGBA, 8)))
	{
		return false;
	}
	const TArray64<uint8> Png = Writer->GetCompressed();

	for (const VRM::ETextureUsage Usage : { VRM::ETextureUsage::Color, VRM::ETextureUsage::Normal })
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Run = 0; Run < 3; ++Run)
		{
			const double Start = FPlatformTime::Seconds();
			TOptional<UE::Interchange::FImportImage> Image = VRM::DecodeTextureImage(Png, Usage);
			Best = FMath::Min(Best, FPlatformTime::Seconds() - Start);
			if (!TestTrue(TEXT("Decodes"), Image.IsSet()))
			{
				return false;
			}
			TestEqual(TEXT("Width"), int32(Image->SizeX), Size);
		}
		Report(*this, FString::Printf(TEXT("texture decode %dx%d PNG, %s"), Size, Size,
			Usage == VRM::ETextureUsage::Normal ? TEXT("normal map") : TEXT("colour")), Best * 1000.0, TEXT("ms"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMPerfSpringSolver, "VRM.Perf.SpringSolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMPerfSpringSolver::RunTest(const FString& Parameters)
{
	using namespace VRMPerfTests;
	// The P5.3 target: 200 joints (20 chains of 10) and 30 colliders, every chain against all of them.
	constexpr int32 NumChains = 20;
	constexpr int32 JointsPerChain = 10;
	constexpr int32 NumColliders = 30;

	FVRMSpringSolverSetup Setup;
	TArray<FTransform> Bones;
	Bones.Add(FTransform::Identity); // bone 0: the colliders' anchor
	for (int32 c = 0; c < NumChains; ++c)
	{
		FVRMSpringSolverSetup::FChain& Chain = Setup.Chains.AddDefaulted_GetRef();
		const int32 First = Bones.Num();
		for (int32 j = 0; j <= JointsPerChain; ++j) // the last bone is the tail of the last joint
		{
			Bones.Add(FTransform(FVector(c * 10.f, 0.f, 150.f - j * 5.f)));
		}
		for (int32 j = 0; j < JointsPerChain; ++j)
		{
			FVRMSpringSolverSetup::FJoint& Joint = Chain.Joints.AddDefaulted_GetRef();
			Joint.Bone = First + j;
			Joint.TailBone = First + j + 1;
			Joint.ParentBone = j == 0 ? 0 : First + j - 1;
			Joint.Stiffness = 1.f;
			Joint.Drag = 0.4f;
			Joint.GravityPower = 1.f;
			Joint.HitRadius = 2.f;
		}
		for (int32 k = 0; k < NumColliders; ++k)
		{
			Chain.Colliders.Add(k);
		}
	}
	for (int32 k = 0; k < NumColliders; ++k)
	{
		FVRMSpringSolverSetup::FCollider& Collider = Setup.Colliders.AddDefaulted_GetRef();
		Collider.Bone = 0;
		FVRMSpringColliderSphere& Sphere = Collider.Spheres.AddDefaulted_GetRef();
		Sphere.Offset = FVector(k * 7.f, 8.f, 100.f + (k % 5) * 10.f);
		Sphere.Radius = 5.f;
	}
	Setup.NumBones = Bones.Num();

	FVRMSpringSolver Solver;
	Solver.Init(Setup);
	constexpr int32 Warmup = 60;
	constexpr int32 Frames = 600;
	double Total = 0.0;
	for (int32 Frame = 0; Frame < Warmup + Frames; ++Frame)
	{
		// The character sways, so the chains move and hit the colliders.
		const FTransform ComponentToWorld(FVector(FMath::Sin(Frame * 0.1f) * 30.f, 0.f, 0.f));
		const double Start = FPlatformTime::Seconds();
		Solver.Step(1.f / 60.f, Bones, ComponentToWorld);
		if (Frame >= Warmup)
		{
			Total += FPlatformTime::Seconds() - Start;
		}
	}
	TestEqual(TEXT("Every joint simulated"), Solver.JointTransforms().Num(), NumChains * JointsPerChain);
	for (const FTransform& Joint : Solver.JointTransforms())
	{
		if (!TestFalse(TEXT("Finite joints"), Joint.ContainsNaN()))
		{
			break;
		}
	}
	Report(*this, FString::Printf(TEXT("spring solver, %d joints, %d colliders, per frame"), NumChains * JointsPerChain, NumColliders),
		Total * 1000.0 / Frames, TEXT("ms"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
