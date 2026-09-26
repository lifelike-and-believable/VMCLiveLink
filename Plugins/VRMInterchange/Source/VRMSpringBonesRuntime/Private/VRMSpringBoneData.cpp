// Copyright (c) 2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMSpringBoneData.h"
#include "Serialization/Archive.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogVRMSpringData, Log, All);

void UVRMSpringBoneData::Serialize(FArchive& Ar)
{
    Ar.UsingCustomVersion(FVRMSpringDataCustomVersion::GUID);
    Super::Serialize(Ar);

    // Only a real load says which version the data was saved with. Undo/redo and duplication
    // also load, but from data this plugin version wrote.
    if (Ar.IsLoading() && Ar.IsPersistent() && !Ar.IsTransacting())
    {
        // CustomVer is -1 for packages saved before the version was registered.
        LoadedDataVersion = FMath::Max(Ar.CustomVer(FVRMSpringDataCustomVersion::GUID),
            static_cast<int32>(FVRMSpringDataCustomVersion::BeforeCustomVersionWasAdded));
    }
}

bool UVRMSpringBoneData::RequiresReimport(int32 DataVersion, const FVRMSpringConfig& Config)
{
    // Empty data has nothing in the wrong axes.
    const bool bHasGeometry = Config.Springs.Num() > 0 || Config.Colliders.Num() > 0;
    if (bHasGeometry && DataVersion < FVRMSpringDataCustomVersion::ConvertedColliderAxes)
    {
        return true;
    }
    // VRM 0.x chains used to hold only the listed roots; their descendants need the source file.
    return Config.Spec == EVRMSpringSpec::VRM0 && Config.Springs.Num() > 0
        && DataVersion < FVRMSpringDataCustomVersion::ExpandedVRM0Chains;
}

void UVRMSpringBoneData::CopySpringParametersToJoints(FVRMSpringConfig& Config)
{
    for (const FVRMSpring& Spring : Config.Springs)
    {
        for (const int32 JointIndex : Spring.JointIndices)
        {
            if (Config.Joints.IsValidIndex(JointIndex))
            {
                FVRMSpringJoint& Joint = Config.Joints[JointIndex];
                Joint.Stiffness = Spring.Stiffness;
                Joint.Drag = Spring.Drag;
                Joint.GravityDir = Spring.GravityDir;
                Joint.GravityPower = Spring.GravityPower;
                Joint.HitRadius = Spring.HitRadius;
            }
        }
    }
}

void UVRMSpringBoneData::CopySpringParametersToJoints(FVRMSpringConfig& Config)
{
    for (const FVRMSpring& Spring : Config.Springs)
    {
        for (const int32 JointIndex : Spring.JointIndices)
        {
            if (Config.Joints.IsValidIndex(JointIndex))
            {
                FVRMSpringJoint& Joint = Config.Joints[JointIndex];
                Joint.Stiffness = Spring.Stiffness;
                Joint.Drag = Spring.Drag;
                Joint.GravityDir = Spring.GravityDir;
                Joint.GravityPower = Spring.GravityPower;
                Joint.HitRadius = Spring.HitRadius;
            }
        }
    }
}

void UVRMSpringBoneData::PostLoad()
{
    Super::PostLoad();

    // Before PerJointParameters the solver read the spring's parameters, so giving every joint its
    // spring's values keeps the asset behaving as it did.
    if (LoadedDataVersion < FVRMSpringDataCustomVersion::PerJointParameters)
    {
        CopySpringParametersToJoints(SpringConfig);
    }

    // Once set, the flag stays until a reimport replaces the asset: saving an old asset stamps
    // the latest version on it without changing its data.
    bNeedsReimport = bNeedsReimport || RequiresReimport(LoadedDataVersion, SpringConfig);
    if (bNeedsReimport)
    {
        UE_LOG(LogVRMSpringData, Warning,
            TEXT("%s holds spring data from an older VRMInterchange version that a fresh import would produce differently (collider axes, or VRM 0.x chains without their descendant bones). Reimport '%s' to update it."),
            *GetPathName(), SourceFilename.IsEmpty() ? TEXT("the source VRM file") : *SourceFilename);
    }
}

#if WITH_EDITOR
void UVRMSpringBoneData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (!PropertyChangedEvent.Property && !PropertyChangedEvent.MemberProperty) { return; }

    const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
    const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    static const TSet<FName> TunableNames = { TEXT("Stiffness"), TEXT("Drag"), TEXT("GravityDir"), TEXT("GravityPower"), TEXT("HitRadius") };
    // Collider-related fields we care about for bumping/sanitizing
    static const TSet<FName> ColliderNames = {
        TEXT("Colliders"), TEXT("ColliderGroups"),
        TEXT("Spheres"), TEXT("Capsules"), TEXT("Planes"),
        TEXT("Offset"), TEXT("TailOffset"), TEXT("Normal"),
        TEXT("Radius"), TEXT("bInside"),
        TEXT("NodeIndex"), TEXT("BoneName")
    };

    bool bBump = false;

    if (TunableNames.Contains(PropName) || TunableNames.Contains(MemberName))
    {
        bBump = true;
    }
    // Editing any element of Springs array (even struct swap) should bump
    if (MemberName == TEXT("Springs") || PropName == TEXT("Springs"))
    {
        bBump = true; 
    }
    // Any collider or collider-group edit should bump as well
    if (ColliderNames.Contains(PropName) || ColliderNames.Contains(MemberName))
    {
        bBump = true;
    }
    if (MemberName == TEXT("Colliders") || PropName == TEXT("Colliders") ||
        MemberName == TEXT("ColliderGroups") || PropName == TEXT("ColliderGroups"))
    {
        bBump = true;
    }

    if (bBump)
    {
        ++EditRevision;

        // Clamp Spring tunables
        for (FVRMSpring& Spring : SpringConfig.Springs)
        {
            Spring.Stiffness = FMath::Clamp(Spring.Stiffness, 0.f, 1.f);
            Spring.Drag = FMath::Clamp(Spring.Drag, 0.f, 1.f);
            if (!Spring.GravityDir.IsNearlyZero())
            {
                Spring.GravityDir = Spring.GravityDir.GetSafeNormal();
            }
            Spring.GravityPower = FMath::Max(0.f, Spring.GravityPower);
            Spring.HitRadius = FMath::Max(0.f, Spring.HitRadius);
        }

        for (FVRMSpringJoint& Joint : SpringConfig.Joints)
        {
            Joint.Stiffness = FMath::Clamp(Joint.Stiffness, 0.f, 1.f);
            Joint.Drag = FMath::Clamp(Joint.Drag, 0.f, 1.f);
            if (!Joint.GravityDir.IsNearlyZero())
            {
                Joint.GravityDir = Joint.GravityDir.GetSafeNormal();
            }
            Joint.GravityPower = FMath::Max(0.f, Joint.GravityPower);
            Joint.HitRadius = FMath::Max(0.f, Joint.HitRadius);
        }

        // Sanitize Colliders
        for (FVRMSpringCollider& Col : SpringConfig.Colliders)
        {
            for (FVRMSpringColliderSphere& S : Col.Spheres)
            {
                S.Radius = FMath::Max(0.f, S.Radius);
            }
            for (FVRMSpringColliderCapsule& C : Col.Capsules)
            {
                C.Radius = FMath::Max(0.f, C.Radius);
            }
            for (FVRMSpringColliderPlane& P : Col.Planes)
            {
                if (P.Normal.IsNearlyZero())
                {
                    P.Normal = FVector(0,0,1);
                }
                else
                {
                    P.Normal = P.Normal.GetSafeNormal();
                }
            }
        }
    }
}

void UVRMSpringBoneData::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
    Super::PostEditChangeChainProperty(PropertyChangedEvent); // also routes to PostEditChangeProperty (clamping)

    // A spring's Stiffness/Drag/GravityDir/GravityPower/HitRadius are "apply to all joints" helpers:
    // the solver reads each joint's own values. Find which spring field the edit was in, if any.
    static const TSet<FName> SpringFields = { TEXT("Stiffness"), TEXT("Drag"), TEXT("GravityDir"), TEXT("GravityPower"), TEXT("HitRadius") };
    FName SpringField = NAME_None;
    for (FEditPropertyChain::TDoubleLinkedListNode* Node = PropertyChangedEvent.PropertyChain.GetHead(); Node; Node = Node->GetNextNode())
    {
        const FProperty* Prop = Node->GetValue();
        if (Prop && Prop->GetOwnerStruct() == FVRMSpring::StaticStruct() && SpringFields.Contains(Prop->GetFName()))
        {
            SpringField = Prop->GetFName();
            break;
        }
    }
    if (SpringField.IsNone())
    {
        return;
    }

    const int32 EditedSpring = PropertyChangedEvent.GetArrayIndex(TEXT("Springs"));
    for (int32 SpringIndex = 0; SpringIndex < SpringConfig.Springs.Num(); ++SpringIndex)
    {
        if (EditedSpring != INDEX_NONE && SpringIndex != EditedSpring)
        {
            continue;
        }
        const FVRMSpring& Spring = SpringConfig.Springs[SpringIndex];
        for (const int32 JointIndex : Spring.JointIndices)
        {
            if (!SpringConfig.Joints.IsValidIndex(JointIndex)) continue;
            FVRMSpringJoint& Joint = SpringConfig.Joints[JointIndex];
            if (SpringField == TEXT("Stiffness"))         { Joint.Stiffness = Spring.Stiffness; }
            else if (SpringField == TEXT("Drag"))         { Joint.Drag = Spring.Drag; }
            else if (SpringField == TEXT("GravityDir"))   { Joint.GravityDir = Spring.GravityDir; }
            else if (SpringField == TEXT("GravityPower")) { Joint.GravityPower = Spring.GravityPower; }
            else if (SpringField == TEXT("HitRadius"))    { Joint.HitRadius = Spring.HitRadius; }
        }
    }
    ++EditRevision;
}

// Called once after ParseSpringBonesFromJson(... OutNodeParent, OutNodeChildren ...)
void UVRMSpringBoneData::BuildResolvedChildren()
{
    const FVRMSpringConfig& Cfg = SpringConfig;
    ResolvedChildNodeIndexPerJoint.SetNum(Cfg.Joints.Num());
    TSet<int32> SpringJointNodes; SpringJointNodes.Reserve(Cfg.Joints.Num());
    for (const auto& J : Cfg.Joints) { SpringJointNodes.Add(J.NodeIndex); }

    for (const FVRMSpring& S : Cfg.Springs)
    {
        // walk chain in order
        for (int32 i = 0; i < S.JointIndices.Num(); ++i)
        {
            const int32 JointIdx = S.JointIndices[i];
            const int32 ThisNode = Cfg.Joints[JointIdx].NodeIndex;

            int32 ChosenChild = INDEX_NONE;

            // Preferred: the next joint in the chain, if it is an actual child
            if (i + 1 < S.JointIndices.Num())
            {
                const int32 NextJointIdx = S.JointIndices[i + 1];
                const int32 NextNode = Cfg.Joints[NextJointIdx].NodeIndex;

                if (const FVRMNodeChildren* Kids = NodeChildren.Find(ThisNode))
                {
                    if (Kids->Children.Contains(NextNode))
                    {
                        ChosenChild = NextNode;
                    }
                }
            }

            // Fallback: pick the first child that is *also* in this spring (handles forks)
            if (ChosenChild == INDEX_NONE)
            {
                if (const FVRMNodeChildren* Kids = NodeChildren.Find(ThisNode))
                {
                    for (int32 K : Kids->Children)
                    {
                        if (SpringJointNodes.Contains(K)) { ChosenChild = K; break; }
                    }
                }
            }

            ResolvedChildNodeIndexPerJoint[JointIdx] = ChosenChild; // can be INDEX_NONE (=> 7cm pseudo tail)
        }
    }
}

#endif
