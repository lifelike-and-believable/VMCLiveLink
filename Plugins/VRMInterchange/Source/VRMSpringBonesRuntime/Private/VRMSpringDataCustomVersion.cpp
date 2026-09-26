// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMSpringDataCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FVRMSpringDataCustomVersion::GUID(0x3CE737D5, 0xDF6F4DAE, 0x88B0CA49, 0xAA1D68D3);

static FCustomVersionRegistration GRegisterVRMSpringDataCustomVersion(
	FVRMSpringDataCustomVersion::GUID,
	FVRMSpringDataCustomVersion::LatestVersion,
	TEXT("VRMSpringDataVer"));
