// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMDocument.h"
#include "VRMDocumentAccess.h"
#include "VRMCoreLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// The plugin's only cgltf implementation (P3.3). Other modules never include cgltf. Undefined
// again so that a unity build that puts another VRMCore file after this one doesn't compile it twice.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#undef CGLTF_IMPLEMENTATION

namespace
{
	uint32 ReadLE32(const uint8* P)
	{
		return uint32(P[0]) | (uint32(P[1]) << 8) | (uint32(P[2]) << 16) | (uint32(P[3]) << 24);
	}

	constexpr uint32 GlbMagic = 0x46546C67;     // "glTF"
	constexpr uint32 GlbJsonChunk = 0x4E4F534A; // "JSON"

	// The top-level JSON: the first chunk of a GLB, or the whole file for glTF JSON text.
	bool ExtractJson(const TArray64<uint8>& Bytes, FString& OutJson, FString& OutError)
	{
		OutJson.Reset();
		const uint8* Data = Bytes.GetData();
		int64 Length = Bytes.Num();
		if (Length >= 4 && ReadLE32(Data) == GlbMagic)
		{
			if (Length < 20 || ReadLE32(Data + 4) != 2 || ReadLE32(Data + 8) != uint64(Length))
			{
				OutError = TEXT("Not a valid GLB 2.0 file (bad header or length).");
				return false;
			}
			const uint32 ChunkLength = ReadLE32(Data + 12);
			if (ReadLE32(Data + 16) != GlbJsonChunk || Length < 20 + int64(ChunkLength))
			{
				OutError = TEXT("The GLB file's first chunk is not JSON.");
				return false;
			}
			Data += 20;
			Length = ChunkLength;
		}

		// Trailing padding (GLB pads the JSON chunk with spaces) and a byte order mark.
		while (Length > 0 && (Data[Length - 1] == 0 || Data[Length - 1] == ' ' || Data[Length - 1] == '\n' || Data[Length - 1] == '\r' || Data[Length - 1] == '\t'))
		{
			--Length;
		}
		if (Length >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
		{
			Data += 3;
			Length -= 3;
		}
		if (Length <= 0 || Length > MAX_int32)
		{
			OutError = TEXT("The file has no JSON.");
			return false;
		}

		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data), int32(Length));
		OutJson = FString(Converted.Length(), Converted.Get());
		return !OutJson.IsEmpty();
	}

	VRM::Coord::EVRMVersion ReadVersion(const FJsonObject& Root)
	{
		const TSharedPtr<FJsonObject>* Extensions = nullptr;
		if (!Root.TryGetObjectField(TEXT("extensions"), Extensions) || !Extensions || !Extensions->IsValid())
		{
			return VRM::Coord::EVRMVersion::Unknown;
		}
		if ((*Extensions)->HasField(TEXT("VRMC_vrm")))
		{
			return VRM::Coord::EVRMVersion::VRM1; // wins over a VRM 0.x block in the same file
		}
		return (*Extensions)->HasField(TEXT("VRM")) ? VRM::Coord::EVRMVersion::VRM0 : VRM::Coord::EVRMVersion::Unknown;
	}

	// Names and hierarchy from nodes[]. A node listed as the child of several nodes keeps its first
	// parent; self references and out-of-range indices are ignored.
	TArray<FVRMDocumentNode> ReadNodes(const FJsonObject& Root)
	{
		TArray<FVRMDocumentNode> Nodes;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Root.TryGetArrayField(TEXT("nodes"), Values) || !Values)
		{
			return Nodes;
		}
		const int32 Num = Values->Num();
		Nodes.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const TSharedPtr<FJsonObject>* Node = nullptr;
			if (!(*Values)[i].IsValid() || !(*Values)[i]->TryGetObject(Node) || !Node || !Node->IsValid())
			{
				continue;
			}
			(*Node)->TryGetStringField(TEXT("name"), Nodes[i].Name);
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if ((*Node)->TryGetArrayField(TEXT("children"), Children) && Children)
			{
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					double ChildIndex = -1.0;
					if (Child.IsValid() && Child->TryGetNumber(ChildIndex) && ChildIndex >= 0.0 && ChildIndex < Num)
					{
						const int32 C = int32(ChildIndex);
						if (C != i && Nodes[C].Parent == INDEX_NONE)
						{
							Nodes[C].Parent = i;
							Nodes[i].Children.Add(C);
						}
					}
				}
			}
		}
		return Nodes;
	}
}

TSharedPtr<const FVRMDocument> FVRMDocument::LoadFile(const FString& InFilename, FString& OutError)
{
	// Checked first: reading a missing file logs a warning of its own.
	TArray64<uint8> FileBytes;
	if (!FPaths::FileExists(InFilename) || !FFileHelper::LoadFileToArray(FileBytes, *InFilename))
	{
		OutError = FString::Printf(TEXT("Could not read '%s'."), *InFilename);
		return nullptr;
	}
	return LoadBytes(MoveTemp(FileBytes), InFilename, OutError);
}

TSharedPtr<const FVRMDocument> FVRMDocument::LoadBytes(TArray64<uint8>&& InBytes, const FString& InFilename, FString& OutError)
{
	OutError.Reset();
	FString JsonText;
	if (!ExtractJson(InBytes, JsonText, OutError))
	{
		OutError = FString::Printf(TEXT("%s ('%s')"), *OutError, *InFilename);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("The glTF JSON of '%s' could not be parsed."), *InFilename);
		return nullptr;
	}

	TSharedRef<FVRMDocument> Document = MakeShareable(new FVRMDocument(MoveTemp(InBytes), InFilename, MoveTemp(JsonText), Root.ToSharedRef()));
	Document->LoadGeometry();
	return Document;
}

TSharedPtr<const FVRMDocument> FVRMDocument::LoadJson(const FString& InJson, const FString& InFilename, FString& OutError)
{
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	if (InJson.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InJson), Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("The glTF JSON of '%s' could not be parsed."), *InFilename);
		return nullptr;
	}
	const FTCHARToUTF8 Utf8(*InJson);
	TArray64<uint8> JsonBytes;
	JsonBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TSharedRef<FVRMDocument> Document = MakeShareable(new FVRMDocument(MoveTemp(JsonBytes), InFilename, FString(InJson), Root.ToSharedRef()));
	Document->GeometryError = FString::Printf(TEXT("The document of '%s' was made from its JSON alone and has no geometry."), *InFilename);
	return Document;
}

FVRMDocument::FVRMDocument(TArray64<uint8>&& InBytes, const FString& InFilename, FString&& InJson, const TSharedRef<FJsonObject>& InRoot)
	: Bytes(MoveTemp(InBytes))
	, Filename(InFilename)
	, Json(MoveTemp(InJson))
	, JsonRoot(InRoot)
{
	FMD5 Md5;
	Md5.Update(Bytes.GetData(), uint64(Bytes.Num()));
	SourceHash.Set(Md5);
	Version = ReadVersion(*JsonRoot);
	Nodes = ReadNodes(*JsonRoot);
}

FVRMDocument::~FVRMDocument()
{
	if (Gltf)
	{
		cgltf_free(Gltf);
	}
}

FName FVRMDocument::GetNodeName(int32 NodeIndex) const
{
	return Nodes.IsValidIndex(NodeIndex) && !Nodes[NodeIndex].Name.IsEmpty() ? FName(*Nodes[NodeIndex].Name) : NAME_None;
}

void FVRMDocument::LoadGeometry()
{
	// cgltf keeps pointers into Bytes (the JSON and the GLB binary chunk), which live as long as
	// the document does.
	const FTCHARToUTF8 PathUtf8(*Filename);
	cgltf_options Options = {};
	cgltf_data* Data = nullptr;
	cgltf_result Result = cgltf_parse(&Options, Bytes.GetData(), cgltf_size(Bytes.Num()), &Data);
	if (Result != cgltf_result_success || !Data)
	{
		if (Data)
		{
			cgltf_free(Data); // cgltf only sets Data on success; free it if an error ever comes with one
		}
		GeometryError = FString::Printf(TEXT("cgltf_parse failed (cgltf error %d): %s"), int32(Result), *Filename);
		return;
	}

	// External buffers are found next to the file.
	Result = cgltf_load_buffers(&Options, Data, PathUtf8.Get());
	if (Result != cgltf_result_success)
	{
		GeometryError = FString::Printf(TEXT("cgltf_load_buffers failed: %s"), *Filename);
		cgltf_free(Data);
		return;
	}

	// Validation catches out-of-range accessors and buffer views that would otherwise be read out of bounds.
	Result = cgltf_validate(Data);
	if (Result != cgltf_result_success)
	{
		GeometryError = FString::Printf(TEXT("glTF validation failed (cgltf error %d): %s"), int32(Result), *Filename);
		cgltf_free(Data);
		return;
	}
	Gltf = Data;
}

const cgltf_data* FVRMDocumentAccess::Gltf(const FVRMDocument& Document)
{
	return Document.Gltf;
}
