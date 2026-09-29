// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "LiveLinkSourceFactory.h"
#include "VMCLiveLinkSourceFactory.generated.h"

#if WITH_EDITOR
class SWidget;
#endif

/**
 * Creates VMC sources: from the Live Link panel's "+ Source" menu (a creation panel showing
 * UVMCLiveLinkSourceSettings), and from a connection string when a Live Link preset is applied
 * (FVMCConnectionSettings::FromString). Game thread.
 */
UCLASS()
class VMCLIVELINK_API UVMCLiveLinkSourceFactory : public ULiveLinkSourceFactory
{
	GENERATED_BODY()

public:
	/** "VMC Live Link Source", in the + Source menu. */
	virtual FText GetSourceDisplayName() const override { return NSLOCTEXT("VMCLiveLink", "DisplayName", "VMC Live Link Source"); }
	/** The menu entry's tooltip. */
	virtual FText GetSourceTooltip() const override { return NSLOCTEXT("VMCLiveLink", "Tooltip", "Receive VMC (OSC) motion/curves"); }

#if WITH_EDITOR
	// Editor-only UI surface - kept out of runtime builds
	virtual EMenuType GetMenuType() const override { return EMenuType::SubPanel; }
	/** The settings panel with a Create button, which calls OnLiveLinkSourceCreated with a new source and its connection string. */
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const override;
#endif

	// Runtime-capable source creation (safe to keep in runtime module)
	/** A source from a connection string; invalid values keep their defaults (and are logged). */
	virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;
};