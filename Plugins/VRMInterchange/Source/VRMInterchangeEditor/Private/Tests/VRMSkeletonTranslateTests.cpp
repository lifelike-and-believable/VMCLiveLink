// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// The translator's skeleton: a joint node per parsed bone, with its parent and bind pose, in the form
// the running engine reads (UE 5.8 reads joints and bind poses only from UInterchangeJointNode).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "InterchangeManager.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMDocument.h"
#include "VRMImportMessages.h"
#include "VRMParsedModel.h"
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
#include "InterchangeJointNode.h"
#endif

namespace VRMSkeletonTranslateTests
{
	FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm")) : FString();
	}

	// Whether the engine treats the node as a joint, and its bind pose if it has one.
	bool IsJoint(const UInterchangeSceneNode& Node)
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		return Node.IsA<UInterchangeJointNode>();
#else
		return Node.IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());
#endif
	}

	bool GetBindPose(const UInterchangeSceneNode& Node, FTransform& Out)
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		const UInterchangeJointNode* Joint = Cast<UInterchangeJointNode>(&Node);
		return Joint && Joint->GetBindPoseLocalTransform(Out);
#else
		return Node.GetCustomBindPoseLocalTransform(Out);
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSkeletonTranslateTest, "VRM.Skeleton.Translate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSkeletonTranslateTest::RunTest(const FString& Parameters)
{
	using namespace VRMSkeletonTranslateTests;

	// bind_pose_offset has a three-bone chain, each bone offset from its parent.
	const FString Path = FixturePath(TEXT("bind_pose_offset"));
	FString LoadError;
	const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, LoadError);
	FVRMParsedModel Expected;
	if (!TestTrue(FString::Printf(TEXT("Fixture loads (%s)"), *LoadError), Document.IsValid())
		|| !TestTrue(TEXT("Fixture parses"), VRM::BuildParsedModel(*Document, Expected))
		|| !TestTrue(TEXT("Fixture has bones"), Expected.Bones.Num() >= 3))
	{
		return false;
	}

	UInterchangeSourceData* Source = UInterchangeManager::CreateSourceData(Path);
	UInterchangeTranslatorBase* Translator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(Source);
	if (!TestNotNull(TEXT("A translator for .vrm"), Translator))
	{
		return false;
	}
	UInterchangeBaseNodeContainer* Container = NewObject<UInterchangeBaseNodeContainer>();
	const bool bTranslated = Translator->Translate(*Container);
	VRM::ImportMessages::Take(Source->GetFilename()); // the import's message bucket, which no report closes here
	if (!TestTrue(TEXT("Translates"), bTranslated))
	{
		return false;
	}

	TMap<FString, const UInterchangeSceneNode*> ByName;
	Container->IterateNodesOfType<UInterchangeSceneNode>([&ByName](const FString&, UInterchangeSceneNode* Node)
	{
		ByName.Add(Node->GetDisplayLabel(), Node);
	});

	const UInterchangeSceneNode* const* Root = ByName.Find(TEXT("VRM_Root"));
	if (!TestNotNull(TEXT("Root joint"), Root))
	{
		return false;
	}
	TestTrue(TEXT("Root is a joint"), IsJoint(**Root));
	FTransform RootBind;
	TestTrue(TEXT("Root has a bind pose"), GetBindPose(**Root, RootBind));
	TestTrue(TEXT("Root bind pose is identity"), RootBind.Equals(FTransform::Identity, 1e-4f));

	for (const FVRMParsedBone& Bone : Expected.Bones)
	{
		const UInterchangeSceneNode* const* Found = ByName.Find(Bone.Name);
		if (!TestNotNull(*FString::Printf(TEXT("Joint %s"), *Bone.Name), Found))
		{
			continue;
		}
		const UInterchangeSceneNode& Node = **Found;
		TestTrue(*FString::Printf(TEXT("%s is a joint"), *Bone.Name), IsJoint(Node));

		const UInterchangeSceneNode* ExpectedParent = Bone.Parent == INDEX_NONE ? *Root : ByName.FindRef(Expected.Bones[Bone.Parent].Name);
		TestEqual(*FString::Printf(TEXT("%s parent"), *Bone.Name), Node.GetParentUid(), ExpectedParent ? ExpectedParent->GetUniqueID() : FString());

		FTransform Bind;
		if (TestTrue(*FString::Printf(TEXT("%s has a bind pose"), *Bone.Name), GetBindPose(Node, Bind)))
		{
			TestTrue(*FString::Printf(TEXT("%s bind pose (%s, expected %s)"), *Bone.Name, *Bind.GetLocation().ToString(), *Bone.LocalBind.GetLocation().ToString()),
				Bind.Equals(Bone.LocalBind, 1e-3f));
		}
		FTransform Local;
		TestTrue(*FString::Printf(TEXT("%s local transform is its bind pose"), *Bone.Name), Node.GetCustomLocalTransform(Local) && Local.Equals(Bone.LocalBind, 1e-3f));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
