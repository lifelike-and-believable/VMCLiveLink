// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCConnectionSettings.h"
#include "Interfaces/IPv4/IPv4Address.h"

namespace
{
	bool ParseBool(const FString& Value, bool& Out)
	{
		const FString V = Value.TrimStartAndEnd();
		if (V == TEXT("1") || V.Equals(TEXT("true"), ESearchCase::IgnoreCase) || V.Equals(TEXT("yes"), ESearchCase::IgnoreCase) || V.Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			Out = true;
			return true;
		}
		if (V == TEXT("0") || V.Equals(TEXT("false"), ESearchCase::IgnoreCase) || V.Equals(TEXT("no"), ESearchCase::IgnoreCase) || V.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Out = false;
			return true;
		}
		return false;
	}

	bool IsValidPort(int32 Port)
	{
		return Port >= 1 && Port <= 65535;
	}

	bool IsValidBindAddress(const FString& Address)
	{
		FIPv4Address Parsed;
		return FIPv4Address::Parse(Address, Parsed);
	}

	bool IsValidSubject(FName Subject)
	{
		// ';' would end the value in the connection string.
		const FString Name = Subject.ToString();
		return !Subject.IsNone() && !Name.TrimStartAndEnd().IsEmpty() && !Name.Contains(TEXT(";"));
	}
}

bool FVMCConnectionSettings::FromString(const FString& ConnectionString, FVMCConnectionSettings& Out, TArray<FString>* OutErrors)
{
	Out = FVMCConnectionSettings();
	bool bOk = true;
	auto Error = [&](const FString& Message)
	{
		bOk = false;
		if (OutErrors)
		{
			OutErrors->Add(Message);
		}
	};

	TArray<FString> Parts;
	ConnectionString.ParseIntoArray(Parts, TEXT(";"), /*bCullEmpty*/ true);
	for (const FString& Part : Parts)
	{
		FString Key, Value;
		if (!Part.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		Key.TrimStartAndEndInline();
		const FString Trimmed = Value.TrimStartAndEnd();

		if (Key.Equals(TEXT("port"), ESearchCase::IgnoreCase))
		{
			const int32 Port = Trimmed.IsNumeric() ? FCString::Atoi(*Trimmed) : 0;
			if (IsValidPort(Port) && !Trimmed.Contains(TEXT(".")))
			{
				Out.Port = Port;
			}
			else
			{
				Error(FString::Printf(TEXT("port '%s' is not a number from 1 to 65535"), *Trimmed));
			}
		}
		else if (Key.Equals(TEXT("bind"), ESearchCase::IgnoreCase))
		{
			if (IsValidBindAddress(Trimmed))
			{
				Out.BindAddress = Trimmed;
			}
			else
			{
				Error(FString::Printf(TEXT("bind address '%s' is not an IPv4 address"), *Trimmed));
			}
		}
		else if (Key.Equals(TEXT("subject"), ESearchCase::IgnoreCase))
		{
			// Not trimmed on read beyond the check: a subject name is whatever the user typed.
			if (IsValidSubject(FName(*Value)))
			{
				Out.SubjectName = FName(*Value);
			}
			else
			{
				Error(TEXT("subject name is empty or contains ';'"));
			}
		}
		else if (Key.Equals(TEXT("yaw"), ESearchCase::IgnoreCase))
		{
			if (Trimmed.IsNumeric())
			{
				Out.YawOffsetDeg = FCString::Atof(*Trimmed);
			}
			else
			{
				Error(FString::Printf(TEXT("yaw '%s' is not a number"), *Trimmed));
			}
		}
		else
		{
			bool* Flag = nullptr;
			if (Key.Equals(TEXT("unity2ue"), ESearchCase::IgnoreCase))         Flag = &Out.bUnityToUE;
			else if (Key.Equals(TEXT("meters2cm"), ESearchCase::IgnoreCase))   Flag = &Out.bMetersToCm;
			else if (Key.Equals(TEXT("zeromissing"), ESearchCase::IgnoreCase)) Flag = &Out.bZeroMissingCurves;
			else if (Key.Equals(TEXT("incomingtranslations"), ESearchCase::IgnoreCase)) Flag = &Out.bPreferIncomingTranslations;
			else if (Key.Equals(TEXT("thread"), ESearchCase::IgnoreCase))      Flag = &Out.bReceiveThread;
			if (Flag && !ParseBool(Trimmed, *Flag))
			{
				Error(FString::Printf(TEXT("%s '%s' is not 1 or 0"), *Key, *Trimmed));
			}
		}
	}
	return bOk;
}

FString FVMCConnectionSettings::ToString() const
{
	return FString::Printf(TEXT("port=%d;bind=%s;unity2ue=%d;meters2cm=%d;yaw=%s;zeromissing=%d;incomingtranslations=%d;thread=%d;subject=%s"),
		Port, *BindAddress, bUnityToUE ? 1 : 0, bMetersToCm ? 1 : 0, *FString::SanitizeFloat(YawOffsetDeg),
		bZeroMissingCurves ? 1 : 0, bPreferIncomingTranslations ? 1 : 0,
		bReceiveThread ? 1 : 0, *SubjectName.ToString());
}

bool FVMCConnectionSettings::Validate(TArray<FString>* OutErrors) const
{
	bool bOk = true;
	auto Error = [&](const FString& Message)
	{
		bOk = false;
		if (OutErrors) OutErrors->Add(Message);
	};
	if (!IsValidPort(Port)) Error(FString::Printf(TEXT("port %d is not from 1 to 65535"), Port));
	if (!IsValidBindAddress(BindAddress)) Error(FString::Printf(TEXT("bind address '%s' is not an IPv4 address"), *BindAddress));
	if (!IsValidSubject(SubjectName)) Error(TEXT("subject name is empty or contains ';'"));
	return bOk;
}

bool FVMCConnectionSettings::operator==(const FVMCConnectionSettings& Other) const
{
	return Port == Other.Port && BindAddress == Other.BindAddress && bUnityToUE == Other.bUnityToUE
		&& bMetersToCm == Other.bMetersToCm && YawOffsetDeg == Other.YawOffsetDeg && SubjectName == Other.SubjectName
		&& bZeroMissingCurves == Other.bZeroMissingCurves && bPreferIncomingTranslations == Other.bPreferIncomingTranslations
		&& bReceiveThread == Other.bReceiveThread;
}
