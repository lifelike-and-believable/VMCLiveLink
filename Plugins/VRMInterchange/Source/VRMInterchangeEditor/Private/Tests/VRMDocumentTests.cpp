// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeVRMNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"
#include "VRMSpringBonesParser.h"

namespace VRMDocumentTests
{
	static FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm"))
			: FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMDocumentLoadTest, "VRM.Document.Load",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMDocumentLoadTest::RunTest(const FString& Parameters)
{
	// One read gives everything an import needs: hash, JSON, nodes, version, geometry (P3.3).
	const FString Path = VRMDocumentTests::FixturePath(TEXT("vrm1_minimal"));
	FString Error;
	const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, Error);
	if (!TestTrue(FString::Printf(TEXT("vrm1_minimal loads (%s)"), *Error), Document.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("VRM 1.0"), Document->GetVersion() == VRM::Coord::EVRMVersion::VRM1);
	TestTrue(FString::Printf(TEXT("Has geometry (%s)"), *Document->GetGeometryError()), Document->HasGeometry());
	TestEqual(TEXT("Hash is the file's MD5"), LexToString(Document->GetSourceHash()), LexToString(FMD5Hash::HashFile(*Path)));
	TestTrue(TEXT("JSON parsed"), Document->GetJsonRoot()->HasField(TEXT("nodes")));

	const TArray<FVRMDocumentNode>& Nodes = Document->GetNodes();
	TestTrue(TEXT("Has nodes"), Nodes.Num() > 0);
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		for (const int32 Child : Nodes[i].Children)
		{
			TestEqual(FString::Printf(TEXT("Node %d's child %d points back"), i, Child), Nodes[Child].Parent, i);
		}
	}

	// The model built from the document is the one built from the file.
	FVRMParsedModel FromDocument, FromFile;
	TestTrue(TEXT("Builds from the document"), VRM::BuildParsedModel(*Document, FromDocument));
	TestTrue(TEXT("Builds from the file"), VRM::LoadVRMFile(Path, FromFile));
	TestEqual(TEXT("Same bones"), FromDocument.Bones.Num(), FromFile.Bones.Num());
	TestEqual(TEXT("Same vertices"), FromDocument.Mesh.Positions.Num(), FromFile.Mesh.Positions.Num());
	TestTrue(TEXT("Same version"), FromDocument.Version == Document->GetVersion());

	// And the spring data read from the document matches the file overload.
	FVRMSpringConfig ConfigFromDocument, ConfigFromFile;
	TMap<int32, FName> NodeMap;
	TMap<int32, int32> NodeParent;
	TMap<int32, FVRMNodeChildren> NodeChildren;
	TestTrue(TEXT("Springs from the document"), VRM::ParseSpringBonesFromDocument(*Document, ConfigFromDocument, NodeMap, NodeParent, NodeChildren, Error));
	TestTrue(TEXT("Springs from the file"), VRM::ParseSpringBonesFromFile(Path, ConfigFromFile, Error));
	TestEqual(TEXT("Same joints"), ConfigFromDocument.Joints.Num(), ConfigFromFile.Joints.Num());
	for (const TPair<int32, FName>& Pair : NodeMap)
	{
		TestEqual(TEXT("Node names match the document"), Pair.Value, Document->GetNodeName(Pair.Key));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMDocumentBadInputTest, "VRM.Document.BadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMDocumentBadInputTest::RunTest(const FString& Parameters)
{
	FString Error;
	TestFalse(TEXT("A missing file fails"), FVRMDocument::LoadFile(TEXT("/no/such/file.vrm"), Error).IsValid());
	TestFalse(TEXT("... with a message"), Error.IsEmpty());

	auto Load = [&Error](const char* Text)
	{
		TArray64<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text));
		return FVRMDocument::LoadBytes(MoveTemp(Bytes), TEXT("memory.gltf"), Error);
	};
	TestFalse(TEXT("Not JSON fails"), Load("not json").IsValid());
	TestFalse(TEXT("Empty fails"), Load("").IsValid());
	TestFalse(TEXT("A truncated GLB header fails"), Load("glTF\x02").IsValid());

	// JSON that parses but isn't valid glTF (an accessor without a buffer) still gives a document:
	// its JSON and nodes are there for the spring parser, its geometry is not.
	const TSharedPtr<const FVRMDocument> NoGeometry = Load(R"({"asset":{"version":"2.0"},"nodes":[{"name":"A"}],"accessors":[{"bufferView":5,"count":1,"componentType":5126,"type":"VEC3"}]})");
	if (TestTrue(TEXT("Document without geometry loads"), NoGeometry.IsValid()))
	{
		TestFalse(TEXT("No geometry"), NoGeometry->HasGeometry());
		TestFalse(TEXT("Geometry error says why"), NoGeometry->GetGeometryError().IsEmpty());
		TestEqual(TEXT("Node name"), NoGeometry->GetNodeName(0), FName(TEXT("A")));
		FVRMParsedModel Model;
		AddExpectedError(TEXT("cgltf"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("No model"), VRM::BuildParsedModel(*NoGeometry, Model));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMDocumentNodeTest, "VRM.Document.PipelineNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMDocumentNodeTest::RunTest(const FString& Parameters)
{
	// The translator leaves the document's JSON and hash in a VRM node (as below, from
	// UVRMTranslator::Translate); the spring pipeline takes them from there, so an import reads the
	// file once.
	const FString Path = VRMDocumentTests::FixturePath(TEXT("vrm1_minimal"));
	FString LoadError;
	const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, LoadError);
	if (!TestTrue(FString::Printf(TEXT("vrm1_minimal loads (%s)"), *LoadError), Document.IsValid()))
	{
		return false;
	}
	UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>();
	UInterchangeVRMNode* NewNode = NewObject<UInterchangeVRMNode>(Container);
	Container->SetupNode(NewNode, TEXT("VRM_vrm1_minimal_Document"), TEXT("VRM_Document"), EInterchangeNodeContainerType::TranslatedAsset);
	NewNode->SetFromDocument(*Document);

	const UInterchangeVRMNode* Node = UInterchangeVRMNode::Find(*Container);
	if (!TestNotNull(TEXT("The container has a VRM node"), Node))
	{
		return false;
	}
	FString Hash;
	TestTrue(TEXT("Hash stored"), Node->GetSourceHash(Hash));
	TestEqual(TEXT("... and it is the file's"), Hash, LexToString(FMD5Hash::HashFile(*Path)));

	FString Error;
	const TSharedPtr<const FVRMDocument> FromNode = Node->MakeDocument(Error);
	if (!TestTrue(FString::Printf(TEXT("Document from the node (%s)"), *Error), FromNode.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("It has no geometry"), FromNode->HasGeometry());
	TestTrue(TEXT("VRM 1.0"), FromNode->GetVersion() == VRM::Coord::EVRMVersion::VRM1);
	FVRMSpringConfig FromNodeConfig, FromFileConfig;
	TMap<int32, FName> NodeMap;
	TMap<int32, int32> NodeParent;
	TMap<int32, FVRMNodeChildren> NodeChildren;
	TestTrue(TEXT("Springs from the node"), VRM::ParseSpringBonesFromDocument(*FromNode, FromNodeConfig, NodeMap, NodeParent, NodeChildren, Error));
	TestTrue(TEXT("Springs from the file"), VRM::ParseSpringBonesFromFile(Path, FromFileConfig, Error));
	TestEqual(TEXT("Same joints"), FromNodeConfig.Joints.Num(), FromFileConfig.Joints.Num());
	TestEqual(TEXT("Same springs"), FromNodeConfig.Springs.Num(), FromFileConfig.Springs.Num());

	// The pipeline gets its spring data from the node: it doesn't need the file, which here isn't
	// where the source data says.
	UInterchangeSourceData* Moved = NewObject<UInterchangeSourceData>();
	Moved->SetFilename(FPaths::Combine(FPaths::GetPath(Path), TEXT("moved_away"), TEXT("vrm1_minimal.vrm")));
	UVRMSpringBonesPostImportPipeline* Pipeline = NewObject<UVRMSpringBonesPostImportPipeline>();
	Pipeline->bGenerateSpringBoneData = true;
	Pipeline->bGeneratePostProcessAnimBP = false;
	Pipeline->ExecutePipeline(Container, { Moved }, TEXT("/Game/VRMDocumentTests"));
	TestTrue(TEXT("Spring data staged without reading the file"), Pipeline->HasPendingPostImportWork());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
