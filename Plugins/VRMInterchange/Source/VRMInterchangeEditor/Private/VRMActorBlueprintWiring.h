// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UAnimBlueprint;
class UAnimInstance;
class UBlueprint;
class USkeletalMesh;
class USkeletalMeshComponent;
class UVRMAvatarDescription;

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

	/**
	 * Gives a generated Live Link AnimBlueprint its VRM Expressions node: with bInsert, inserted
	 * between the AnimGraph's output and the pose that feeds it (the Live Link Pose node in the
	 * template), with Description as its avatar description. If the AnimGraph already has one, it
	 * takes Description when bInsert (the import made the AnimBlueprint), else only an unset avatar
	 * description is filled in. Pass bInsert only for an AnimBlueprint the import has just made: one
	 * it reuses keeps the user's graph. Compiles the Blueprint (without saving it)
	 * when it changes it. False when it changes nothing: no Description, or one without expressions
	 * (the node would warn on every compile), the node's class isn't loaded, or the AnimGraph's
	 * output isn't fed by a single pose (an edited graph is left to the user; logged).
	 */
	bool AddExpressionsNode(UAnimBlueprint* AnimBlueprint, UVRMAvatarDescription* Description, bool bInsert);
}
