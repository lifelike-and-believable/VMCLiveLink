// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "InterchangeVRMNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "VRMDocument.h"

namespace VRMNodeAttributes
{
	static const TCHAR* const Json = TEXT("VRM_DocumentJson");
	static const TCHAR* const SourceHash = TEXT("VRM_SourceHash");
	static const TCHAR* const Filename = TEXT("VRM_SourceFilename");
}

void UInterchangeVRMNode::SetFromDocument(const FVRMDocument& Document)
{
	AddStringAttribute(VRMNodeAttributes::Json, Document.GetJson());
	AddStringAttribute(VRMNodeAttributes::SourceHash, LexToString(Document.GetSourceHash()));
	AddStringAttribute(VRMNodeAttributes::Filename, Document.GetFilename());
}

bool UInterchangeVRMNode::GetDocumentJson(FString& OutJson) const
{
	return GetStringAttribute(VRMNodeAttributes::Json, OutJson);
}

bool UInterchangeVRMNode::GetSourceHash(FString& OutHash) const
{
	return GetStringAttribute(VRMNodeAttributes::SourceHash, OutHash);
}

TSharedPtr<const FVRMDocument> UInterchangeVRMNode::MakeDocument(FString& OutError) const
{
	FString Json;
	if (!GetDocumentJson(Json) || Json.IsEmpty())
	{
		OutError = TEXT("The VRM node holds no document.");
		return nullptr;
	}
	FString Filename;
	GetStringAttribute(VRMNodeAttributes::Filename, Filename);
	return FVRMDocument::LoadJson(Json, Filename, OutError);
}

const UInterchangeVRMNode* UInterchangeVRMNode::Find(const UInterchangeBaseNodeContainer& Container)
{
	const UInterchangeVRMNode* Found = nullptr;
	Container.IterateNodesOfType<UInterchangeVRMNode>([&Found](const FString& /*NodeUid*/, UInterchangeVRMNode* Node)
	{
		if (!Found)
		{
			Found = Node;
		}
	});
	return Found;
}
