// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FOSCMessage;

/**
 * Parsing of VMC protocol messages (https://protocol.vmc.info), independent of the Live Link
 * source so it can be unit tested.
 */
namespace VMCProtocol
{
	/** One OSC argument, reduced to the types VMC uses. */
	struct FArg
	{
		enum class EType : uint8 { Float, Int, String, Other };

		EType Type = EType::Other;
		float Number = 0.f; // Float and Int (converted)
		FString String;

		static FArg MakeFloat(float V) { FArg A; A.Type = EType::Float; A.Number = V; return A; }
		static FArg MakeInt(int32 V) { FArg A; A.Type = EType::Int; A.Number = float(V); return A; }
		static FArg MakeString(const FString& V) { FArg A; A.Type = EType::String; A.String = V; return A; }

		bool IsNumber() const { return Type == EType::Float || Type == EType::Int; }
	};

	using FArgs = TArray<FArg, TInlineAllocator<16>>;

	/** Copies an OSC message's arguments. */
	void ReadArgs(const FOSCMessage& Message, FArgs& OutArgs);

	/** A transform as sent: Unity space (left-handed, Y up), metres, quaternion x y z w. */
	struct FPose
	{
		FString Name;
		FVector3f Position = FVector3f::ZeroVector;
		FQuat4f Rotation = FQuat4f::Identity;

		/** VMC v2.1 Root/Pos only: scale and offset for mixed-reality calibration. */
		bool bHasScaleAndOffset = false;
		FVector3f Scale = FVector3f::OneVector;
		FVector3f Offset = FVector3f::ZeroVector;
	};

	/** /VMC/Ext/Bone/Pos: (string name, 7 floats). */
	bool ParseBonePos(TConstArrayView<FArg> Args, FPose& Out);

	/**
	 * /VMC/Ext/Root/Pos: (string name, 7 floats), or v2.1 (string name, 7 floats, 3 scale, 3 offset).
	 * Also accepts the non-conformant form of 7 floats with no name, reporting it through
	 * bOutLegacyForm, since earlier VMCLiveLink versions and test senders used it.
	 */
	bool ParseRootPos(TConstArrayView<FArg> Args, FPose& Out, bool& bOutLegacyForm);

	/** /VMC/Ext/Blend/Val: (string name, float value). */
	bool ParseBlendVal(TConstArrayView<FArg> Args, FString& OutName, float& OutValue);

	/** Unity position (metres) to UE, optionally converting the basis and scaling to centimetres. */
	FVector ToUEPosition(const FVector3f& UnityPosition, bool bUnityToUE, bool bMetersToCm);

	/** Unity rotation to UE (normalized), optionally converting the basis. */
	FQuat ToUERotation(const FQuat4f& UnityRotation, bool bUnityToUE);
}
