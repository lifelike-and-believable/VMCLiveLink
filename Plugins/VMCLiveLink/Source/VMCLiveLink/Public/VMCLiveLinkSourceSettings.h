// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "VMCConnectionSettings.h"
#include "VMCLiveLinkSourceSettings.generated.h"

/**
 * A VMC source's settings, shown in the Live Link panel's details for the source and saved with
 * Live Link presets. Edits apply to the running source: a new port or bind address restarts the
 * listener, a new subject name moves the data to that subject.
 */
UCLASS()
class VMCLIVELINK_API UVMCLiveLinkSourceSettings : public ULiveLinkSourceSettings
{
	GENERATED_BODY()

public:
	/** UDP port the sender sends to (VMC's default is 39539). */
	UPROPERTY(EditAnywhere, Category = "VMC|Connection", meta = (ClampMin = "1", ClampMax = "65535", UIMin = "1", UIMax = "65535"))
	int32 Port = FVMCConnectionSettings::DefaultPort;

	/** IPv4 address to listen on. 0.0.0.0 listens on every network interface. */
	UPROPERTY(EditAnywhere, Category = "VMC|Connection")
	FString BindAddress = TEXT("0.0.0.0");

	/** Live Link subject the data is published as. */
	UPROPERTY(EditAnywhere, Category = "VMC|Connection")
	FName SubjectName = TEXT("VMC_Subject");

	/** Convert Unity's axes (Y up) to UE's (Z up). Leave on for VMC senders. */
	UPROPERTY(EditAnywhere, Category = "VMC|Conversion", meta = (DisplayName = "Convert Unity to UE Axes"))
	bool bUnityToUE = true;

	/** Convert metres to centimetres. Leave on for VMC senders. */
	UPROPERTY(EditAnywhere, Category = "VMC|Conversion", meta = (DisplayName = "Convert Metres to Centimetres"))
	bool bMetersToCm = true;

	/** Extra turn of the root about UE's up axis. */
	UPROPERTY(EditAnywhere, Category = "VMC|Conversion", meta = (Units = "Degrees", UIMin = "-180", UIMax = "180"))
	float YawOffsetDeg = 0.f;

	/** Curves the sender didn't send since the last frame read 0. Turn off for senders that only send blend shapes that changed. */
	UPROPERTY(EditAnywhere, Category = "VMC|Frame")
	bool bZeroMissingCurves = false;

	/** Use each bone's streamed translation. Otherwise only Hips does, and the other bones use the reference skeleton's. */
	UPROPERTY(EditAnywhere, Category = "VMC|Frame")
	bool bPreferIncomingTranslations = false;

	/** Use the remapper's reference skeleton for bone translations the sender doesn't provide. */
	UPROPERTY(EditAnywhere, Category = "VMC|Frame")
	bool bUseRefOffsets = true;

	/** Receive on a thread of the plugin's own, with each frame timestamped on arrival (lower latency,
	 *  steadier timing). Off: messages arrive through the OSC plugin on the game thread, one batch per
	 *  engine frame. The status shows the frame rate and jitter, to compare the two. */
	UPROPERTY(EditAnywhere, Category = "VMC|Connection")
	bool bReceiveThread = true;

	FVMCConnectionSettings ToConnectionSettings() const;
	void FromConnectionSettings(const FVMCConnectionSettings& In);
};
