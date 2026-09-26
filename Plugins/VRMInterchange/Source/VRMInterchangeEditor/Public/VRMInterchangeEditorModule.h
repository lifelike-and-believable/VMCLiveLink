// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class VRMINTERCHANGEEDITOR_API FVRMInterchangeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Offers to register the VRM import pipelines when they are missing. Never edits settings by itself. */
	void OnPostEngineInit();

	FDelegateHandle PostEngineInitHandle;

};
