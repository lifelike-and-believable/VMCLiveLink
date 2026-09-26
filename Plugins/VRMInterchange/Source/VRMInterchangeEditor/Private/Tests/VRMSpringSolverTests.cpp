// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the spring solver core (P2.1), without the anim graph: gravity, frame rate independence,
// world-space inertia, colliders and center space.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringSolver.h"

namespace VRMSpringSolverTests
{
	/** A chain of NumJoints joints, each Length cm long along RestDir, starting 100 cm up. Solver bones
	 *  0..NumJoints-1 are the joints, NumJoints is the tail, NumJoints+1 is an anchor (the chain's
	 *  root, usable as center). Bone rotations are identity. */
	struct FChainRig
	{
		int32 NumJoints;
		TArray<FVector> Base;
		TArray<FTransform> Bones;
		FVRMSpringSolverSetup Setup;

		FChainRig(int32 InNumJoints, const FVector& RestDir, float Length, float Stiffness, float Drag, const FVector& Gravity)
			: NumJoints(InNumJoints)
		{
			const FVector Root(0, 0, 100);
			for (int32 I = 0; I <= NumJoints; ++I)
			{
				Base.Add(Root + RestDir * Length * I);
			}
			Base.Add(Root); // anchor
			Setup.NumBones = Base.Num();

			FVRMSpringSolverSetup::FChain& Chain = Setup.Chains.AddDefaulted_GetRef();
			for (int32 I = 0; I < NumJoints; ++I)
			{
				FVRMSpringSolverSetup::FJoint& J = Chain.Joints.AddDefaulted_GetRef();
				J.Bone = I;
				J.TailBone = I + 1;
				J.Stiffness = Stiffness;
				J.Drag = Drag;
				J.GravityDir = Gravity.GetSafeNormal();
				J.GravityPower = Gravity.Size();
			}
			Move(FVector::ZeroVector);
		}

		/** The animated pose, with every bone moved by Offset (component space). */
		TConstArrayView<FTransform> Move(const FVector& Offset)
		{
			Bones.Reset();
			for (const FVector& P : Base)
			{
				Bones.Add(FTransform(P + Offset));
			}
			return Bones;
		}

		int32 Anchor() const { return NumJoints + 1; }
	};

	FVector Tip(const FVRMSpringSolver& Solver)
	{
		return Solver.JointDebug().Num() > 0 ? Solver.JointDebug().Last().Tail : FVector::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverGravity, "VRM.SpringBones.Solver.Gravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverGravity::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// A horizontal chain with no stiffness falls and hangs straight down.
	FChainRig Rig(3, FVector(1, 0, 0), 10.f, 0.f, 0.4f, FVector(0, 0, -100));
	FVRMSpringSolver Solver;
	Solver.Init(Rig.Setup);
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		Solver.Step(1.f / 60.f, Rig.Bones, FTransform::Identity);
	}
	if (!TestEqual(TEXT("Three joints"), Solver.JointDebug().Num(), 3)) return false;
	for (int32 K = 0; K < 3; ++K)
	{
		const FVRMSpringSolverJointDebug& J = Solver.JointDebug()[K];
		const FVector Expected = J.Head + FVector(0, 0, -10);
		TestTrue(FString::Printf(TEXT("Joint %d hangs straight down (off by %.4f cm)"), K, FVector::Dist(J.Tail, Expected)),
			FVector::Dist(J.Tail, Expected) < 0.1f);
	}
	// And the output rotations point each bone at its tail: bone +X was the rest direction.
	const FTransform& First = Solver.JointTransforms()[0];
	TestTrue(TEXT("The first bone's rest axis now points down"), First.GetRotation().GetAxisX().Equals(FVector(0, 0, -1), 1.0e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverFrameRate, "VRM.SpringBones.Solver.FrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverFrameRate::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// The character sways sideways for a second, then stops. After two seconds the chain is in the
	// same place whatever the frame rate.
	TArray<FVector> Tips;
	for (const int32 Hz : { 30, 60, 144 })
	{
		FChainRig Rig(3, FVector(1, 0, 0), 10.f, 0.3f, 0.4f, FVector(0, 0, -100));
		FVRMSpringSolver Solver;
		Solver.Init(Rig.Setup);
		for (int32 Frame = 1; Frame <= 2 * Hz; ++Frame)
		{
			const float Time = float(Frame) / Hz;
			const FTransform ComponentToWorld(FVector(0, 50.f * FMath::Sin(UE_PI * FMath::Min(Time, 1.f)), 0));
			Solver.Step(1.f / Hz, Rig.Bones, ComponentToWorld);
		}
		Tips.Add(Tip(Solver));
	}
	TestTrue(FString::Printf(TEXT("30 Hz matches 60 Hz (%s vs %s)"), *Tips[0].ToString(), *Tips[1].ToString()), Tips[0].Equals(Tips[1], 0.01f));
	TestTrue(FString::Printf(TEXT("144 Hz matches 60 Hz (%s vs %s)"), *Tips[2].ToString(), *Tips[1].ToString()), Tips[2].Equals(Tips[1], 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverWorldInertia, "VRM.SpringBones.Solver.WorldInertia",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverWorldInertia::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// A hanging chain; the character then walks sideways at 1 m/s. The tip trails behind.
	FChainRig Rig(3, FVector(0, 0, -1), 10.f, 0.2f, 0.4f, FVector(0, 0, -100));
	FVRMSpringSolver Solver;
	Solver.Init(Rig.Setup);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Solver.Step(1.f / 60.f, Rig.Bones, FTransform::Identity);
	}
	for (int32 Frame = 1; Frame <= 15; ++Frame)
	{
		Solver.Step(1.f / 60.f, Rig.Bones, FTransform(FVector(0, 100.f * Frame / 60.f, 0)));
	}
	const float TipY = Tip(Solver).Y; // component space
	TestTrue(FString::Printf(TEXT("The tip trails the motion (component Y %.3f cm)"), TipY), TipY < -2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverSphere, "VRM.SpringBones.Solver.SphereCollider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverSphere::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// One joint hanging onto a sphere just beside where its tail would rest.
	const FVector Center(1, 0, 90);
	const float Radius = 3.f;
	const float HitRadius = 1.f;
	for (const bool bWithCollider : { true, false })
	{
		FChainRig Rig(1, FVector(0, 0, -1), 10.f, 0.f, 0.4f, FVector(0, 0, -100));
		if (bWithCollider)
		{
			FVRMSpringSolverSetup::FCollider& Collider = Rig.Setup.Colliders.AddDefaulted_GetRef();
			Collider.Bone = INDEX_NONE; // component origin
			FVRMSpringColliderSphere& Sphere = Collider.Spheres.AddDefaulted_GetRef();
			Sphere.Offset = Center;
			Sphere.Radius = Radius;
			Rig.Setup.Chains[0].Colliders.Add(0);
			Rig.Setup.Chains[0].Joints[0].HitRadius = HitRadius;
		}
		FVRMSpringSolver Solver;
		Solver.Init(Rig.Setup);
		float Closest = TNumericLimits<float>::Max();
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Solver.Step(1.f / 60.f, Rig.Bones, FTransform::Identity);
			Closest = FMath::Min(Closest, FVector::Dist(Tip(Solver), Center));
		}
		if (bWithCollider)
		{
			// The final length constraint can pull the tail back in by a hair.
			TestTrue(FString::Printf(TEXT("The tail stays outside radius + hit radius (closest %.4f cm)"), Closest), Closest >= Radius + HitRadius - 0.05f);
		}
		else
		{
			TestTrue(TEXT("Without the collider the tail would be inside it"), Closest < Radius);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverCenter, "VRM.SpringBones.Solver.CenterSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverCenter::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// The whole skeleton slides sideways in the component (say, a root-motion-free walk cycle's hips).
	// With the chain's root as center, the chain moves with it and doesn't lag; in world space it does.
	for (const bool bCenter : { true, false })
	{
		FChainRig Rig(3, FVector(1, 0, 0), 10.f, 0.5f, 0.4f, FVector::ZeroVector);
		if (bCenter)
		{
			Rig.Setup.Chains[0].CenterBone = Rig.Anchor();
		}
		FVRMSpringSolver Solver;
		Solver.Init(Rig.Setup);
		FVector Offset;
		for (int32 Frame = 1; Frame <= 30; ++Frame)
		{
			Offset = FVector(0, 50.f * Frame / 60.f, 0);
			Solver.Step(1.f / 60.f, Rig.Move(Offset), FTransform::Identity);
		}
		const float Lag = Tip(Solver).Y - Offset.Y;
		if (bCenter)
		{
			TestTrue(FString::Printf(TEXT("Center space: no lag (%.4f cm)"), Lag), FMath::Abs(Lag) < 0.01f);
		}
		else
		{
			TestTrue(FString::Printf(TEXT("World space: the tip lags (%.3f cm)"), Lag), Lag < -2.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringSolverSubsteps, "VRM.SpringBones.Solver.Substeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringSolverSubsteps::RunTest(const FString& Parameters)
{
	using namespace VRMSpringSolverTests;
	// Frames shorter than a step move nothing until a whole step has built up; pausing (zero delta
	// time) holds the pose; a long hitch is capped.
	FChainRig Rig(1, FVector(1, 0, 0), 10.f, 0.f, 0.4f, FVector(0, 0, -100));
	FVRMSpringSolver Solver;
	Solver.Init(Rig.Setup);
	Solver.Step(0.f, Rig.Bones, FTransform::Identity);
	const FVector Start = Tip(Solver);
	TestTrue(TEXT("A zero-length frame doesn't move the tail"), Tip(Solver).Equals(Rig.Base[1], 1.0e-3f));

	Solver.Step(1.f / 240.f, Rig.Bones, FTransform::Identity);
	TestTrue(TEXT("A quarter step doesn't move the tail"), Tip(Solver).Equals(Start, 1.0e-4f));
	for (int32 I = 0; I < 3; ++I) Solver.Step(1.f / 240.f, Rig.Bones, FTransform::Identity);
	const FVector AfterOneStep = Tip(Solver);
	TestTrue(TEXT("Four quarter steps make one step"), !AfterOneStep.Equals(Start, 1.0e-3f));

	FChainRig Rig2(1, FVector(1, 0, 0), 10.f, 0.f, 0.4f, FVector(0, 0, -100));
	FVRMSpringSolver OneStep;
	OneStep.Init(Rig2.Setup);
	OneStep.Step(1.f / 60.f, Rig2.Bones, FTransform::Identity);
	TestTrue(TEXT("... the same step a 60 Hz frame takes"), Tip(OneStep).Equals(AfterOneStep, 1.0e-3f));

	// A 5 second hitch is simulated as MaxDeltaTime (0.1 s, 6 steps), the same as a 0.1 s frame.
	FVRMSpringSolver Hitch, Capped;
	Hitch.Init(Rig2.Setup);
	Capped.Init(Rig2.Setup);
	Hitch.Step(5.f, Rig2.Bones, FTransform::Identity);
	Capped.Step(0.1f, Rig2.Bones, FTransform::Identity);
	TestTrue(TEXT("A hitch is capped"), Tip(Hitch).Equals(Tip(Capped), 1.0e-3f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
