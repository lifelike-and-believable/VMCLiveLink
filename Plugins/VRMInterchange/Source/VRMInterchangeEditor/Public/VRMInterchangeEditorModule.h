// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * The VRM import editor module: registers the spring data details panel and the import report's
 * message log, and after the engine starts offers to register the VRM import pipelines if they are
 * missing.
 */
class VRMINTERCHANGEEDITOR_API FVRMInterchangeEditorModule : public IModuleInterface
{
public:
	/** Registers the details customization, the message log listing and the post-init prompt. */
	virtual void StartupModule() override;
	/** Unregisters what StartupModule registered. */
	virtual void ShutdownModule() override;

private:
	/** Offers to register the VRM import pipelines when they are missing. Never edits settings by itself. */
	void OnPostEngineInit();

	FDelegateHandle PostEngineInitHandle;

};
