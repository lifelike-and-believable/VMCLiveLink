// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	// VRM 1.0 bone names, in EVRMHumanBone order (after None).
	const TCHAR* const HumanBoneNames[] = {
		TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("upperChest"), TEXT("neck"), TEXT("head"), TEXT("leftEye"), TEXT("rightEye"), TEXT("jaw"),
		TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot"), TEXT("leftToes"),
		TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot"), TEXT("rightToes"),
		TEXT("leftShoulder"), TEXT("leftUpperArm"), TEXT("leftLowerArm"), TEXT("leftHand"),
		TEXT("rightShoulder"), TEXT("rightUpperArm"), TEXT("rightLowerArm"), TEXT("rightHand"),
		TEXT("leftThumbMetacarpal"), TEXT("leftThumbProximal"), TEXT("leftThumbDistal"),
		TEXT("leftIndexProximal"), TEXT("leftIndexIntermediate"), TEXT("leftIndexDistal"),
		TEXT("leftMiddleProximal"), TEXT("leftMiddleIntermediate"), TEXT("leftMiddleDistal"),
		TEXT("leftRingProximal"), TEXT("leftRingIntermediate"), TEXT("leftRingDistal"),
		TEXT("leftLittleProximal"), TEXT("leftLittleIntermediate"), TEXT("leftLittleDistal"),
		TEXT("rightThumbMetacarpal"), TEXT("rightThumbProximal"), TEXT("rightThumbDistal"),
		TEXT("rightIndexProximal"), TEXT("rightIndexIntermediate"), TEXT("rightIndexDistal"),
		TEXT("rightMiddleProximal"), TEXT("rightMiddleIntermediate"), TEXT("rightMiddleDistal"),
		TEXT("rightRingProximal"), TEXT("rightRingIntermediate"), TEXT("rightRingDistal"),
		TEXT("rightLittleProximal"), TEXT("rightLittleIntermediate"), TEXT("rightLittleDistal"),
	};
	static_assert(UE_ARRAY_COUNT(HumanBoneNames) == int32(EVRMHumanBone::Count) - 1, "One name per humanoid bone");

	// VRM 1.0 preset names, in EVRMExpressionPreset order (after Custom).
	const TCHAR* const PresetNames[] = {
		TEXT("happy"), TEXT("angry"), TEXT("sad"), TEXT("relaxed"), TEXT("surprised"),
		TEXT("aa"), TEXT("ih"), TEXT("ou"), TEXT("ee"), TEXT("oh"),
		TEXT("blink"), TEXT("blinkLeft"), TEXT("blinkRight"),
		TEXT("lookUp"), TEXT("lookDown"), TEXT("lookLeft"), TEXT("lookRight"),
		TEXT("neutral"),
	};
	static_assert(UE_ARRAY_COUNT(PresetNames) == int32(EVRMExpressionPreset::Neutral), "One name per preset");

	const FJsonObject* JObject(const FJsonObject* Parent, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Parent && Parent->TryGetObjectField(Field, Value) && Value && Value->IsValid() ? Value->Get() : nullptr;
	}

	const FJsonArray* JArray(const FJsonObject* Parent, const TCHAR* Field)
	{
		const FJsonArray* Value = nullptr;
		return Parent && Parent->TryGetArrayField(Field, Value) ? Value : nullptr;
	}

	FString JString(const FJsonObject* Parent, const TCHAR* Field)
	{
		FString Value;
		if (Parent)
		{
			Parent->TryGetStringField(Field, Value);
		}
		return Value;
	}

	double JNumber(const FJsonObject* Parent, const TCHAR* Field, double Default)
	{
		double Value = Default;
		if (Parent)
		{
			Parent->TryGetNumberField(Field, Value);
		}
		return Value;
	}

	int32 JIndex(const FJsonObject* Parent, const TCHAR* Field)
	{
		double Value = -1.0;
		return Parent && Parent->TryGetNumberField(Field, Value) && Value >= 0.0 ? int32(Value) : INDEX_NONE;
	}

	bool JBool(const FJsonObject* Parent, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		if (Parent)
		{
			Parent->TryGetBoolField(Field, bValue);
		}
		return bValue;
	}

	const FJsonObject* JArrayObject(const FJsonArray* Values, int32 Index)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Values && Values->IsValidIndex(Index) && (*Values)[Index].IsValid() && (*Values)[Index]->TryGetObject(Value) && Value && Value->IsValid()
			? Value->Get() : nullptr;
	}

	/** [x, y, z] (VRM 1.0) or {x, y, z} (VRM 0.x). */
	FVector JVector3(const FJsonObject* Parent, const TCHAR* Field)
	{
		if (const FJsonArray* Values = JArray(Parent, Field))
		{
			double V[3] = { 0, 0, 0 };
			for (int32 i = 0; i < 3 && i < Values->Num(); ++i)
			{
				(*Values)[i]->TryGetNumber(V[i]);
			}
			return FVector(V[0], V[1], V[2]);
		}
		if (const FJsonObject* Obj = JObject(Parent, Field))
		{
			return FVector(JNumber(Obj, TEXT("x"), 0), JNumber(Obj, TEXT("y"), 0), JNumber(Obj, TEXT("z"), 0));
		}
		return FVector::ZeroVector;
	}

	FVector2D JVector2(const FJsonArray* Values, const FVector2D& Default)
	{
		FVector2D V = Default;
		if (Values && Values->Num() >= 2)
		{
			(*Values)[0]->TryGetNumber(V.X);
			(*Values)[1]->TryGetNumber(V.Y);
		}
		return V;
	}

	FLinearColor JColor(const FJsonArray* Values)
	{
		double C[4] = { 1, 1, 1, 1 };
		for (int32 i = 0; Values && i < 4 && i < Values->Num(); ++i)
		{
			(*Values)[i]->TryGetNumber(C[i]);
		}
		return FLinearColor(float(C[0]), float(C[1]), float(C[2]), float(C[3]));
	}

	EVRMExpressionOverride JOverride(const FJsonObject* Expression, const TCHAR* Field)
	{
		const FString Value = JString(Expression, Field);
		return Value == TEXT("block") ? EVRMExpressionOverride::Block : Value == TEXT("blend") ? EVRMExpressionOverride::Blend : EVRMExpressionOverride::None;
	}

	EVRMFirstPersonType JFirstPersonType(const FString& Value)
	{
		if (Value.Equals(TEXT("both"), ESearchCase::IgnoreCase)) return EVRMFirstPersonType::Both;
		if (Value.Equals(TEXT("thirdPersonOnly"), ESearchCase::IgnoreCase)) return EVRMFirstPersonType::ThirdPersonOnly;
		if (Value.Equals(TEXT("firstPersonOnly"), ESearchCase::IgnoreCase)) return EVRMFirstPersonType::FirstPersonOnly;
		return EVRMFirstPersonType::Auto;
	}

	FVRMLookAtRange JRange(const FJsonObject* Map, const TCHAR* InputField, const TCHAR* OutputField)
	{
		FVRMLookAtRange R;
		if (Map)
		{
			R.InputMaxValue = float(JNumber(Map, InputField, R.InputMaxValue));
			R.OutputScale = float(JNumber(Map, OutputField, R.OutputScale));
		}
		return R;
	}

	/** Reads what BuildAvatarData needs from the document and the model. */
	struct FReader
	{
		const FVRMDocument& Document;
		const FVRMParsedModel& Model;
		const FJsonObject& Root;
		TArray<FString>* Warnings;
		EVRMAvatarVersion Version = EVRMAvatarVersion::Unknown;

		void Warn(FString&& Message) const
		{
			if (Warnings)
			{
				Warnings->Add(MoveTemp(Message));
			}
		}

		/** The skeleton bone a node became, or the node's name if it isn't a joint. */
		FName BoneOfNode(int32 NodeIndex) const
		{
			if (const FName* Bone = Model.NodeToBoneMap.Find(NodeIndex))
			{
				return *Bone;
			}
			return Document.GetNodeName(NodeIndex);
		}

		int32 MeshOfNode(int32 NodeIndex) const
		{
			return JIndex(JArrayObject(JArray(&Root, TEXT("nodes")), NodeIndex), TEXT("mesh"));
		}

		FString MeshName(int32 MeshIndex) const
		{
			return JString(JArrayObject(JArray(&Root, TEXT("meshes")), MeshIndex), TEXT("name"));
		}

		FString MaterialName(int32 MaterialIndex) const
		{
			return JString(JArrayObject(JArray(&Root, TEXT("materials")), MaterialIndex), TEXT("name"));
		}

		/** Adds a morph bind if the mesh's target was imported. */
		void AddMorphBind(FVRMExpression& Expression, int32 MeshIndex, int32 TargetIndex, float Weight) const
		{
			const TArray<FString>* Names = MeshIndex != INDEX_NONE ? Model.MeshMorphNames.Find(MeshIndex) : nullptr;
			if (!Names || !Names->IsValidIndex(TargetIndex) || (*Names)[TargetIndex].IsEmpty())
			{
				Warn(FString::Printf(TEXT("Expression '%s': mesh %d has no imported morph target %d; the bind is skipped."),
					*Expression.Name.ToString(), MeshIndex, TargetIndex));
				return;
			}
			FVRMMorphBind& Bind = Expression.MorphBinds.AddDefaulted_GetRef();
			Bind.MorphTarget = FName(*(*Names)[TargetIndex]);
			Bind.Weight = Weight;
			Bind.MeshIndex = MeshIndex;
			Bind.TargetIndex = TargetIndex;
		}

		void AddHumanBone(FVRMAvatarData& Out, const FString& BoneName, int32 NodeIndex) const
		{
			const EVRMHumanBone Bone = VRM::HumanBoneFromName(BoneName, Version);
			if (Bone == EVRMHumanBone::None)
			{
				Warn(FString::Printf(TEXT("Unknown humanoid bone '%s' is skipped."), *BoneName));
				return;
			}
			const FName Target = BoneOfNode(NodeIndex);
			if (Target.IsNone())
			{
				Warn(FString::Printf(TEXT("Humanoid bone '%s' points at node %d, which has no name; it is skipped."), *BoneName, NodeIndex));
				return;
			}
			Out.HumanoidToBone.Add(Bone, Target);
		}

		void ReadVRM1(const FJsonObject& Vrm, FVRMAvatarData& Out) const
		{
			// Meta
			if (const FJsonObject* Meta = JObject(&Vrm, TEXT("meta")))
			{
				FVRMMeta& M = Out.Meta;
				M.Name = JString(Meta, TEXT("name"));
				M.Version = JString(Meta, TEXT("version"));
				Meta->TryGetStringArrayField(TEXT("authors"), M.Authors);
				M.CopyrightInformation = JString(Meta, TEXT("copyrightInformation"));
				M.ContactInformation = JString(Meta, TEXT("contactInformation"));
				Meta->TryGetStringArrayField(TEXT("references"), M.References);
				M.ThirdPartyLicenses = JString(Meta, TEXT("thirdPartyLicenses"));
				M.LicenseUrl = JString(Meta, TEXT("licenseUrl"));
				M.OtherLicenseUrl = JString(Meta, TEXT("otherLicenseUrl"));
				M.AvatarPermission = JString(Meta, TEXT("avatarPermission"));
				M.CommercialUsage = JString(Meta, TEXT("commercialUsage"));
				M.CreditNotation = JString(Meta, TEXT("creditNotation"));
				M.Modification = JString(Meta, TEXT("modification"));
				M.bAllowExcessivelyViolentUsage = JBool(Meta, TEXT("allowExcessivelyViolentUsage"));
				M.bAllowExcessivelySexualUsage = JBool(Meta, TEXT("allowExcessivelySexualUsage"));
				M.bAllowPoliticalOrReligiousUsage = JBool(Meta, TEXT("allowPoliticalOrReligiousUsage"));
				M.bAllowAntisocialOrHateUsage = JBool(Meta, TEXT("allowAntisocialOrHateUsage"));
				M.bAllowRedistribution = JBool(Meta, TEXT("allowRedistribution"));
				M.ThumbnailImage = JIndex(Meta, TEXT("thumbnailImage"));
			}

			// Humanoid: humanBones { name: { node } }
			if (const FJsonObject* Bones = JObject(JObject(&Vrm, TEXT("humanoid")), TEXT("humanBones")))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Bones->Values)
				{
					const TSharedPtr<FJsonObject>* Bone = nullptr;
					if (Pair.Value.IsValid() && Pair.Value->TryGetObject(Bone) && Bone)
					{
						AddHumanBone(Out, Pair.Key, JIndex(Bone->Get(), TEXT("node")));
					}
				}
			}

			// Expressions: preset { name: expression } and custom { name: expression }
			const FJsonObject* Expressions = JObject(&Vrm, TEXT("expressions"));
			for (const TCHAR* Group : { TEXT("preset"), TEXT("custom") })
			{
				const FJsonObject* Entries = JObject(Expressions, Group);
				if (!Entries)
				{
					continue;
				}
				const bool bPreset = FCString::Strcmp(Group, TEXT("preset")) == 0;
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Entries->Values)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Entry) || !Entry)
					{
						continue;
					}
					const FJsonObject* E = Entry->Get();
					FVRMExpression& Expression = Out.Expressions.AddDefaulted_GetRef();
					Expression.Name = FName(*Pair.Key);
					Expression.Preset = bPreset ? VRM::ExpressionPresetFromName(Pair.Key, Version) : EVRMExpressionPreset::Custom;
					Expression.bIsBinary = JBool(E, TEXT("isBinary"));
					Expression.OverrideBlink = JOverride(E, TEXT("overrideBlink"));
					Expression.OverrideLookAt = JOverride(E, TEXT("overrideLookAt"));
					Expression.OverrideMouth = JOverride(E, TEXT("overrideMouth"));

					const FJsonArray* MorphBinds = JArray(E, TEXT("morphTargetBinds"));
					for (int32 i = 0; MorphBinds && i < MorphBinds->Num(); ++i)
					{
						const FJsonObject* Bind = JArrayObject(MorphBinds, i);
						AddMorphBind(Expression, MeshOfNode(JIndex(Bind, TEXT("node"))), JIndex(Bind, TEXT("index")), float(JNumber(Bind, TEXT("weight"), 1.0)));
					}
					const FJsonArray* ColorBinds = JArray(E, TEXT("materialColorBinds"));
					for (int32 i = 0; ColorBinds && i < ColorBinds->Num(); ++i)
					{
						const FJsonObject* Bind = JArrayObject(ColorBinds, i);
						FVRMMaterialColorBind& C = Expression.MaterialColorBinds.AddDefaulted_GetRef();
						C.Material = MaterialName(JIndex(Bind, TEXT("material")));
						C.Property = JString(Bind, TEXT("type"));
						C.TargetValue = JColor(JArray(Bind, TEXT("targetValue")));
					}
					const FJsonArray* TextureBinds = JArray(E, TEXT("textureTransformBinds"));
					for (int32 i = 0; TextureBinds && i < TextureBinds->Num(); ++i)
					{
						const FJsonObject* Bind = JArrayObject(TextureBinds, i);
						FVRMTextureTransformBind& T = Expression.TextureTransformBinds.AddDefaulted_GetRef();
						T.Material = MaterialName(JIndex(Bind, TEXT("material")));
						T.Scale = JVector2(JArray(Bind, TEXT("scale")), FVector2D(1.0, 1.0));
						T.Offset = JVector2(JArray(Bind, TEXT("offset")), FVector2D::ZeroVector);
					}
				}
			}

			// Look-at
			if (const FJsonObject* LookAt = JObject(&Vrm, TEXT("lookAt")))
			{
				Out.LookAt.Type = JString(LookAt, TEXT("type")) == TEXT("expression") ? EVRMLookAtType::Expression : EVRMLookAtType::Bone;
				Out.LookAt.OffsetFromHeadBone = VRM::Coord::FVRMAxisConvention::ForVersion(VRM::Coord::EVRMVersion::VRM1).Position(JVector3(LookAt, TEXT("offsetFromHeadBone")));
				Out.LookAt.HorizontalInner = JRange(JObject(LookAt, TEXT("rangeMapHorizontalInner")), TEXT("inputMaxValue"), TEXT("outputScale"));
				Out.LookAt.HorizontalOuter = JRange(JObject(LookAt, TEXT("rangeMapHorizontalOuter")), TEXT("inputMaxValue"), TEXT("outputScale"));
				Out.LookAt.VerticalDown = JRange(JObject(LookAt, TEXT("rangeMapVerticalDown")), TEXT("inputMaxValue"), TEXT("outputScale"));
				Out.LookAt.VerticalUp = JRange(JObject(LookAt, TEXT("rangeMapVerticalUp")), TEXT("inputMaxValue"), TEXT("outputScale"));
			}

			// First person: meshAnnotations [{ node, type }]
			const FJsonArray* Annotations = JArray(JObject(&Vrm, TEXT("firstPerson")), TEXT("meshAnnotations"));
			for (int32 i = 0; Annotations && i < Annotations->Num(); ++i)
			{
				const FJsonObject* A = JArrayObject(Annotations, i);
				FVRMFirstPersonAnnotation& Annotation = Out.FirstPerson.AddDefaulted_GetRef();
				Annotation.Index = JIndex(A, TEXT("node"));
				Annotation.Mesh = Document.GetNodeName(Annotation.Index).ToString();
				Annotation.Type = JFirstPersonType(JString(A, TEXT("type")));
			}
		}

		void ReadVRM0(const FJsonObject& Vrm, FVRMAvatarData& Out) const
		{
			// Meta (0.x names and values mapped onto 1.0's)
			if (const FJsonObject* Meta = JObject(&Vrm, TEXT("meta")))
			{
				FVRMMeta& M = Out.Meta;
				M.Name = JString(Meta, TEXT("title"));
				M.Version = JString(Meta, TEXT("version"));
				const FString Author = JString(Meta, TEXT("author"));
				if (!Author.IsEmpty())
				{
					M.Authors.Add(Author);
				}
				M.ContactInformation = JString(Meta, TEXT("contactInformation"));
				const FString Reference = JString(Meta, TEXT("reference"));
				if (!Reference.IsEmpty())
				{
					M.References.Add(Reference);
				}
				M.LicenseName = JString(Meta, TEXT("licenseName"));
				M.OtherLicenseUrl = JString(Meta, TEXT("otherLicenseUrl"));
				const FString Allowed = JString(Meta, TEXT("allowedUserName"));
				M.AvatarPermission = Allowed == TEXT("Everyone") ? TEXT("everyone")
					: Allowed == TEXT("ExplicitlyLicensedPerson") ? TEXT("onlySeparatelyLicensedPerson")
					: Allowed == TEXT("OnlyAuthor") ? TEXT("onlyAuthor") : FString();
				// The 0.x spellings ("Ussage") are the spec's.
				M.bAllowExcessivelyViolentUsage = JString(Meta, TEXT("violentUssageName")) == TEXT("Allow");
				M.bAllowExcessivelySexualUsage = JString(Meta, TEXT("sexualUssageName")) == TEXT("Allow");
				const FString Commercial = JString(Meta, TEXT("commercialUssageName"));
				M.CommercialUsage = Commercial == TEXT("Allow") ? TEXT("corporation") : Commercial == TEXT("Disallow") ? TEXT("personalNonProfit") : FString();
				// meta.texture is a texture index; the thumbnail is that texture's image.
				M.ThumbnailImage = JIndex(JArrayObject(JArray(&Root, TEXT("textures")), JIndex(Meta, TEXT("texture"))), TEXT("source"));
			}

			// Humanoid: humanBones [{ bone, node }]
			const FJsonArray* Bones = JArray(JObject(&Vrm, TEXT("humanoid")), TEXT("humanBones"));
			for (int32 i = 0; Bones && i < Bones->Num(); ++i)
			{
				const FJsonObject* Bone = JArrayObject(Bones, i);
				AddHumanBone(Out, JString(Bone, TEXT("bone")), JIndex(Bone, TEXT("node")));
			}

			// Expressions: blendShapeMaster.blendShapeGroups [{ name, presetName, binds, materialValues, isBinary }]
			const FJsonArray* Groups = JArray(JObject(&Vrm, TEXT("blendShapeMaster")), TEXT("blendShapeGroups"));
			for (int32 i = 0; Groups && i < Groups->Num(); ++i)
			{
				const FJsonObject* G = JArrayObject(Groups, i);
				if (!G)
				{
					continue;
				}
				FVRMExpression& Expression = Out.Expressions.AddDefaulted_GetRef();
				Expression.Name = FName(*JString(G, TEXT("name")));
				Expression.Preset = VRM::ExpressionPresetFromName(JString(G, TEXT("presetName")), Version);
				Expression.bIsBinary = JBool(G, TEXT("isBinary"));
				const FJsonArray* Binds = JArray(G, TEXT("binds"));
				for (int32 b = 0; Binds && b < Binds->Num(); ++b)
				{
					const FJsonObject* Bind = JArrayObject(Binds, b);
					// VRM 0.x weights are 0 to 100.
					AddMorphBind(Expression, JIndex(Bind, TEXT("mesh")), JIndex(Bind, TEXT("index")), float(JNumber(Bind, TEXT("weight"), 100.0) / 100.0));
				}
				const FJsonArray* Values = JArray(G, TEXT("materialValues"));
				for (int32 v = 0; Values && v < Values->Num(); ++v)
				{
					const FJsonObject* Value = JArrayObject(Values, v);
					const FString Property = JString(Value, TEXT("propertyName"));
					if (Property.EndsWith(TEXT("_ST")))
					{
						// _MainTex_ST and friends: scale (xy) and offset (zw).
						const FLinearColor ST = JColor(JArray(Value, TEXT("targetValue")));
						FVRMTextureTransformBind& T = Expression.TextureTransformBinds.AddDefaulted_GetRef();
						T.Material = JString(Value, TEXT("materialName"));
						T.Scale = FVector2D(ST.R, ST.G);
						T.Offset = FVector2D(ST.B, ST.A);
					}
					else
					{
						FVRMMaterialColorBind& C = Expression.MaterialColorBinds.AddDefaulted_GetRef();
						C.Material = JString(Value, TEXT("materialName"));
						C.Property = Property;
						C.TargetValue = JColor(JArray(Value, TEXT("targetValue")));
					}
				}
			}

			// Look-at and first person live together in 0.x.
			if (const FJsonObject* FirstPerson = JObject(&Vrm, TEXT("firstPerson")))
			{
				Out.LookAt.Type = JString(FirstPerson, TEXT("lookAtTypeName")) == TEXT("BlendShape") ? EVRMLookAtType::Expression : EVRMLookAtType::Bone;
				Out.LookAt.OffsetFromHeadBone = VRM::Coord::FVRMAxisConvention::ForVersion(VRM::Coord::EVRMVersion::VRM0).Position(JVector3(FirstPerson, TEXT("firstPersonBoneOffset")));
				Out.LookAt.HorizontalInner = JRange(JObject(FirstPerson, TEXT("lookAtHorizontalInner")), TEXT("xRange"), TEXT("yRange"));
				Out.LookAt.HorizontalOuter = JRange(JObject(FirstPerson, TEXT("lookAtHorizontalOuter")), TEXT("xRange"), TEXT("yRange"));
				Out.LookAt.VerticalDown = JRange(JObject(FirstPerson, TEXT("lookAtVerticalDown")), TEXT("xRange"), TEXT("yRange"));
				Out.LookAt.VerticalUp = JRange(JObject(FirstPerson, TEXT("lookAtVerticalUp")), TEXT("xRange"), TEXT("yRange"));

				const FJsonArray* Annotations = JArray(FirstPerson, TEXT("meshAnnotations"));
				for (int32 i = 0; Annotations && i < Annotations->Num(); ++i)
				{
					const FJsonObject* A = JArrayObject(Annotations, i);
					FVRMFirstPersonAnnotation& Annotation = Out.FirstPerson.AddDefaulted_GetRef();
					Annotation.Index = JIndex(A, TEXT("mesh"));
					Annotation.Mesh = MeshName(Annotation.Index);
					Annotation.Type = JFirstPersonType(JString(A, TEXT("firstPersonFlag")));
				}
			}
		}
	};
}

namespace VRM
{
	EVRMHumanBone HumanBoneFromName(const FString& Name, EVRMAvatarVersion Version)
	{
		FString Lookup = Name;
		if (Version == EVRMAvatarVersion::VRM0)
		{
			// VRM 0.x thumbs are Proximal, Intermediate, Distal; VRM 1.0's are Metacarpal, Proximal, Distal.
			for (const TCHAR* Side : { TEXT("left"), TEXT("right") })
			{
				const FString Prefix = FString(Side) + TEXT("Thumb");
				if (Name.Equals(Prefix + TEXT("Proximal"), ESearchCase::IgnoreCase)) { Lookup = Prefix + TEXT("Metacarpal"); }
				else if (Name.Equals(Prefix + TEXT("Intermediate"), ESearchCase::IgnoreCase)) { Lookup = Prefix + TEXT("Proximal"); }
			}
		}
		for (int32 i = 0; i < UE_ARRAY_COUNT(HumanBoneNames); ++i)
		{
			if (Lookup.Equals(HumanBoneNames[i], ESearchCase::IgnoreCase))
			{
				return EVRMHumanBone(i + 1);
			}
		}
		return EVRMHumanBone::None;
	}

	FString HumanBoneName(EVRMHumanBone Bone)
	{
		const int32 i = int32(Bone) - 1;
		return i >= 0 && i < UE_ARRAY_COUNT(HumanBoneNames) ? FString(HumanBoneNames[i]) : FString();
	}

	EVRMExpressionPreset ExpressionPresetFromName(const FString& Name, EVRMAvatarVersion Version)
	{
		if (Version == EVRMAvatarVersion::VRM0)
		{
			struct FAlias { const TCHAR* VRM0; EVRMExpressionPreset Preset; };
			static const FAlias Aliases[] = {
				{ TEXT("joy"), EVRMExpressionPreset::Happy }, { TEXT("sorrow"), EVRMExpressionPreset::Sad },
				{ TEXT("fun"), EVRMExpressionPreset::Relaxed }, { TEXT("a"), EVRMExpressionPreset::Aa },
				{ TEXT("i"), EVRMExpressionPreset::Ih }, { TEXT("u"), EVRMExpressionPreset::Ou },
				{ TEXT("e"), EVRMExpressionPreset::Ee }, { TEXT("o"), EVRMExpressionPreset::Oh },
				{ TEXT("blink_l"), EVRMExpressionPreset::BlinkLeft }, { TEXT("blink_r"), EVRMExpressionPreset::BlinkRight },
			};
			for (const FAlias& Alias : Aliases)
			{
				if (Name.Equals(Alias.VRM0, ESearchCase::IgnoreCase))
				{
					return Alias.Preset;
				}
			}
			// angry, blink, lookup, lookdown, lookleft, lookright and neutral have the same names
			// (0.x writes them lowercase); unknown is a custom expression.
		}
		for (int32 i = 0; i < UE_ARRAY_COUNT(PresetNames); ++i)
		{
			if (Name.Equals(PresetNames[i], ESearchCase::IgnoreCase))
			{
				return EVRMExpressionPreset(i + 1);
			}
		}
		return EVRMExpressionPreset::Custom;
	}

	FString ExpressionPresetName(EVRMExpressionPreset Preset)
	{
		const int32 i = int32(Preset) - 1;
		return i >= 0 && i < UE_ARRAY_COUNT(PresetNames) ? FString(PresetNames[i]) : FString();
	}

	bool BuildAvatarData(const FVRMDocument& Document, const FVRMParsedModel& Model, FVRMAvatarData& Out, TArray<FString>* OutWarnings)
	{
		Out = FVRMAvatarData();
		const FJsonObject& Root = *Document.GetJsonRoot();
		const FJsonObject* Extensions = JObject(&Root, TEXT("extensions"));
		FReader Reader{ Document, Model, Root, OutWarnings };

		// VRMC_vrm wins over a VRM 0.x block in the same file, as for the version (FVRMDocument).
		if (const FJsonObject* Vrm1 = JObject(Extensions, TEXT("VRMC_vrm")))
		{
			Out.Version = Reader.Version = EVRMAvatarVersion::VRM1;
			Reader.ReadVRM1(*Vrm1, Out);
			return true;
		}
		if (const FJsonObject* Vrm0 = JObject(Extensions, TEXT("VRM")))
		{
			Out.Version = Reader.Version = EVRMAvatarVersion::VRM0;
			Reader.ReadVRM0(*Vrm0, Out);
			return true;
		}
		return false;
	}

	FString DescribeLicense(const FVRMMeta& Meta)
	{
		TArray<FString> Parts;
		if (!Meta.LicenseName.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("Licence: %s"), *Meta.LicenseName));
		}
		else if (!Meta.LicenseUrl.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("Licence: %s"), *Meta.LicenseUrl));
		}
		if (!Meta.AvatarPermission.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("avatar use: %s"), *Meta.AvatarPermission));
		}
		if (!Meta.CommercialUsage.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("commercial use: %s"), *Meta.CommercialUsage));
		}
		if (!Meta.Modification.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("modification: %s"), *Meta.Modification));
		}
		if (!Meta.OtherLicenseUrl.IsEmpty())
		{
			Parts.Add(FString::Printf(TEXT("see %s"), *Meta.OtherLicenseUrl));
		}
		FString Summary = FString::Join(Parts, TEXT(", "));
		const FString By = FString::Join(Meta.Authors, TEXT(", "));
		if (!Meta.Name.IsEmpty() || !By.IsEmpty())
		{
			Summary = FString::Printf(TEXT("'%s'%s%s. %s"), *Meta.Name, By.IsEmpty() ? TEXT("") : TEXT(" by "), *By, *Summary);
		}
		return Summary;
	}
}
