// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * The one place where VRM/glTF space is converted to Unreal space. Every importer path (mesh,
 * skeleton, morph targets, spring bones) uses these functions, so geometry from different parts
 * of a file always lands on the same axes.
 *
 * glTF is right-handed, Y up, metres. Unreal is left-handed, Z up, centimetres. The mapping is
 *
 *     UE = (glTF.X, glTF.Z, glTF.Y)
 *
 * a reflection (it swaps Y and Z), which is what changes the handedness. Triangle winding is kept
 * as written: the reflection turns a glTF counter-clockwise front face into the clockwise front face
 * Unreal expects. Rotations follow from the same reflection: conjugating a rotation by it keeps
 * the axis mapping and reverses the sense, so q = (x, y, z, w) becomes (-x, -z, -y, w). Then
 * ToUEDirection(q * v) == ToUERotation(q) * ToUEDirection(v) for any q and v.
 *
 * Facing: VRM 1.0 models face +Z in glTF (VRM 1.0 spec), and VRM 0.x models face -Z (three-vrm's
 * VRMUtils.rotateVRM0 turns VRM 0.x scenes 180 degrees about Y for the same reason). The mapping
 * above takes glTF +Z to UE +Y, the direction the UE mannequin faces, so VRM 1.0 needs nothing more
 * and VRM 0.x needs a 180-degree yaw. FVRMAxisConvention adds that yaw; importer code should convert
 * through a convention for the file's version rather than call the functions below directly.
 */
namespace VRM::Coord
{
	/** glTF metres to Unreal centimetres. */
	inline constexpr float MetersToCentimeters = 100.f;

	/** A direction or offset (no unit scale): (x, y, z) -> (x, z, y). */
	inline FVector ToUEDirection(const FVector& V) { return FVector(V.X, V.Z, V.Y); }
	inline FVector3f ToUEDirection(const FVector3f& V) { return FVector3f(V.X, V.Z, V.Y); }

	/** A position or length vector, with the unit scale (usually MetersToCentimeters). */
	inline FVector ToUEPosition(const FVector& V, double Scale) { return ToUEDirection(V) * Scale; }
	inline FVector3f ToUEPosition(const FVector3f& V, float Scale) { return ToUEDirection(V) * Scale; }

	/** A rotation: (x, y, z, w) -> (-x, -z, -y, w). */
	inline FQuat ToUERotation(const FQuat& Q) { return FQuat(-Q.X, -Q.Z, -Q.Y, Q.W); }

	/** A per-axis scale: the axes swap like directions, and magnitudes are unchanged. */
	inline FVector ToUEScale(const FVector& S) { return FVector(S.X, S.Z, S.Y); }

	enum class EVRMVersion : uint8
	{
		Unknown,	// not a VRM file; imported as generic glTF, which faces +Z like VRM 1.0
		VRM0,
		VRM1,
	};

	/**
	 * How a given file's geometry maps to UE: the axis mapping above, the unit scale, and a
	 * 180-degree yaw about UE Z for VRM 0.x so every version faces UE +Y. The yaw is a proper
	 * rotation, so it doesn't change handedness or winding.
	 */
	struct FVRMAxisConvention
	{
		float Scale = MetersToCentimeters;
		bool bYaw180 = false;

		static FVRMAxisConvention ForVersion(EVRMVersion Version, float InScale = MetersToCentimeters)
		{
			FVRMAxisConvention Convention;
			Convention.Scale = InScale;
			Convention.bYaw180 = (Version == EVRMVersion::VRM0);
			return Convention;
		}

		FVector Direction(const FVector& V) const
		{
			const FVector D = ToUEDirection(V);
			return bYaw180 ? FVector(-D.X, -D.Y, D.Z) : D;
		}
		FVector3f Direction(const FVector3f& V) const
		{
			const FVector3f D = ToUEDirection(V);
			return bYaw180 ? FVector3f(-D.X, -D.Y, D.Z) : D;
		}
		FVector Position(const FVector& V) const { return Direction(V) * Scale; }
		FVector3f Position(const FVector3f& V) const { return Direction(V) * Scale; }

		/** Conjugating by a 180-degree yaw about Z negates the rotation's X and Y. */
		FQuat Rotation(const FQuat& Q) const
		{
			const FQuat R = ToUERotation(Q);
			return bYaw180 ? FQuat(-R.X, -R.Y, R.Z, R.W) : R;
		}
	};
}
