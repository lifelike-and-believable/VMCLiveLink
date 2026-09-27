// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"

struct FOSCMessage;

/**
 * Parsing of VMC protocol messages (https://protocol.vmc.info), independent of the Live Link
 * source so it can be unit tested.
 */
namespace VMCProtocol
{
	/** The VMC messages this plugin knows (https://protocol.vmc.info/english). */
	enum class EAddress : uint8
	{
		RootPos,     // /VMC/Ext/Root/Pos
		BonePos,     // /VMC/Ext/Bone/Pos
		BlendVal,    // /VMC/Ext/Blend/Val
		BlendApply,  // /VMC/Ext/Blend/Apply
		Time,        // /VMC/Ext/T
		Available,   // /VMC/Ext/OK
		DevicePos,      // /VMC/Ext/Hmd/Pos, Con/Pos, Tra/Pos: a tracked device in world space
		DevicePosLocal, // their /Local variants (relative to the avatar's root; not used)
		Camera,         // /VMC/Ext/Cam
		Other,          // anything else (keys, MIDI, settings, non-VMC)
	};

	/** Which VMC message an OSC address is. Case-sensitive, as OSC addresses are. */
	EAddress ClassifyAddress(FStringView Address);
	EAddress ClassifyAddress(FAnsiStringView Address);

	/** One OSC argument, reduced to the types VMC uses. */
	struct FArg
	{
		enum class EType : uint8 { Float, Int, String, Other };

		EType Type = EType::Other;
		float Number = 0.f; // Float and Int (converted)
		FString String;     // String, from the OSC plugin's messages
		FUtf8StringView Utf8; // String, from VMCOscParser: points into the packet, no copy

		static FArg MakeUtf8(FUtf8StringView V) { FArg A; A.Type = EType::String; A.Utf8 = V; return A; }

		bool IsNonEmptyString() const { return Type == EType::String && (!String.IsEmpty() || !Utf8.IsEmpty()); }

		/** The string as a name. Decodes UTF-8 without a heap allocation for names up to 128 characters. */
		FName ToName() const;

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
		FName Name;
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

	/** /VMC/Ext/Hmd|Con|Tra/Pos: (string serial, 7 floats), like Bone/Pos. */
	inline bool ParseDevicePos(TConstArrayView<FArg> Args, FPose& Out) { return ParseBonePos(Args, Out); }

	/** /VMC/Ext/Cam: (string name, 7 floats, float fov in degrees). */
	bool ParseCamera(TConstArrayView<FArg> Args, FPose& Out, float& OutFieldOfView);

	/** The Live Link subject a device or camera is published as: "<Subject>_<Name>". */
	FName MakeDeviceSubjectName(FName Subject, FName DeviceName);

	/** /VMC/Ext/Blend/Val: (string name, float value). */
	bool ParseBlendVal(TConstArrayView<FArg> Args, FName& OutName, float& OutValue);

	/** /VMC/Ext/T: (float time), the sender's clock in seconds. */
	bool ParseTime(TConstArrayView<FArg> Args, float& OutSeconds);

	/** What /VMC/Ext/OK says about the sender. Fields the sender didn't send are unset. */
	struct FSenderState
	{
		bool bLoaded = false;              // an avatar is loaded
		TOptional<int32> Calibration;      // 0 uncalibrated, 1 waiting for calibration, 2 calibrating, 3 calibrated
		TOptional<int32> CalibrationMode;  // 0 normal, 1 mixed reality (hand), 2 mixed reality (floor)
		TOptional<int32> Tracking;         // 0 tracking lost, 1 tracking

		bool operator==(const FSenderState& Other) const
		{
			return bLoaded == Other.bLoaded && Calibration == Other.Calibration
				&& CalibrationMode == Other.CalibrationMode && Tracking == Other.Tracking;
		}
		bool operator!=(const FSenderState& Other) const { return !(*this == Other); }
	};

	/** /VMC/Ext/OK: (int loaded), (int loaded, int calibrationState, int calibrationMode) or v2.5's
	 *  (..., int trackingStatus). */
	bool ParseAvailable(TConstArrayView<FArg> Args, FSenderState& Out);

	/**
	 * The parts of a sender state worth showing in the source status, comma-separated ("no avatar
	 * loaded", "calibrating", "tracking lost", ...). Empty when the sender is loaded, calibrated (or
	 * doesn't say) and tracking (or doesn't say).
	 */
	FString DescribeSenderState(const FSenderState& State);

	/** Unity position (metres) to UE, optionally converting the basis and scaling to centimetres. */
	FVector ToUEPosition(const FVector3f& UnityPosition, bool bUnityToUE, bool bMetersToCm);

	/** Unity rotation to UE (normalized), optionally converting the basis. */
	FQuat ToUERotation(const FQuat4f& UnityRotation, bool bUnityToUE);
}
