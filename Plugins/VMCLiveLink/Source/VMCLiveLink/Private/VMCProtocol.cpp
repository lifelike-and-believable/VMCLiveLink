// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCProtocol.h"

#include "OSCMessage.h"
#include "OSCTypes.h"

namespace VMCProtocol
{
	namespace
	{
		/** Compares a TCHAR or ANSI address with an ASCII literal, character by character. */
		template <typename CharType>
		bool EqualsAscii(TStringView<CharType> A, const char* B, int32 BLen)
		{
			if (A.Len() != BLen)
			{
				return false;
			}
			for (int32 i = 0; i < BLen; ++i)
			{
				if (A[i] != CharType(B[i]))
				{
					return false;
				}
			}
			return true;
		}

		template <typename CharType>
		EAddress Classify(TStringView<CharType> Address)
		{
			static const char Prefix[] = "/VMC/Ext/";
			constexpr int32 PrefixLen = UE_ARRAY_COUNT(Prefix) - 1;
			if (Address.Len() < PrefixLen || !EqualsAscii(Address.Left(PrefixLen), Prefix, PrefixLen))
			{
				return EAddress::Other;
			}
			const TStringView<CharType> Rest = Address.RightChop(PrefixLen);
			auto Is = [&Rest](const char* Name) { return EqualsAscii(Rest, Name, int32(FCStringAnsi::Strlen(Name))); };

			// Most frequent first: a frame is ~55 Bone/Pos, a few dozen Blend/Val, one of the rest.
			if (Is("Bone/Pos"))    return EAddress::BonePos;
			if (Is("Blend/Val"))   return EAddress::BlendVal;
			if (Is("Blend/Apply")) return EAddress::BlendApply;
			if (Is("Root/Pos"))    return EAddress::RootPos;
			if (Is("T"))           return EAddress::Time;
			if (Is("OK"))          return EAddress::Available;
			for (const char* Device : { "Hmd/Pos", "Con/Pos", "Tra/Pos", "Hmd/Pos/Local", "Con/Pos/Local", "Tra/Pos/Local" })
			{
				if (Is(Device)) return EAddress::DevicePos;
			}
			return EAddress::Other;
		}
	}

	EAddress ClassifyAddress(FStringView Address)
	{
		return Classify(Address);
	}

	EAddress ClassifyAddress(FAnsiStringView Address)
	{
		return Classify(Address);
	}

	FName FArg::ToName() const
	{
		if (Utf8.IsEmpty())
		{
			return String.Len() < NAME_SIZE ? FName(*String) : NAME_None;
		}
		if (Utf8.Len() >= NAME_SIZE)
		{
			return NAME_None; // longer than any name can be; treated as malformed
		}
		// Pure ASCII (every bone name, most blend shapes): straight to FName.
		bool bAscii = true;
		for (UTF8CHAR C : Utf8)
		{
			if (uint8(C) >= 0x80) { bAscii = false; break; }
		}
		if (bAscii)
		{
			return FName(Utf8.Len(), reinterpret_cast<const ANSICHAR*>(Utf8.GetData()));
		}
		// Anything else (Japanese blend shape names, say) is UTF-8.
		const auto Converted = StringCast<TCHAR>(Utf8.GetData(), Utf8.Len());
		return FName(Converted.Length(), Converted.Get());
	}

	void ReadArgs(const FOSCMessage& Message, FArgs& OutArgs)
	{
		OutArgs.Reset();
		for (const UE::OSC::FOSCData& Data : Message.GetArgumentsChecked())
		{
			if (Data.IsFloat())
			{
				OutArgs.Add(FArg::MakeFloat(Data.GetFloat()));
			}
			else if (Data.IsInt32())
			{
				OutArgs.Add(FArg::MakeInt(Data.GetInt32()));
			}
			else if (Data.IsString())
			{
				OutArgs.Add(FArg::MakeString(Data.GetString()));
			}
			else
			{
				OutArgs.Add(FArg());
			}
		}
	}

	namespace
	{
		/** Reads Count numbers starting at Args[First]. Fails if any is not a number. */
		bool ReadNumbers(TConstArrayView<FArg> Args, int32 First, int32 Count, float* Out)
		{
			if (First + Count > Args.Num())
			{
				return false;
			}
			for (int32 i = 0; i < Count; ++i)
			{
				const FArg& Arg = Args[First + i];
				if (!Arg.IsNumber())
				{
					return false;
				}
				Out[i] = Arg.Number;
			}
			return true;
		}

		bool ReadPose7(TConstArrayView<FArg> Args, int32 First, FPose& Out)
		{
			float V[7];
			if (!ReadNumbers(Args, First, 7, V))
			{
				return false;
			}
			Out.Position = FVector3f(V[0], V[1], V[2]);
			Out.Rotation = FQuat4f(V[3], V[4], V[5], V[6]);
			return true;
		}
	}

	bool ParseBonePos(TConstArrayView<FArg> Args, FPose& Out)
	{
		if (Args.Num() != 8 || !Args[0].IsNonEmptyString())
		{
			return false;
		}
		Out = FPose();
		Out.Name = Args[0].ToName();
		return !Out.Name.IsNone() && ReadPose7(Args, 1, Out);
	}

	bool ParseRootPos(TConstArrayView<FArg> Args, FPose& Out, bool& bOutLegacyForm)
	{
		Out = FPose();
		bOutLegacyForm = false;

		if (Args.Num() == 7)
		{
			// Non-conformant: no name.
			bOutLegacyForm = true;
			return ReadPose7(Args, 0, Out);
		}

		if ((Args.Num() != 8 && Args.Num() != 14) || Args[0].Type != FArg::EType::String)
		{
			return false;
		}
		Out.Name = Args[0].ToName();
		if (!ReadPose7(Args, 1, Out))
		{
			return false;
		}
		if (Args.Num() == 14)
		{
			float V[6];
			if (!ReadNumbers(Args, 8, 6, V))
			{
				return false;
			}
			Out.bHasScaleAndOffset = true;
			Out.Scale = FVector3f(V[0], V[1], V[2]);
			Out.Offset = FVector3f(V[3], V[4], V[5]);
		}
		return true;
	}

	bool ParseBlendVal(TConstArrayView<FArg> Args, FName& OutName, float& OutValue)
	{
		if (Args.Num() != 2 || !Args[0].IsNonEmptyString() || !Args[1].IsNumber())
		{
			return false;
		}
		OutName = Args[0].ToName();
		OutValue = Args[1].Number;
		return !OutName.IsNone();
	}

	bool ParseTime(TConstArrayView<FArg> Args, float& OutSeconds)
	{
		if (Args.Num() != 1 || !Args[0].IsNumber())
		{
			return false;
		}
		OutSeconds = Args[0].Number;
		return true;
	}

	FVector ToUEPosition(const FVector3f& P, bool bUnityToUE, bool bMetersToCm)
	{
		// Both spaces are left-handed; this basis change is a proper rotation. It keeps a character
		// that faces Unity +Z facing UE +Y, matching the VRM importer and the UE mannequin.
		FVector Out = bUnityToUE ? FVector(-P.X, P.Z, P.Y) : FVector(P.X, P.Y, P.Z);
		if (bMetersToCm)
		{
			Out *= 100.0;
		}
		return Out;
	}

	FQuat ToUERotation(const FQuat4f& Q, bool bUnityToUE)
	{
		// The quaternion's vector part follows the same basis change as positions.
		FQuat Out = bUnityToUE ? FQuat(-Q.X, Q.Z, Q.Y, Q.W) : FQuat(Q.X, Q.Y, Q.Z, Q.W);
		Out.Normalize();
		return Out;
	}
}
