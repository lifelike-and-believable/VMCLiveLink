// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * The VMC remapper's Mapping Tools and Live Mapping table in the Live Link panel's subject details.
 * There the details panel's root is the subject's ULiveLinkSubjectSettings and the remapper is an
 * instanced subobject shown inline under Remapper, which the details panel never customizes, so
 * FVMCLiveLinkRemapperCustomization doesn't apply. This layout adds the same tools as categories of
 * the subject details whenever the subject's Remapper is a UVMCLiveLinkRemapper; other subjects are
 * left as they are. Registered on ULiveLinkSubjectSettings, so it also applies to its subclasses
 * (such as Live Link Hub's settings, whose own layout still runs).
 */
class FVMCLiveLinkSubjectSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

private:
	TWeakPtr<IDetailLayoutBuilder> Builder;
};
