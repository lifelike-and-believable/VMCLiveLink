// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "InterchangeVRMNode.generated.h"

class UInterchangeBaseNodeContainer;
class FVRMDocument;

/**
 * What UVRMTranslator read from a VRM file that the import pipelines need (P3.3): the top-level
 * JSON and the file's hash. The pipelines take these from here instead of opening the file again,
 * so an import reads the file once. Stored as node attributes, so they survive the node container
 * being copied between translation and the pipelines.
 */
UCLASS(BlueprintType)
class VRMINTERCHANGE_API UInterchangeVRMNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	virtual FString GetTypeName() const override { return TEXT("VRMDocumentNode"); }

	/** Records the document in this node. */
	void SetFromDocument(const FVRMDocument& Document);

	/** The top-level JSON, as read from the file. */
	bool GetDocumentJson(FString& OutJson) const;

	/** MD5 of the file's bytes, as text (LexToString of FMD5Hash). */
	bool GetSourceHash(FString& OutHash) const;

	/**
	 * A document made from the stored JSON, without reading the file. It has the JSON, node
	 * table and version; it has no geometry (the pipelines don't need it). Null if the node holds
	 * no JSON.
	 */
	TSharedPtr<const FVRMDocument> MakeDocument(FString& OutError) const;

	/** The container's VRM node, if the VRM translator made the container. */
	static const UInterchangeVRMNode* Find(const UInterchangeBaseNodeContainer& Container);
};
