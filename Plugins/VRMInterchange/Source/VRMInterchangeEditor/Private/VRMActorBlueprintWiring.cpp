// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMActorBlueprintWiring.h"

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
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
#include "Misc/EngineVersionComparison.h"
#include "VRMAvatarDescription.h"
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
					// If no override can be made, keep looking (another parent, then the native component).
					if (USkeletalMeshComponent* Override = Cast<USkeletalMeshComponent>(Handler->CreateOverridenComponentTemplate(FComponentKey(Node))))
					{
						return Override;
					}
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

		// A template is never registered, so the runtime work its setters (SetSkeletalMeshAsset and
		// friends) do, such as render state, bounds and animation, doesn't apply to it. Only the values
		// matter: they are written straight to the properties (the *_InContainer writers would call
		// the same setters). The mesh is held twice (the skinned asset of USkinnedMeshComponent and
		// SkeletalMeshAsset); both are set so they agree.
		// Both must be found, or the component would be left holding two different meshes.
		FObjectPropertyBase* MeshProperty = FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("SkeletalMeshAsset"));
		FObjectPropertyBase* SkinnedProperty = FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("SkinnedAsset"));
		if (!MeshProperty || !SkinnedProperty)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not set the mesh of '%s' (no %s property)."),
				*Template->GetPathName(), !MeshProperty ? TEXT("SkeletalMeshAsset") : TEXT("SkinnedAsset"));
			return false;
		}
		Template->Modify();
		MeshProperty->SetObjectPropertyValue(MeshProperty->ContainerPtrToValuePtr<void>(Template), Mesh);
		SkinnedProperty->SetObjectPropertyValue(SkinnedProperty->ContainerPtrToValuePtr<void>(Template), Mesh);
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		// UE 5.8 checks that the skinned asset isn't changed behind the component's back, and ensures
		// when the Blueprint compiles. The notification that records the change is protected;
		// PostEditChangeProperty on the skinned asset property runs it (and none of the anim handling).
		{
			FPropertyChangedEvent Changed(SkinnedProperty, EPropertyChangeType::ValueSet);
			Template->PostEditChangeProperty(Changed);
		}
#endif
		bool bAnimSet = true;
		if (AnimClass)
		{
			FByteProperty* Mode = FindFProperty<FByteProperty>(Template->GetClass(), TEXT("AnimationMode"));
			FObjectPropertyBase* Class = FindFProperty<FObjectPropertyBase>(Template->GetClass(), TEXT("AnimClass"));
			if (Mode && Class)
			{
				Mode->SetPropertyValue(Mode->ContainerPtrToValuePtr<void>(Template), uint8(EAnimationMode::AnimationBlueprint));
				Class->SetObjectPropertyValue(Class->ContainerPtrToValuePtr<void>(Template), AnimClass.Get());
			}
			else
			{
				// The mesh is still applied (compiled below); the caller learns the anim class wasn't.
				UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not set the anim class of '%s' (no %s property)."),
					*Template->GetPathName(), !Mode ? TEXT("AnimationMode") : TEXT("AnimClass"));
				bAnimSet = false;
			}
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return bAnimSet;
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
	namespace
	{
		// The graph node class lives in VRMSpringBonesEditor, which this module doesn't link: found by name.
		const TCHAR* const ExpressionsNodeClassPath = TEXT("/Script/VRMSpringBonesEditor.AnimGraphNode_VRMExpressions");

		UEdGraph* FindAnimGraph(UAnimBlueprint& AnimBlueprint)
		{
			for (UEdGraph* Graph : AnimBlueprint.FunctionGraphs)
			{
				if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
				{
					return Graph;
				}
			}
			return nullptr;
		}

		// A node's pose pin (an FPoseLink) in a direction.
		UEdGraphPin* FindPosePin(UEdGraphNode& Node, EEdGraphPinDirection Direction)
		{
			for (UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
					&& Pin->PinType.PinSubCategoryObject == FPoseLink::StaticStruct())
				{
					return Pin;
				}
			}
			return nullptr;
		}
	}

	bool AddExpressionsNode(UAnimBlueprint* AnimBlueprint, UVRMAvatarDescription* Description)
	{
		if (!AnimBlueprint || !Description)
		{
			return false;
		}
		UClass* NodeClass = FindObject<UClass>(nullptr, ExpressionsNodeClassPath);
		UEdGraph* Graph = FindAnimGraph(*AnimBlueprint);
		if (!NodeClass || !Graph)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not add a VRM Expressions node to '%s' (%s)."),
				*AnimBlueprint->GetPathName(), !NodeClass ? TEXT("the VRMSpringBonesEditor module isn't loaded") : TEXT("it has no AnimGraph"));
			return false;
		}
		const UEdGraphSchema* Schema = Graph->GetSchema();

		UEdGraphNode* ExpressionsNode = nullptr;
		UAnimGraphNode_Root* Root = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA(NodeClass))
			{
				ExpressionsNode = Node;
			}
			else if (UAnimGraphNode_Root* AsRoot = Cast<UAnimGraphNode_Root>(Node))
			{
				Root = AsRoot;
			}
		}

		if (!ExpressionsNode)
		{
			// Between the output and the pose that feeds it.
			UEdGraphPin* Result = Root ? FindPosePin(*Root, EGPD_Input) : nullptr;
			if (!Result || Result->LinkedTo.Num() != 1)
			{
				UE_LOG(LogVRMInterchange, Log, TEXT("[VRMInterchange] '%s': its AnimGraph's output isn't fed by a single pose, so no VRM Expressions node was added. Add one by hand for facial expressions."),
					*AnimBlueprint->GetPathName());
				return false;
			}
			UEdGraphPin* Source = Result->LinkedTo[0];
			UEdGraphNode* SourceNode = Source->GetOwningNode();

			UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
			Graph->AddNode(NewNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
			NewNode->CreateNewGuid();
			NewNode->PostPlacedNewNode();
			NewNode->AllocateDefaultPins();
			NewNode->NodePosX = (SourceNode->NodePosX + Root->NodePosX) / 2;
			NewNode->NodePosY = Root->NodePosY;

			UEdGraphPin* In = FindPosePin(*NewNode, EGPD_Input);
			UEdGraphPin* Out = FindPosePin(*NewNode, EGPD_Output);
			if (!In || !Out || !Schema->TryCreateConnection(Source, In) || !Schema->TryCreateConnection(Out, Result))
			{
				UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] Could not connect a VRM Expressions node in '%s'."), *AnimBlueprint->GetPathName());
				Graph->RemoveNode(NewNode);
				Schema->TryCreateConnection(Source, Result); // as it was
				return false;
			}
			ExpressionsNode = NewNode;
		}
		else
		{
			// One already there: fill in its avatar description only if it has none.
			const UEdGraphPin* Avatar = ExpressionsNode->FindPin(TEXT("AvatarDescription"), EGPD_Input);
			if (!Avatar || Avatar->LinkedTo.Num() > 0 || Avatar->DefaultObject)
			{
				return false;
			}
		}

		UEdGraphPin* Avatar = ExpressionsNode->FindPin(TEXT("AvatarDescription"), EGPD_Input);
		if (!Avatar)
		{
			UE_LOG(LogVRMInterchange, Warning, TEXT("[VRMInterchange] The VRM Expressions node in '%s' has no Avatar Description pin."), *AnimBlueprint->GetPathName());
			return false;
		}
		Schema->TrySetDefaultObject(*Avatar, Description);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
		return true;
	}
}
