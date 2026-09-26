// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Everything a VMC source is created with, and its Live Link connection string: the one place that
 * reads and writes it (P3.1, VMC-17).
 *
 * Format: "key=value" pairs separated by ';', keys case-insensitive, unknown keys ignored, e.g.
 *   port=39539;bind=0.0.0.0;unity2ue=1;meters2cm=1;yaw=0;subject=VMC_Subject;zeromissing=0
 * Booleans accept 1/0, true/false, yes/no, on/off. Strings saved by earlier versions (port,
 * unity2ue, meters2cm, subject) read the same.
 */
struct VMCLIVELINK_API FVMCConnectionSettings
{
	static constexpr int32 DefaultPort = 39539;

	/** UDP port to listen on (1 to 65535). */
	int32 Port = DefaultPort;
	/** IPv4 address to bind to; 0.0.0.0 listens on every interface. */
	FString BindAddress = TEXT("0.0.0.0");
	/** Convert Unity's axes to UE's. */
	bool bUnityToUE = true;
	/** Convert metres to centimetres. */
	bool bMetersToCm = true;
	/** Extra turn about UE Z applied to the root, in degrees. */
	float YawOffsetDeg = 0.f;
	/** Live Link subject name. */
	FName SubjectName = TEXT("VMC_Subject");
	/** Curves not sent since the previous Blend/Apply read 0 instead of holding their last value. */
	bool bZeroMissingCurves = false;
	/** Use each bone's streamed translation instead of the reference skeleton's (Hips always uses it). */
	bool bPreferIncomingTranslations = false;
	/** Use the reference skeleton's translations for bones without one of their own. */
	bool bUseRefOffsets = true;

	/**
	 * Reads a connection string. Keys that are missing keep their defaults. Returns false if any
	 * value is invalid (that value keeps its default); OutErrors, if given, says which.
	 */
	static bool FromString(const FString& ConnectionString, FVMCConnectionSettings& Out, TArray<FString>* OutErrors = nullptr);

	/** Writes every setting. FromString(ToString()) gives the same settings. */
	FString ToString() const;

	/** Checks port, bind address and subject name. */
	bool Validate(TArray<FString>* OutErrors = nullptr) const;

	bool operator==(const FVMCConnectionSettings& Other) const;
	bool operator!=(const FVMCConnectionSettings& Other) const { return !(*this == Other); }
};
