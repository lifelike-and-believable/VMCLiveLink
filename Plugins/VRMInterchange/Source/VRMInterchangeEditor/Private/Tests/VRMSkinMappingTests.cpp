// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "VRMTranslator.h"

namespace VRMSkinMappingTests
{
	static FString FixturePath(const TCHAR* Name, const TCHAR* Extension)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + Extension)
			: FString();
	}

	static TSharedPtr<FJsonObject> LoadExpected(const TCHAR* Name)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FixturePath(Name, TEXT(".expected.json"))))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	/** Index of the bone with the largest weight for a vertex. */
	static int32 DominantBone(const FVRMParsedMesh::FWeight& W)
	{
		int32 Best = 0;
		for (int32 k = 1; k < 4; ++k)
		{
			if (W.Weight[k] > W.Weight[Best])
			{
				Best = k;
			}
		}
		return W.BoneIndex[Best];
	}

	/** Expected UE position, converted like the importer converts this file (axes, scale and facing). */
	static FVector ExpectedPosition(const TSharedPtr<FJsonObject>& Object, const FVRMParsedModel& Model)
	{
		const TArray<TSharedPtr<FJsonValue>>& P = Object->GetArrayField(TEXT("world_position_gltf"));
		return VRM::GltfPositionToUE(FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()), Model.GlobalScale, Model.Version);
	}

	/** Component-space rest position of a bone (bones are ordered parents first). */
	static FVector BoneRestPosition(const FVRMParsedModel& Model, int32 BoneIndex)
	{
		FTransform Xf = Model.Bones[BoneIndex].LocalBind;
		for (int32 P = Model.Bones[BoneIndex].Parent; Model.Bones.IsValidIndex(P); P = Model.Bones[P].Parent)
		{
			Xf = Xf * Model.Bones[P].LocalBind;
		}
		return Xf.GetTranslation();
	}

	/** Positions are in UE units (cm); fixtures are exact to 1e-6 m. */
	static constexpr double PositionTolerance = 0.01;

	/**
	 * Checks one fixture: every expected joint is a bone at its rest position, bone names are
	 * unique, parents precede children, and every vertex (skinned or rigid, one set per mesh node)
	 * is dominated by the bone of the expected joint node and sits at its expected rest position.
	 */
	static void CheckFixture(FAutomationTestBase& Test, const TCHAR* Name)
	{
		FVRMParsedModel Model;
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads"), Name), VRM::LoadVRMFile(FixturePath(Name, TEXT(".vrm")), Model)))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Expected = LoadExpected(Name);
		if (!Test.TestTrue(FString::Printf(TEXT("%s expected values load"), Name), Expected.IsValid()))
		{
			return;
		}

		TSet<FName> Names;
		for (int32 i = 0; i < Model.Bones.Num(); ++i)
		{
			const FVRMParsedBone& Bone = Model.Bones[i];
			bool bDuplicate = false;
			Names.Add(FName(*Bone.Name), &bDuplicate);
			Test.TestFalse(FString::Printf(TEXT("%s: bone name '%s' is unique"), Name, *Bone.Name), bDuplicate);
			Test.TestTrue(FString::Printf(TEXT("%s: parent of '%s' precedes it"), Name, *Bone.Name), Bone.Parent < i);
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Expected->GetObjectField(TEXT("bones"))->Values)
		{
			const int32 Node = int32(Pair.Value->AsObject()->GetNumberField(TEXT("node")));
			const int32 BoneIndex = Model.Bones.IndexOfByPredicate([Node](const FVRMParsedBone& B) { return B.NodeIndex == Node; });
			if (!Test.TestTrue(FString::Printf(TEXT("%s: joint node %d is a bone"), Name, Node), Model.NodeToBoneMap.Contains(Node) && BoneIndex != INDEX_NONE))
			{
				continue;
			}
			const FVector ExpectedBonePos = ExpectedPosition(Pair.Value->AsObject(), Model);
			const FVector ActualBonePos = BoneRestPosition(Model, BoneIndex);
			Test.TestTrue(FString::Printf(TEXT("%s: joint node %d rest position %s (expected %s)"), Name, Node, *ActualBonePos.ToString(), *ExpectedBonePos.ToString()),
				ActualBonePos.Equals(ExpectedBonePos, PositionTolerance));
		}

		const TSharedPtr<FJsonObject>* ExpectedNames = nullptr;
		if (Expected->TryGetObjectField(TEXT("expected_bone_names_by_node"), ExpectedNames))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ExpectedNames)->Values)
			{
				const FName* Actual = Model.NodeToBoneMap.Find(FCString::Atoi(*Pair.Key));
				Test.TestEqual(FString::Printf(TEXT("%s: name of node %s"), Name, *Pair.Key),
					Actual ? Actual->ToString() : FString(), Pair.Value->AsString());
			}
		}

		const TArray<TSharedPtr<FJsonValue>>& Vertices = Expected->GetArrayField(TEXT("vertices"));
		Test.TestEqual(FString::Printf(TEXT("%s: vertex count"), Name), Model.Mesh.Positions.Num(), Vertices.Num());
		Test.TestEqual(FString::Printf(TEXT("%s: skin weights for every vertex"), Name), Model.Mesh.SkinWeights.Num(), Model.Mesh.Positions.Num());

		int32 VertexIndex = 0;
		for (const TSharedPtr<FJsonValue>& Value : Vertices)
		{
			const TSharedPtr<FJsonObject> V = Value->AsObject();
			const FString MeshNode = V->GetStringField(TEXT("mesh_node"));
			if (!Model.Mesh.SkinWeights.IsValidIndex(VertexIndex) || !Model.Mesh.Positions.IsValidIndex(VertexIndex))
			{
				Test.AddError(FString::Printf(TEXT("%s: missing vertex %d"), Name, VertexIndex));
				return;
			}

			const int32 ExpectedNode = int32(V->GetNumberField(TEXT("dominant_bone_node")));
			const FName* ExpectedBone = Model.NodeToBoneMap.Find(ExpectedNode);
			const int32 ActualBoneIndex = DominantBone(Model.Mesh.SkinWeights[VertexIndex]);
			const FString ActualBone = Model.Bones.IsValidIndex(ActualBoneIndex) ? Model.Bones[ActualBoneIndex].Name : FString();
			Test.TestEqual(FString::Printf(TEXT("%s: vertex %s#%d bound to the right bone"), Name, *MeshNode, int32(V->GetNumberField(TEXT("vertex")))),
				ActualBone, ExpectedBone ? ExpectedBone->ToString() : FString(TEXT("<missing bone>")));

			const FVector ExpectedPos = ExpectedPosition(V, Model);
			const FVector ActualPos = FVector(Model.Mesh.Positions[VertexIndex]);
			Test.TestTrue(FString::Printf(TEXT("%s: vertex %s#%d rest position %s (expected %s)"), Name, *MeshNode, int32(V->GetNumberField(TEXT("vertex"))), *ActualPos.ToString(), *ExpectedPos.ToString()),
				ActualPos.Equals(ExpectedPos, PositionTolerance));
			++VertexIndex;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSkinMappingTest, "VRM.Translator.SkinMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSkinMappingTest::RunTest(const FString& Parameters)
{
	static const TCHAR* const Fixtures[] = {
		TEXT("vrm0_minimal"),
		TEXT("vrm1_minimal"),
		TEXT("multi_skin"),
		TEXT("rigid_accessory"),
		TEXT("unnamed_and_duplicate_nodes"),
		TEXT("armature_transform"),
		TEXT("bind_pose_offset"),
	};
	for (const TCHAR* Name : Fixtures)
	{
		VRMSkinMappingTests::CheckFixture(*this, Name);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
