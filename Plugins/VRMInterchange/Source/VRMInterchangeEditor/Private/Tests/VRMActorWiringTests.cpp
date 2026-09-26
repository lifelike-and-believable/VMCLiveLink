// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for wiring generated actor Blueprints to the imported mesh (P3.4, PE-07).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "VRMActorBlueprintWiring.h"

namespace VRMActorWiringTests
{
	UBlueprint* MakeBlueprint(UClass* Parent, const TCHAR* Name)
	{
		UPackage* Package = CreatePackage(*(FString(TEXT("/Game/VRMActorWiringTests/")) + Name));
		return FKismetEditorUtilities::CreateBlueprint(Parent, Package, Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	}

	/** A Blueprint with a Skeletal Mesh component added in the Blueprint editor (a construction script node). */
	UBlueprint* MakeBlueprintWithMeshNode(const TCHAR* Name, USCS_Node*& OutNode)
	{
		UBlueprint* Blueprint = MakeBlueprint(AActor::StaticClass(), Name);
		OutNode = Blueprint->SimpleConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), TEXT("Body"));
		Blueprint->SimpleConstructionScript->AddNode(OutNode);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint;
	}

	/** The mesh a component (or template) holds, read from its SkeletalMeshAsset property. */
	UObject* MeshOf(const USkeletalMeshComponent* Component)
	{
		const FObjectPropertyBase* Property = Component ? FindFProperty<FObjectPropertyBase>(Component->GetClass(), TEXT("SkeletalMeshAsset")) : nullptr;
		return Property ? Property->GetObjectPropertyValue(Property->ContainerPtrToValuePtr<void>(Component)) : nullptr;
	}

	USkeletalMesh* MakeMesh(const TCHAR* Name)
	{
		return NewObject<USkeletalMesh>(GetTransientPackage(), Name, RF_Transient);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMActorWiringConstructionScript, "VRM.Pipeline.ActorWiring.ConstructionScript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMActorWiringConstructionScript::RunTest(const FString& Parameters)
{
	using namespace VRMActorWiringTests;
	USkeletalMesh* Mesh = MakeMesh(TEXT("SK_WiringScript"));

	// A component added in the Blueprint editor: its template gets the mesh (it isn't on the CDO).
	USCS_Node* Node = nullptr;
	UBlueprint* Blueprint = MakeBlueprintWithMeshNode(TEXT("BP_WiringScript"), Node);
	TestTrue(TEXT("The template is found"), VRMPipeline::FindSkeletalMeshComponentTemplate(Blueprint) == Node->ComponentTemplate);
	TestTrue(TEXT("Wired"), VRMPipeline::SetActorBlueprintMesh(Blueprint, Mesh, nullptr));
	const USkeletalMeshComponent* Template = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
	TestTrue(TEXT("The construction script template has the mesh"), MeshOf(Template) == Mesh);

	// A child Blueprint overrides the parent's template; the parent keeps its own.
	UBlueprint* Child = MakeBlueprint(Blueprint->GeneratedClass, TEXT("BP_WiringScriptChild"));
	FKismetEditorUtilities::CompileBlueprint(Child);
	USkeletalMesh* ChildMesh = MakeMesh(TEXT("SK_WiringScriptChild"));
	TestTrue(TEXT("Child wired"), VRMPipeline::SetActorBlueprintMesh(Child, ChildMesh, nullptr));
	const UInheritableComponentHandler* Handler = Child->GetInheritableComponentHandler(false);
	const USkeletalMeshComponent* Override = Handler ? Cast<USkeletalMeshComponent>(Handler->GetOverridenComponentTemplate(FComponentKey(Node))) : nullptr;
	TestTrue(TEXT("The child's override has the child's mesh"), MeshOf(Override) == ChildMesh);
	TestTrue(TEXT("The parent keeps its mesh"), MeshOf(Template) == Mesh);

	// Nothing to wire.
	AddExpectedError(TEXT("has no Skeletal Mesh component"), EAutomationExpectedErrorFlags::Contains, 1);
	UBlueprint* Empty = MakeBlueprint(AActor::StaticClass(), TEXT("BP_WiringEmpty"));
	TestFalse(TEXT("A Blueprint without a mesh component is reported"), VRMPipeline::SetActorBlueprintMesh(Empty, Mesh, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMActorWiringNative, "VRM.Pipeline.ActorWiring.NativeComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMActorWiringNative::RunTest(const FString& Parameters)
{
	// A native component (ACharacter's mesh) is set on the class defaults and survives compiling.
	using namespace VRMActorWiringTests;
	USkeletalMesh* Mesh = MakeMesh(TEXT("SK_WiringNative"));
	UBlueprint* Blueprint = MakeBlueprint(ACharacter::StaticClass(), TEXT("BP_WiringNative"));
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	TestTrue(TEXT("Wired"), VRMPipeline::SetActorBlueprintMesh(Blueprint, Mesh, nullptr));
	const ACharacter* Defaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	TestTrue(TEXT("The character's mesh is set after compiling"), Defaults && MeshOf(Defaults->GetMesh()) == Mesh);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMActorWiringVariable, "VRM.Pipeline.ActorWiring.Variable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMActorWiringVariable::RunTest(const FString& Parameters)
{
	// The retarget actor template takes the mesh through its "VRM Character" variable.
	using namespace VRMActorWiringTests;
	USkeletalMesh* Mesh = MakeMesh(TEXT("SK_WiringVariable"));
	UBlueprint* Blueprint = MakeBlueprint(AActor::StaticClass(), TEXT("BP_WiringVariable"));
	const FEdGraphPinType MeshPin(UEdGraphSchema_K2::PC_Object, NAME_None, USkeletalMesh::StaticClass(), EPinContainerType::None, false, FEdGraphTerminalType());
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("VRM Character"), MeshPin);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TestTrue(TEXT("Set"), VRMPipeline::SetBlueprintObjectVariable(Blueprint, TEXT("VRM Character"), Mesh));
	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Blueprint->GeneratedClass, TEXT("VRM Character"));
	TestTrue(TEXT("The class default holds the mesh"),
		Property && Property->GetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject()) == Mesh);

	AddExpectedError(TEXT("has no variable"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("A missing variable is reported"), VRMPipeline::SetBlueprintObjectVariable(Blueprint, TEXT("Missing"), Mesh));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
