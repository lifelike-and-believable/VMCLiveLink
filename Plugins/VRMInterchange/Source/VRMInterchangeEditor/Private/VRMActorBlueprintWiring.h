// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UAnimInstance;
class UBlueprint;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Wires generated actor Blueprints to an imported character (PE-07, P3.4).
 *
 * A component added in the Blueprint editor isn't on the class default object: it's a template in
 * the Blueprint's construction script (or, for one inherited from a parent Blueprint, an override
 * the child keeps). Setting the mesh on the CDO's components, as before, missed those.
 */
namespace VRMPipeline
{
	/**
	 * The skeletal mesh component a Blueprint's actors take their mesh from: the first one in its own
	 * construction script, else an override of one a parent Blueprint adds (created if needed), else
	 * a native one on the class default object. Null if there is none.
	 */
	USkeletalMeshComponent* FindSkeletalMeshComponentTemplate(UBlueprint* Blueprint);

	/**
	 * Sets that component's mesh and, if AnimClass is set, its anim Blueprint, then marks the
	 * Blueprint modified and compiles it. False, with a warning, if the Blueprint has no skeletal
	 * mesh component or its mesh can't be set, or if the anim class can't be set (the mesh is
	 * still applied then).
	 */
	bool SetActorBlueprintMesh(UBlueprint* Blueprint, USkeletalMesh* Mesh, TSubclassOf<UAnimInstance> AnimClass);

	/**
	 * Sets an object variable's default value (e.g. the "VRM Character" variable of the retarget actor
	 * template). False, with a warning, if the Blueprint has no such variable or it can't hold Value.
	 */
	bool SetBlueprintObjectVariable(UBlueprint* Blueprint, FName VariableName, UObject* Value);
}
