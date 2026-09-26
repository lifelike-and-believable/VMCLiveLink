// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/SecureHash.h"
#include "VRMCoordinateConversion.h"

struct cgltf_data;

/** One glTF node, as the file lists it. */
struct FVRMDocumentNode
{
	/** The node's name; empty if it has none. */
	FString Name;
	/** Index of the node that lists this one as a child, or INDEX_NONE. */
	int32 Parent = INDEX_NONE;
	/** The node's children, in file order (only valid indices). */
	TArray<int32> Children;
};

/**
 * A .vrm, .glb or .gltf file read once (P3.3): its bytes, the top-level JSON parsed once, the node
 * table, the VRM version and, when the file's binary data loads and validates, the glTF geometry
 * that VRM::BuildParsedModel reads. Everything that needs the file's contents during an import
 * reads it from here instead of opening the file again.
 *
 * Immutable once loaded, so it can be shared (TSharedRef<const FVRMDocument>) across threads.
 */
class VRMCORE_API FVRMDocument
{
public:
	/**
	 * Reads and parses a file. Returns null, with OutError set, if the file can't be read or its
	 * JSON can't be parsed. A file whose JSON parses but whose geometry doesn't load still gives a
	 * document; HasGeometry() is false and GetGeometryError() says why.
	 */
	static TSharedPtr<const FVRMDocument> LoadFile(const FString& InFilename, FString& OutError);

	/**
	 * The same from bytes already in memory (GLB or glTF JSON text). Filename is only used to find
	 * external buffers and images next to it, and in messages (InFilename).
	 */
	static TSharedPtr<const FVRMDocument> LoadBytes(TArray64<uint8>&& InBytes, const FString& InFilename, FString& OutError);

	/**
	 * A document from top-level JSON text alone, e.g. what the translator stored for the pipelines
	 * (UInterchangeVRMNode). It has no geometry, and its hash is the JSON's, not the file's.
	 */
	static TSharedPtr<const FVRMDocument> LoadJson(const FString& InJson, const FString& InFilename, FString& OutError);

	~FVRMDocument();
	FVRMDocument(const FVRMDocument&) = delete;
	FVRMDocument& operator=(const FVRMDocument&) = delete;

	const FString& GetFilename() const { return Filename; }

	/** MD5 of the file's bytes. */
	const FMD5Hash& GetSourceHash() const { return SourceHash; }

	/** The top-level JSON text (the GLB JSON chunk, or the whole .gltf file). */
	const FString& GetJson() const { return Json; }

	/** The top-level JSON, parsed. Read-only: the document is shared. */
	const TSharedRef<FJsonObject>& GetJsonRoot() const { return JsonRoot; }

	/** From the top-level extensions: VRMC_vrm is VRM 1.0, VRM is VRM 0.x, neither is Unknown. */
	VRM::Coord::EVRMVersion GetVersion() const { return Version; }

	/** Every node in the file, by glTF node index. */
	const TArray<FVRMDocumentNode>& GetNodes() const { return Nodes; }

	/** A node's name, or NAME_None if the index is out of range or the node has no name. */
	FName GetNodeName(int32 NodeIndex) const;

	/** True if the binary data loaded and the glTF validated, so the geometry can be read. */
	bool HasGeometry() const { return Gltf != nullptr; }
	const FString& GetGeometryError() const { return GeometryError; }

private:
	FVRMDocument(TArray64<uint8>&& InBytes, const FString& InFilename, FString&& InJson, const TSharedRef<FJsonObject>& InRoot);

	// Parses the glTF with cgltf, loads its buffers and validates it; on failure sets GeometryError.
	void LoadGeometry();

	friend struct FVRMDocumentAccess; // VRMCore's own code reads the cgltf data

	TArray64<uint8> Bytes;
	FString Filename;
	FMD5Hash SourceHash;
	FString Json;
	TSharedRef<FJsonObject> JsonRoot;
	VRM::Coord::EVRMVersion Version = VRM::Coord::EVRMVersion::Unknown;
	TArray<FVRMDocumentNode> Nodes;

	// cgltf's parse of Bytes (which it points into); null if the geometry didn't load.
	cgltf_data* Gltf = nullptr;
	FString GeometryError;
};
