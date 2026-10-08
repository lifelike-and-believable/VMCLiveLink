// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the VRM Expressions node the import adds to the generated Live Link AnimBlueprint.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNodeBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "VRMActorBlueprintWiring.h"
#include "VRMAvatarDescription.h"

namespace VRMExpressionsNodeWiringTests
{
	const TCHAR* const TemplatePath = TEXT("/VRMInterchange/Animation/ABP_LL_VRM_Template.ABP_LL_VRM_Template");
	const TCHAR* const NodeClassPath = TEXT("/Script/VRMSpringBonesEditor.AnimGraphNode_VRMExpressions");

	// A copy of the plugin's Live Link AnimBlueprint template, as the Live Link pipeline makes.
	UAnimBlueprint* CopyTemplate()
	{
		UAnimBlueprint* Template = LoadObject<UAnimBlueprint>(nullptr, TemplatePath);
		if (!Template)
		{
			return nullptr;
		}
		const FString Name = FString::Printf(TEXT("ABP_ExpressionsTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
		UPackage* Package = CreatePackage(*(FString(TEXT("/Game/VRMExpressionsNodeTests/")) + Name));
		Package->SetFlags(RF_Transient);
		UAnimBlueprint* Copy = DuplicateObject<UAnimBlueprint>(Template, Package, *Name);
		FKismetEditorUtilities::CompileBlueprint(Copy);
		return Copy;
	}

	UVRMAvatarDescription* MakeDescription()
	{
		UVRMAvatarDescription* Description = NewObject<UVRMAvatarDescription>(GetTransientPackage());
		FVRMExpression& Happy = Description->Avatar.Expressions.AddDefaulted_GetRef();
		Happy.Name = TEXT("happy");
		Happy.Preset = EVRMExpressionPreset::Happy;
		return Description;
	}

	UEdGraph* AnimGraph(UAnimBlueprint& AnimBlueprint)
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

	TArray<UEdGraphNode*> ExpressionsNodes(UEdGraph& Graph)
	{
		UClass* NodeClass = FindObject<UClass>(nullptr, NodeClassPath);
		return Graph.Nodes.FilterByPredicate([NodeClass](const UEdGraphNode* Node) { return Node && NodeClass && Node->IsA(NodeClass); });
	}

	UEdGraphPin* PosePin(UEdGraphNode& Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.PinSubCategoryObject == FPoseLink::StaticStruct())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UAnimGraphNode_Root* Root(UEdGraph& Graph)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			if (UAnimGraphNode_Root* AsRoot = Cast<UAnimGraphNode_Root>(Node))
			{
				return AsRoot;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMExpressionsNodeWiringTest, "VRM.Pipeline.ExpressionsNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMExpressionsNodeWiringTest::RunTest(const FString& Parameters)
{
	using namespace VRMExpressionsNodeWiringTests;

	// The copies are standalone (as the template is): released at the end, so they don't stay loaded.
	TArray<UAnimBlueprint*> Copies;
	ON_SCOPE_EXIT
	{
		for (UAnimBlueprint* Copy : Copies)
		{
			if (Copy)
			{
				Copy->ClearFlags(RF_Standalone | RF_Public);
				Copy->MarkAsGarbage();
			}
		}
	};
	auto Copy = [&Copies]() { UAnimBlueprint* New = CopyTemplate(); Copies.Add(New); return New; };

	UAnimBlueprint* AnimBlueprint = Copy();
	UVRMAvatarDescription* Description = MakeDescription();
	if (!TestNotNull(TEXT("The template AnimBlueprint"), AnimBlueprint)) return false;
	UEdGraph* Graph = AnimGraph(*AnimBlueprint);
	if (!TestNotNull(TEXT("Its AnimGraph"), Graph)) return false;
	TestEqual(TEXT("The template has no expressions node"), ExpressionsNodes(*Graph).Num(), 0);
	TestEqual(TEXT("The template compiles without errors or warnings"), int32(AnimBlueprint->Status), int32(BS_UpToDate));

	// A reused AnimBlueprint (bInsert false) keeps its graph; a description without expressions adds nothing.
	TestFalse(TEXT("Not inserted into a reused AnimBlueprint"), VRMPipeline::AddExpressionsNode(AnimBlueprint, Description, /*bInsert*/ false));
	TestFalse(TEXT("Not inserted for an avatar without expressions"), VRMPipeline::AddExpressionsNode(AnimBlueprint, NewObject<UVRMAvatarDescription>(GetTransientPackage()), /*bInsert*/ true));
	TestEqual(TEXT("Still no node"), ExpressionsNodes(*Graph).Num(), 0);

	// Added between the output and the Live Link Pose node, with the avatar description.
	TestTrue(TEXT("Adds the node"), VRMPipeline::AddExpressionsNode(AnimBlueprint, Description, /*bInsert*/ true));
	const TArray<UEdGraphNode*> Added = ExpressionsNodes(*Graph);
	if (!TestEqual(TEXT("One expressions node"), Added.Num(), 1)) return false;
	UEdGraphNode* Node = Added[0];
	UEdGraphPin* Result = Root(*Graph) ? PosePin(*Root(*Graph), EGPD_Input) : nullptr;
	UEdGraphPin* In = PosePin(*Node, EGPD_Input);
	UEdGraphPin* Out = PosePin(*Node, EGPD_Output);
	if (!TestTrue(TEXT("Pins"), Result && In && Out)) return false;
	TestTrue(TEXT("It feeds the output"), Result->LinkedTo.Num() == 1 && Result->LinkedTo[0] == Out);
	TestTrue(TEXT("The Live Link pose feeds it"), In->LinkedTo.Num() == 1
		&& In->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("AnimGraphNode_LiveLinkPose"));
	const UEdGraphPin* Avatar = Node->FindPin(TEXT("AvatarDescription"), EGPD_Input);
	TestTrue(TEXT("Its avatar description"), Avatar && Avatar->DefaultObject == Description);
	TestEqual(TEXT("Compiles without errors or warnings"), int32(AnimBlueprint->Status), int32(BS_UpToDate));

	// Again: nothing to do.
	TestFalse(TEXT("A second call changes nothing"), VRMPipeline::AddExpressionsNode(AnimBlueprint, MakeDescription(), /*bInsert*/ true));
	TestEqual(TEXT("Still one node"), ExpressionsNodes(*Graph).Num(), 1);
	TestTrue(TEXT("The avatar description kept"), Avatar && Avatar->DefaultObject == Description);

	// An existing node without an avatar description gets one.
	{
		UEdGraphPin* Pin = Node->FindPin(TEXT("AvatarDescription"), EGPD_Input);
		Pin->DefaultObject = nullptr;
		UVRMAvatarDescription* Other = MakeDescription();
		TestTrue(TEXT("Fills in an unset avatar description, also in a reused AnimBlueprint"), VRMPipeline::AddExpressionsNode(AnimBlueprint, Other, /*bInsert*/ false));
		TestTrue(TEXT("The new avatar description"), Pin->DefaultObject == Other);
		TestEqual(TEXT("Still one node after filling in"), ExpressionsNodes(*Graph).Num(), 1);
	}

	// No avatar description: nothing is added (a node without one would warn on every compile).
	UAnimBlueprint* Untouched = Copy();
	UEdGraph* UntouchedGraph = Untouched ? AnimGraph(*Untouched) : nullptr;
	if (TestNotNull(TEXT("A second copy's AnimGraph"), UntouchedGraph))
	{
		TestFalse(TEXT("No avatar description, no node"), VRMPipeline::AddExpressionsNode(Untouched, nullptr, /*bInsert*/ true));
		TestEqual(TEXT("No node added"), ExpressionsNodes(*UntouchedGraph).Num(), 0);
	}

	// An edited graph whose output isn't fed by a single pose is left alone.
	UAnimBlueprint* Edited = Copy();
	UEdGraph* EditedGraph = Edited ? AnimGraph(*Edited) : nullptr;
	if (TestNotNull(TEXT("A third copy's AnimGraph"), EditedGraph))
	{
		UEdGraphPin* EditedResult = Root(*EditedGraph) ? PosePin(*Root(*EditedGraph), EGPD_Input) : nullptr;
		if (TestNotNull(TEXT("The output pin"), EditedResult))
		{
			EditedResult->BreakAllPinLinks();
			TestFalse(TEXT("An unlinked output gets no node"), VRMPipeline::AddExpressionsNode(Edited, MakeDescription(), /*bInsert*/ true));
			TestEqual(TEXT("No node in the edited graph"), ExpressionsNodes(*EditedGraph).Num(), 0);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
