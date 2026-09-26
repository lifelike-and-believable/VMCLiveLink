// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Tests for the VRM Expressions anim node (P4.2): expression curves in, morph target curves out.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_VRMExpressions.h"
#include "Animation/AnimCurveTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "VRMAvatarDescription.h"
#include "VRMAvatarParser.h"
#include "VRMDocument.h"
#include "VRMParsedModel.h"

namespace VRMExpressionsTests
{
	struct FNamedValue
	{
		const TCHAR* Name;
		float Value;
	};

	FVRMExpression MakeExpression(const TCHAR* Name, EVRMExpressionPreset Preset, std::initializer_list<FNamedValue> Binds)
	{
		FVRMExpression Expression;
		Expression.Name = FName(Name);
		Expression.Preset = Preset;
		for (const FNamedValue& Bind : Binds)
		{
			FVRMMorphBind& Morph = Expression.MorphBinds.AddDefaulted_GetRef();
			Morph.MorphTarget = FName(Bind.Name);
			Morph.Weight = Bind.Value;
		}
		return Expression;
	}

	FBlendedCurve MakeCurve(std::initializer_list<FNamedValue> Values)
	{
		FBlendedCurve Curve;
		for (const FNamedValue& Value : Values)
		{
			Curve.Set(FName(Value.Name), Value.Value);
		}
		return Curve;
	}

	TMap<FName, float> CurveValues(const FBlendedCurve& Curve)
	{
		TMap<FName, float> Out;
		Curve.ForEachElement([&Out](const auto& Element) { Out.Add(Element.Name, Element.Value); });
		return Out;
	}

	void TestCurve(FAutomationTestBase& Test, const TMap<FName, float>& Values, const TCHAR* Name, float Expected)
	{
		const float* Actual = Values.Find(FName(Name));
		if (Test.TestNotNull(FString::Printf(TEXT("curve %s is set"), Name), Actual))
		{
			Test.TestEqual(FString::Printf(TEXT("curve %s"), Name), *Actual, Expected, 1e-4f);
		}
	}

	UVRMAvatarDescription* LoadFixtureDescription(FAutomationTestBase& Test, const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VRMInterchange"));
		const FString Path = Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FString(Name) + TEXT(".vrm")) : FString();
		FString Error;
		const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Path, Error);
		FVRMParsedModel Model;
		UVRMAvatarDescription* Description = NewObject<UVRMAvatarDescription>(GetTransientPackage());
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads (%s)"), Name, *Error), Document.IsValid())
			|| !Test.TestTrue(FString::Printf(TEXT("%s: model"), Name), VRM::BuildParsedModel(*Document, Model))
			|| !Test.TestTrue(FString::Printf(TEXT("%s: avatar"), Name), VRM::BuildAvatarData(*Document, Model, Description->Avatar, nullptr)))
		{
			return nullptr;
		}
		return Description;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMExpressionsRulesTest, "VRM.Expressions.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMExpressionsRulesTest::RunTest(const FString& Parameters)
{
	using namespace VRMExpressionsTests;

	UVRMAvatarDescription* Description = NewObject<UVRMAvatarDescription>(GetTransientPackage());
	FVRMExpression Happy = MakeExpression(TEXT("happy"), EVRMExpressionPreset::Happy, { { TEXT("MorphJoy"), 1.f }, { TEXT("MorphBrow"), 0.5f } });
	Happy.OverrideBlink = EVRMExpressionOverride::Block;
	Happy.OverrideMouth = EVRMExpressionOverride::Blend;
	FVRMExpression Wink = MakeExpression(TEXT("wink"), EVRMExpressionPreset::Custom, { { TEXT("MorphWink"), 1.f } });
	Wink.bIsBinary = true;
	Description->Avatar.Expressions = {
		Happy,
		MakeExpression(TEXT("sad"), EVRMExpressionPreset::Sad, { { TEXT("MorphBrow"), 0.5f } }),
		MakeExpression(TEXT("blink"), EVRMExpressionPreset::Blink, { { TEXT("MorphBlink"), 1.f } }),
		MakeExpression(TEXT("aa"), EVRMExpressionPreset::Aa, { { TEXT("MorphA"), 1.f } }),
		Wink,
	};

	FAnimNode_VRMExpressions Node;
	Node.AvatarDescription = Description;

	// VRM 0.x names (Joy, sorrow, A, Blink) drive the VRM 1.0 presets, in any case.
	{
		FBlendedCurve Curve = MakeCurve({ { TEXT("Joy"), 0.4f }, { TEXT("SORROW"), 0.6f }, { TEXT("A"), 1.f }, { TEXT("Blink"), 1.f }, { TEXT("wink"), 0.4f }, { TEXT("Other"), 0.3f } });
		Node.ApplyExpressions(Curve);
		const TMap<FName, float> Values = CurveValues(Curve);
		TestEqual(TEXT("Five expressions have input"), Node.GetActiveExpressionCount(), 5);
		TestCurve(*this, Values, TEXT("MorphJoy"), 0.4f);
		TestCurve(*this, Values, TEXT("MorphBrow"), 0.4f * 0.5f + 0.6f * 0.5f); // two expressions bind it: the sum
		TestCurve(*this, Values, TEXT("MorphBlink"), 0.f);                      // happy blocks blink while active
		TestCurve(*this, Values, TEXT("MorphA"), 1.f - 0.4f);                   // happy blends the mouth by its weight
		TestCurve(*this, Values, TEXT("MorphWink"), 0.f);                       // binary: 0.4 rounds to 0
		TestCurve(*this, Values, TEXT("Other"), 0.3f);                          // other curves pass through
		TestCurve(*this, Values, TEXT("Joy"), 0.4f);                            // and so do the inputs
	}

	// Only morphs of expressions with input are written.
	{
		FBlendedCurve Curve = MakeCurve({ { TEXT("wink"), 0.6f } });
		Node.ApplyExpressions(Curve);
		const TMap<FName, float> Values = CurveValues(Curve);
		TestEqual(TEXT("One expression has input"), Node.GetActiveExpressionCount(), 1);
		TestCurve(*this, Values, TEXT("MorphWink"), 1.f); // binary: 0.6 rounds to 1
		TestFalse(TEXT("MorphJoy is not written without happy input"), Values.Contains(FName(TEXT("MorphJoy"))));
		TestFalse(TEXT("MorphBlink is not written without blink input"), Values.Contains(FName(TEXT("MorphBlink"))));
	}

	// Blink without happy is not blocked; Alpha scales it; blink_l is not blink.
	{
		Node.Alpha = 0.5f;
		FBlendedCurve Curve = MakeCurve({ { TEXT("blink"), 1.f }, { TEXT("blink_l"), 1.f } });
		Node.ApplyExpressions(Curve);
		TestCurve(*this, CurveValues(Curve), TEXT("MorphBlink"), 0.5f);
		TestEqual(TEXT("blink_l drives nothing on this avatar"), Node.GetActiveExpressionCount(), 1);
		Node.Alpha = 1.f;
	}

	// Another description rebuilds the lookups.
	{
		UVRMAvatarDescription* Other = NewObject<UVRMAvatarDescription>(GetTransientPackage());
		Other->Avatar.Expressions = { MakeExpression(TEXT("Blink"), EVRMExpressionPreset::Blink, { { TEXT("OtherBlink"), 1.f } }) };
		Node.AvatarDescription = Other;
		FBlendedCurve Curve = MakeCurve({ { TEXT("blink"), 1.f } });
		Node.ApplyExpressions(Curve);
		const TMap<FName, float> Values = CurveValues(Curve);
		TestCurve(*this, Values, TEXT("OtherBlink"), 1.f);
		TestFalse(TEXT("The old avatar's morph is gone"), Values.Contains(FName(TEXT("MorphBlink"))));
	}

	// No description: the curves are untouched.
	{
		Node.AvatarDescription = nullptr;
		FBlendedCurve Curve = MakeCurve({ { TEXT("blink"), 1.f } });
		Node.ApplyExpressions(Curve);
		TestEqual(TEXT("Nothing added without a description"), CurveValues(Curve).Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMExpressionsCrossVersionTest, "VRM.Expressions.CrossVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMExpressionsCrossVersionTest::RunTest(const FString& Parameters)
{
	using namespace VRMExpressionsTests;

	// A VRM 0.x sender (Joy) drives a VRM 1.0 avatar, and a VRM 1.0 sender (happy) a VRM 0.x one.
	// Both fixtures bind their happy expression to Fcl_ALL_Joy at full weight.
	struct FCase { const TCHAR* Fixture; const TCHAR* Input; };
	for (const FCase& Case : { FCase{ TEXT("vrm1_minimal"), TEXT("Joy") }, FCase{ TEXT("vrm0_minimal"), TEXT("happy") } })
	{
		UVRMAvatarDescription* Description = LoadFixtureDescription(*this, Case.Fixture);
		if (!Description)
		{
			continue;
		}
		FAnimNode_VRMExpressions Node;
		Node.AvatarDescription = Description;
		FBlendedCurve Curve = MakeCurve({ { Case.Input, 0.7f } });
		Node.ApplyExpressions(Curve);
		AddInfo(FString::Printf(TEXT("%s driven by %s"), Case.Fixture, Case.Input));
		TestCurve(*this, CurveValues(Curve), TEXT("Fcl_ALL_Joy"), 0.7f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
