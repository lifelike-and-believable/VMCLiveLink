// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMActorBlueprintWiring.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "VRMInterchangeLog.h"

namespace VRMPipeline
{
	static USkeletalMeshComponent* FindInConstructionScript(const USimpleConstructionScript* SCS, USCS_Node*& OutNode)
	{
		OutNode = nullptr;
		if (!SCS)
		{
			return nullptr;
		}
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (USkeletalMeshComponent* Template = Node ? Cast<USkeletalMeshComponent>(Node->ComponentTemplate) : nullptr)
			{
				OutNode = Node;
				return Template;
			}
		}
		return nullptr;
	}

	USkeletalMeshComponent* FindSkeletalMeshComponentTemplate(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		if (!Blueprint->GeneratedClass)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}

		// 1) Added in this Blueprint's editor.
		USCS_Node* Node = nullptr;
		if (USkeletalMeshComponent* Own = FindInConstructionScript(Blueprint->SimpleConstructionScript, Node))
		{
			return Own;
		}

		// 2) Added by a parent Blueprint: the child edits its own override of that template.
		for (UClass* Class = Blueprint->ParentClass; Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* ParentClass = Cast<UBlueprintGeneratedClass>(Class);
			if (!ParentClass)
			{
				continue;
			}
			if (FindInConstructionScript(ParentClass->SimpleConstructionScript, Node) && Node)
			{
				if (UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(true))
				{
					return Cast<USkeletalMeshComponent>(Handler->CreateOverridenComponentTemplate(FComponentKey(Node)));
				}
			}
		}

		// 3) A native component (e.g. ACharacter's mesh) lives on the class default object.
		if (Blueprint->GeneratedClass)
		{
			if (const AActor* DefaultActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				return DefaultActor->FindComponentByClass<USkeletalMeshComponent>();
			}
		}
		return nullptr;
	}

	bool SetActorBlueprintMesh(UBlueprint* Blueprint, USkeletalMesh* Mesh, TSubclassOf<UAnimInstance> AnimClass)
	{
		if (!Blueprint || !Mesh)
		{
			return false;
		}
		USkeletalMeshComponent* Template = FindSkeletalMeshComponentTemplate(Blueprint);
		if (!Template)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] '%s' has no Skeletal Mesh component to give '%s'."), *Blueprint->GetPathName(), *Mesh->GetPathName());
			return false;
		}

		Template->Modify();
		Template->SetSkeletalMeshAsset(Mesh);
		if (AnimClass)
		{
			Template->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Template->SetAnimInstanceClass(AnimClass);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return true;
	}

	bool SetBlueprintObjectVariable(UBlueprint* Blueprint, FName VariableName, UObject* Value)
	{
		if (!Blueprint || !Value)
		{
			return false;
		}
		if (!Blueprint->GeneratedClass)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
		UClass* Class = Blueprint->GeneratedClass;
		UObject* Defaults = Class ? Class->GetDefaultObject() : nullptr;
		FObjectPropertyBase* Property = Class ? FindFProperty<FObjectPropertyBase>(Class, VariableName) : nullptr;
		if (!Defaults || !Property)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] '%s' has no variable '%s' to set to '%s'."), *Blueprint->GetPathName(), *VariableName.ToString(), *Value->GetPathName());
			return false;
		}
		if (!Value->IsA(Property->PropertyClass))
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Variable '%s' of '%s' holds a %s, not a %s."), *VariableName.ToString(), *Blueprint->GetPathName(), *Property->PropertyClass->GetName(), *Value->GetClass()->GetName());
			return false;
		}
		Defaults->Modify();
		Property->SetObjectPropertyValue_InContainer(Defaults, Value);
		Blueprint->MarkPackageDirty();
		return true;
	}
}
