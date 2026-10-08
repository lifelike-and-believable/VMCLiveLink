// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSubjectSettingsCustomization.h"
#include "VMCLiveLinkRemapper.h"
#include "VMCLiveLinkRemapperCustomization.h"

#include "DetailLayoutBuilder.h"
#include "LiveLinkSubjectSettings.h"
#include "PropertyHandle.h"

TSharedRef<IDetailCustomization> FVMCLiveLinkSubjectSettingsCustomization::MakeInstance()
{
	return MakeShared<FVMCLiveLinkSubjectSettingsCustomization>();
}

void FVMCLiveLinkSubjectSettingsCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FVMCLiveLinkSubjectSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Picking or clearing the remapper class adds or removes the tools: rebuild the panel. The
	// remappers collected below belong to this build, so a new remapper gets new buttons.
	const TSharedRef<IPropertyHandle> RemapperHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkSubjectSettings, Remapper), ULiveLinkSubjectSettings::StaticClass());
	if (RemapperHandle->IsValidHandle())
	{
		RemapperHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakBuilder = Builder]()
		{
			if (const TSharedPtr<IDetailLayoutBuilder> Pinned = WeakBuilder.Pin())
			{
				Pinned->ForceRefreshDetails();
			}
		}));
	}

	TArray<TWeakObjectPtr<UVMCLiveLinkRemapper>> Remappers;
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		const ULiveLinkSubjectSettings* Settings = Cast<ULiveLinkSubjectSettings>(Object.Get());
		if (UVMCLiveLinkRemapper* Remapper = Settings ? Cast<UVMCLiveLinkRemapper>(Settings->Remapper) : nullptr)
		{
			Remappers.Add(Remapper);
		}
	}
	if (Remappers.IsEmpty())
	{
		return;
	}

	// After the subject's own LiveLink category, which holds the remapper's settings inline.
	FVMCLiveLinkRemapperCustomization::AddMappingTools(DetailBuilder, Remappers, Builder, ECategoryPriority::Default);
}
