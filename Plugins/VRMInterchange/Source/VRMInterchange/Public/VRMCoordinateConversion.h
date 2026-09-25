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
 * Facing: VRM 1.0 models face +Z in glTF and VRM 0.x models are expected to face -Z [Verify, T-05].
 * These functions don't yaw; plan task P1.9 adds a per-version yaw on top of them.
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
}
