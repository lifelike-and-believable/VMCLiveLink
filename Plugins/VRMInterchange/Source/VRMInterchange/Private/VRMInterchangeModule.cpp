// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Engine/Engine.h"
#include "VRMTranslator.h"
#include "VRMImportMessages.h"

namespace
{
	// UE 5.8 replaced the OnPostEngineInit member with an accessor.
	FSimpleMulticastDelegate& VRMPostEngineInitDelegate()
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		return FCoreDelegates::GetOnPostEngineInit();
#else
		return FCoreDelegates::OnPostEngineInit;
#endif
	}
}

class FVRMInterchangeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		VRM::ImportMessages::RegisterCapture();
		// Register after engine init; call immediately if GEngine is already valid
		PostEngineInitHandle = VRMPostEngineInitDelegate().AddRaw(this, &FVRMInterchangeModule::OnPostEngineInit);
		if (GEngine)
		{
			OnPostEngineInit();
		}
	}

	virtual void ShutdownModule() override
	{
		if (PostEngineInitHandle.IsValid())
		{
			VRMPostEngineInitDelegate().Remove(PostEngineInitHandle);
			PostEngineInitHandle.Reset();
		}
		// UE 5.6 to 5.8 have no UnregisterTranslator; the manager cleans up internally
		VRM::ImportMessages::UnregisterCapture();
	}

private:
	void OnPostEngineInit()
	{
		UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
		Manager.RegisterTranslator(UVRMTranslator::StaticClass());
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FVRMInterchangeModule, VRMInterchange)