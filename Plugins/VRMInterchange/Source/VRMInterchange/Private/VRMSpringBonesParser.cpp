// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"
#include "VRMCoordinateConversion.h"
#include "VRMDocument.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // Reads a vector in either form VRM uses: an array [x, y, z] (VRM 1.0) or an object
    // {"x": .., "y": .., "z": ..} (VRM 0.x secondaryAnimation). Missing object members read as 0.
    static FVector ReadVec3(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FVector& Default = FVector::ZeroVector)
    {
        if (!Obj.IsValid()) return Default;
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (Obj->TryGetArrayField(Field, Arr) && Arr && Arr->Num() >= 3)
        {
            double XYZ[3] = { 0.0, 0.0, 0.0 };
            for (int32 k = 0; k < 3; ++k)
            {
                if ((*Arr)[k].IsValid()) { (*Arr)[k]->TryGetNumber(XYZ[k]); }
            }
            return FVector(XYZ[0], XYZ[1], XYZ[2]);
        }
        const TSharedPtr<FJsonObject>* VecObj = nullptr;
        if (Obj->TryGetObjectField(Field, VecObj) && VecObj && VecObj->IsValid())
        {
            double X = 0.0, Y = 0.0, Z = 0.0;
            (*VecObj)->TryGetNumberField(TEXT("x"), X);
            (*VecObj)->TryGetNumberField(TEXT("y"), Y);
            (*VecObj)->TryGetNumberField(TEXT("z"), Z);
            return FVector(X, Y, Z);
        }
        return Default;
    }

    // Non-spec VRMC_springBone layouts (SP-05, decision D-6): accepted for one release, with a log
    // naming the layout each time one is used, so files that depend on them can be found and fixed.
    static TAutoConsoleVariable<bool> CVarLenientSpringSchema(
        TEXT("vrm.SpringBones.LenientSchema"),
        true,
        TEXT("Accept non-spec VRMC_springBone layouts (collider 'shapes' arrays, wrapped or typed shapes, VRMC_node_collider, spring-level parameters, 'drag', center objects). Each one used is logged."),
        ECVF_Default);

    static bool AcceptNonSpec(const TCHAR* Layout)
    {
        if (CVarLenientSpringSchema.GetValueOnAnyThread())
        {
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] VRM1: accepted non-spec layout: %s (vrm.SpringBones.LenientSchema)"), Layout);
            return true;
        }
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring Parser] VRM1: ignored non-spec layout: %s (set vrm.SpringBones.LenientSchema 1 to accept it)"), Layout);
        return false;
    }

    // Reads the shapes in one shape object, the spec form: { "sphere": {..} }, { "capsule": {..} } or,
    // in VRMC_springBone_extended_collider, { "plane": {..} }.
    static void ParseShapeContainer(const TSharedPtr<FJsonObject>& Container,
                                    TArray<FVRMSpringColliderSphere>& OutSpheres,
                                    TArray<FVRMSpringColliderCapsule>& OutCapsules,
                                    TArray<FVRMSpringColliderPlane>& OutPlanes)
    {
        if (!Container.IsValid()) return;
        const TSharedPtr<FJsonObject>* Sphere = nullptr;
        if (Container->TryGetObjectField(TEXT("sphere"), Sphere) && Sphere && Sphere->IsValid())
        {
            FVRMSpringColliderSphere S;
            S.Offset = ReadVec3(*Sphere, TEXT("offset"));
            (*Sphere)->TryGetNumberField(TEXT("radius"), S.Radius);
            if ((*Sphere)->HasTypedField<EJson::Boolean>(TEXT("inside"))) { S.bInside = (*Sphere)->GetBoolField(TEXT("inside")); }
            OutSpheres.Add(S);
        }
        const TSharedPtr<FJsonObject>* Capsule = nullptr;
        if (Container->TryGetObjectField(TEXT("capsule"), Capsule) && Capsule && Capsule->IsValid())
        {
            FVRMSpringColliderCapsule C;
            C.Offset = ReadVec3(*Capsule, TEXT("offset"));
            C.TailOffset = ReadVec3(*Capsule, TEXT("tail"));
            (*Capsule)->TryGetNumberField(TEXT("radius"), C.Radius);
            if ((*Capsule)->HasTypedField<EJson::Boolean>(TEXT("inside"))) { C.bInside = (*Capsule)->GetBoolField(TEXT("inside")); }
            OutCapsules.Add(C);
        }
        const TSharedPtr<FJsonObject>* Plane = nullptr;
        if (Container->TryGetObjectField(TEXT("plane"), Plane) && Plane && Plane->IsValid())
        {
            FVRMSpringColliderPlane P;
            P.Offset = ReadVec3(*Plane, TEXT("offset"));
            P.Normal = ReadVec3(*Plane, TEXT("normal"), FVector(0,0,1));
            if (!P.Normal.IsNearlyZero()) P.Normal = P.Normal.GetSafeNormal();
            OutPlanes.Add(P);
        }
    }

    // Non-spec shape entries: the spec form, a nested { "shape": {..} }, or a typed
    // { "type": "sphere", "offset": .., "radius": .. }. Only used with the lenient schema.
    static void ParseOneShapeObject(const TSharedPtr<FJsonObject>& ShapeEntry,
                                    TArray<FVRMSpringColliderSphere>& OutSpheres,
                                    TArray<FVRMSpringColliderCapsule>& OutCapsules,
                                    TArray<FVRMSpringColliderPlane>& OutPlanes)
    {
        if (!ShapeEntry.IsValid()) return;

        ParseShapeContainer(ShapeEntry, OutSpheres, OutCapsules, OutPlanes);

        const TSharedPtr<FJsonObject>* Wrapped = nullptr;
        if (ShapeEntry->TryGetObjectField(TEXT("shape"), Wrapped) && Wrapped && Wrapped->IsValid())
        {
            ParseShapeContainer(*Wrapped, OutSpheres, OutCapsules, OutPlanes);
        }

        FString Type;
        if (ShapeEntry->TryGetStringField(TEXT("type"), Type))
        {
            Type.TrimStartAndEndInline(); Type.ToLowerInline();
            if (Type == TEXT("sphere"))
            {
                FVRMSpringColliderSphere SphereTemp;
                SphereTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset"));
                ShapeEntry->TryGetNumberField(TEXT("radius"), SphereTemp.Radius);
                if (ShapeEntry->HasTypedField<EJson::Boolean>(TEXT("inside"))) { SphereTemp.bInside = ShapeEntry->GetBoolField(TEXT("inside")); }
                OutSpheres.Add(SphereTemp);
            }
            else if (Type == TEXT("capsule"))
            {
                FVRMSpringColliderCapsule CapsuleTemp;
                CapsuleTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset"));
                CapsuleTemp.TailOffset = ReadVec3(ShapeEntry, TEXT("tail"));
                ShapeEntry->TryGetNumberField(TEXT("radius"), CapsuleTemp.Radius);
                if (ShapeEntry->HasTypedField<EJson::Boolean>(TEXT("inside"))) { CapsuleTemp.bInside = ShapeEntry->GetBoolField(TEXT("inside")); }
                OutCapsules.Add(CapsuleTemp);
            }
            else if (Type == TEXT("plane"))
            {
                FVRMSpringColliderPlane PlaneTemp;
                PlaneTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset"));
                PlaneTemp.Normal = ReadVec3(ShapeEntry, TEXT("normal"), FVector(0,0,1));
                if (!PlaneTemp.Normal.IsNearlyZero()) PlaneTemp.Normal = PlaneTemp.Normal.GetSafeNormal();
                OutPlanes.Add(PlaneTemp);
            }
        }
    }

    // Helper: field can be either a number node index or an object { "node": <index> }
    static bool TryGetNodeIndexFlexible(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32& OutIndex)
    {
        OutIndex = INDEX_NONE;
        if (!Obj.IsValid()) return false;
        if (Obj->TryGetNumberField(Field, OutIndex))
        {
            return true;
        }
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (Obj->TryGetObjectField(Field, Inner) && Inner && Inner->IsValid())
        {
            return (*Inner)->TryGetNumberField(TEXT("node"), OutIndex);
        }
        return false;
    }

    // Build a map from glTF node index -> collider shapes defined by the optional VRMC_node_collider extension.
    // Some exporters store collider shapes on nodes rather than in VRMC_springBone.colliders.
    static void BuildNodeColliderShapeMap(const TSharedPtr<FJsonObject>& Root,
        TMap<int32, TArray<FVRMSpringColliderSphere>>& OutSpheres,
        TMap<int32, TArray<FVRMSpringColliderCapsule>>& OutCapsules,
        TMap<int32, TArray<FVRMSpringColliderPlane>>& OutPlanes)
    {
        OutSpheres.Reset();
        OutCapsules.Reset();
        OutPlanes.Reset();

        if (!Root.IsValid())
        {
            return;
        }

        auto ParseShapesFromObj = [](const TSharedPtr<FJsonObject>& ShapesOwner,
                                     TArray<FVRMSpringColliderSphere>& Spheres,
                                     TArray<FVRMSpringColliderCapsule>& Capsules,
                                     TArray<FVRMSpringColliderPlane>& Planes)
        {
            const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
            if (ShapesOwner.IsValid() && ShapesOwner->TryGetArrayField(TEXT("shapes"), Shapes) && Shapes)
            {
                for (const TSharedPtr<FJsonValue>& SV : *Shapes)
                {
                    const TSharedPtr<FJsonObject>* SObj = nullptr;
                    if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;
                    ParseOneShapeObject(*SObj, Spheres, Capsules, Planes);
                }
            }
        };

        // 1) Root-level extension: extensions.VRMC_node_collider.colliders[]
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (Root->TryGetObjectField(TEXT("extensions"), Exts) && Exts && Exts->IsValid())
        {
            const TSharedPtr<FJsonObject>* NodeCol = nullptr;
            if ((*Exts)->TryGetObjectField(TEXT("VRMC_node_collider"), NodeCol) && NodeCol && NodeCol->IsValid())
            {
                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*NodeCol)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;
                        int32 NodeIndex = INDEX_NONE; (*CObj)->TryGetNumberField(TEXT("node"), NodeIndex);
                        if (NodeIndex == INDEX_NONE) continue;
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIndex);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIndex);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIndex);
                        ParseShapesFromObj(*CObj, SArr, CArr, PArr);
                    }
                }
            }
        }

        // 2) Per-node extension: nodes[i].extensions.VRMC_node_collider.(colliders[]|collider)
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (Root->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
        {
            for (int32 NodeIdx = 0; NodeIdx < Nodes->Num(); ++NodeIdx)
            {
                const TSharedPtr<FJsonValue>& NV = (*Nodes)[NodeIdx];
                const TSharedPtr<FJsonObject>* NObj = nullptr;
                if (!NV.IsValid() || !NV->TryGetObject(NObj) || !NObj || !NObj->IsValid()) continue;

                const TSharedPtr<FJsonObject>* NExts = nullptr;
                if (!(*NObj)->TryGetObjectField(TEXT("extensions"), NExts) || !NExts || !NExts->IsValid()) continue;

                const TSharedPtr<FJsonObject>* NodeCol = nullptr;
                if (!(*NExts)->TryGetObjectField(TEXT("VRMC_node_collider"), NodeCol) || !NodeCol || !NodeCol->IsValid()) continue;

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*NodeCol)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIdx);
                        ParseShapesFromObj(*CObj, SArr, CArr, PArr);
                    }
                }
                else
                {
                    const TSharedPtr<FJsonObject>* Single = nullptr;
                    if ((*NodeCol)->TryGetObjectField(TEXT("collider"), Single) && Single && Single->IsValid())
                    {
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIdx);
                        ParseShapesFromObj(*Single, SArr, CArr, PArr);
                    }
                }
            }
        }
    }

    static bool HasShape(const FVRMSpringCollider& Collider)
    {
        return Collider.Spheres.Num() > 0 || Collider.Capsules.Num() > 0 || Collider.Planes.Num() > 0;
    }

    // Reads the simulation parameters a VRM 1.0 joint may set. Anything it doesn't set keeps the
    // value already in InOut (the spec default, or a lenient spring-level value). Values are glTF.
    // Returns true if any field was present.
    static bool ReadJointParameters(const TSharedPtr<FJsonObject>& Obj, FVRMSpringJoint& InOut, bool bAllowDragAlias)
    {
        bool bAny = false;
        float Value = 0.f;
        if (Obj->TryGetNumberField(TEXT("stiffness"), Value)) { InOut.Stiffness = Value; bAny = true; }
        if (Obj->TryGetNumberField(TEXT("dragForce"), Value)) { InOut.Drag = Value; bAny = true; }
        else if (bAllowDragAlias && Obj->HasField(TEXT("drag")) && AcceptNonSpec(TEXT("'drag' (spec: 'dragForce')")) && Obj->TryGetNumberField(TEXT("drag"), Value)) { InOut.Drag = Value; bAny = true; }
        if (Obj->TryGetNumberField(TEXT("gravityPower"), Value)) { InOut.GravityPower = Value; bAny = true; }
        if (Obj->HasField(TEXT("gravityDir"))) { InOut.GravityDir = ReadVec3(Obj, TEXT("gravityDir"), InOut.GravityDir); bAny = true; }
        if (Obj->TryGetNumberField(TEXT("hitRadius"), Value)) { InOut.HitRadius = Value; bAny = true; }
        return bAny;
    }

    // VRM 1.0 (VRMC_springBone 1.0, with VRMC_springBone_extended_collider)
    static bool ParseVRM1(const TSharedPtr<FJsonObject>& Root, FVRMSpringConfig& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (!Root->TryGetObjectField(TEXT("extensions"), Exts) || !Exts || !Exts->IsValid())
        {
            OutError = TEXT("No 'extensions' for VRM1.");
            return false;
        }

        const TSharedPtr<FJsonObject>* Spring = nullptr;
        if (!(*Exts)->TryGetObjectField(TEXT("VRMC_springBone"), Spring) || !Spring || !Spring->IsValid())
        {
            OutError = TEXT("No 'VRMC_springBone' extension.");
            return false;
        }

        Out.Spec = EVRMSpringSpec::VRM1;

        // colliders: { "node": n, "shape": { "sphere" | "capsule": {..} } }. When the collider has
        // extensions.VRMC_springBone_extended_collider.shape, that shape replaces the base one (the
        // base is only a fallback for readers without the extension).
        TMap<int32, TArray<FVRMSpringColliderSphere>> NodeSpheres;
        TMap<int32, TArray<FVRMSpringColliderCapsule>> NodeCapsules;
        TMap<int32, TArray<FVRMSpringColliderPlane>> NodePlanes;
        bool bNodeCollidersBuilt = false;

        const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
        {
            for (const TSharedPtr<FJsonValue>& CV : *Colliders)
            {
                const TSharedPtr<FJsonObject>* CObj = nullptr;
                if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid())
                {
                    Out.Colliders.AddDefaulted(); // keep collider group indices lined up
                    continue;
                }

                FVRMSpringCollider Collider;
                (*CObj)->TryGetNumberField(TEXT("node"), Collider.NodeIndex);

                const TSharedPtr<FJsonObject>* ExtShape = nullptr;
                const TSharedPtr<FJsonObject>* ColliderExts = nullptr;
                if ((*CObj)->TryGetObjectField(TEXT("extensions"), ColliderExts) && ColliderExts && ColliderExts->IsValid())
                {
                    const TSharedPtr<FJsonObject>* Extended = nullptr;
                    if ((*ColliderExts)->TryGetObjectField(TEXT("VRMC_springBone_extended_collider"), Extended) && Extended && Extended->IsValid())
                    {
                        (*Extended)->TryGetObjectField(TEXT("shape"), ExtShape);
                    }
                }

                const TSharedPtr<FJsonObject>* BaseShape = nullptr;
                if (ExtShape && ExtShape->IsValid())
                {
                    ParseShapeContainer(*ExtShape, Collider.Spheres, Collider.Capsules, Collider.Planes);
                }
                else if ((*CObj)->TryGetObjectField(TEXT("shape"), BaseShape) && BaseShape && BaseShape->IsValid())
                {
                    ParseShapeContainer(*BaseShape, Collider.Spheres, Collider.Capsules, Collider.Planes);
                    if (!HasShape(Collider) && AcceptNonSpec(TEXT("wrapped or typed collider shape")))
                    {
                        ParseOneShapeObject(*BaseShape, Collider.Spheres, Collider.Capsules, Collider.Planes);
                    }
                }

                // Non-spec: a "shapes" array, or "shape" as an array.
                if (!HasShape(Collider))
                {
                    const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
                    const bool bShapesArray = (*CObj)->TryGetArrayField(TEXT("shapes"), Shapes) && Shapes;
                    if (!bShapesArray)
                    {
                        (*CObj)->TryGetArrayField(TEXT("shape"), Shapes);
                    }
                    if (Shapes && AcceptNonSpec(bShapesArray ? TEXT("collider 'shapes' array") : TEXT("collider 'shape' array")))
                    {
                        for (const TSharedPtr<FJsonValue>& SV : *Shapes)
                        {
                            const TSharedPtr<FJsonObject>* SObj = nullptr;
                            if (SV.IsValid() && SV->TryGetObject(SObj) && SObj && SObj->IsValid())
                            {
                                ParseOneShapeObject(*SObj, Collider.Spheres, Collider.Capsules, Collider.Planes);
                            }
                        }
                    }
                }

                // Non-spec: shapes stored on the node under a VRMC_node_collider extension.
                if (!HasShape(Collider) && Collider.NodeIndex != INDEX_NONE)
                {
                    if (!bNodeCollidersBuilt)
                    {
                        BuildNodeColliderShapeMap(Root, NodeSpheres, NodeCapsules, NodePlanes);
                        bNodeCollidersBuilt = true;
                    }
                    const bool bOnNode = NodeSpheres.Contains(Collider.NodeIndex) || NodeCapsules.Contains(Collider.NodeIndex) || NodePlanes.Contains(Collider.NodeIndex);
                    if (bOnNode && AcceptNonSpec(TEXT("VRMC_node_collider shapes")))
                    {
                        if (const TArray<FVRMSpringColliderSphere>* FoundS = NodeSpheres.Find(Collider.NodeIndex)) { Collider.Spheres.Append(*FoundS); }
                        if (const TArray<FVRMSpringColliderCapsule>* FoundC = NodeCapsules.Find(Collider.NodeIndex)) { Collider.Capsules.Append(*FoundC); }
                        if (const TArray<FVRMSpringColliderPlane>* FoundP = NodePlanes.Find(Collider.NodeIndex)) { Collider.Planes.Append(*FoundP); }
                    }
                }

                Out.Colliders.Add(MoveTemp(Collider));
            }
        }

        // Non-spec: no colliders at all, but shapes on nodes under VRMC_node_collider.
        if (Out.Colliders.Num() == 0)
        {
            BuildNodeColliderShapeMap(Root, NodeSpheres, NodeCapsules, NodePlanes);
            TSet<int32> NodesWithAnyShape;
            for (const auto& Pair : NodeSpheres) { NodesWithAnyShape.Add(Pair.Key); }
            for (const auto& Pair : NodeCapsules) { NodesWithAnyShape.Add(Pair.Key); }
            for (const auto& Pair : NodePlanes) { NodesWithAnyShape.Add(Pair.Key); }
            if (NodesWithAnyShape.Num() > 0 && AcceptNonSpec(TEXT("colliders only in VRMC_node_collider")))
            {
                for (int32 NodeIdx : NodesWithAnyShape)
                {
                    FVRMSpringCollider Synth;
                    Synth.NodeIndex = NodeIdx;
                    if (const TArray<FVRMSpringColliderSphere>* FoundS = NodeSpheres.Find(NodeIdx)) { Synth.Spheres.Append(*FoundS); }
                    if (const TArray<FVRMSpringColliderCapsule>* FoundC = NodeCapsules.Find(NodeIdx)) { Synth.Capsules.Append(*FoundC); }
                    if (const TArray<FVRMSpringColliderPlane>* FoundP = NodePlanes.Find(NodeIdx)) { Synth.Planes.Append(*FoundP); }
                    if (HasShape(Synth))
                    {
                        Out.Colliders.Add(MoveTemp(Synth));
                    }
                }
            }
        }

        // colliderGroups: { "name": .., "colliders": [indices] }
        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            for (const TSharedPtr<FJsonValue>& GV : *ColliderGroups)
            {
                FVRMSpringColliderGroup Group;
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (GV.IsValid() && GV->TryGetObject(GObj) && GObj && GObj->IsValid())
                {
                    (*GObj)->TryGetStringField(TEXT("name"), Group.Name);
                    const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
                    if ((*GObj)->TryGetArrayField(TEXT("colliders"), Indices) && Indices)
                    {
                        for (const TSharedPtr<FJsonValue>& IV : *Indices)
                        {
                            double Index = -1.0;
                            if (IV.IsValid() && IV->TryGetNumber(Index)) { Group.ColliderIndices.Add((int32)Index); }
                        }
                    }
                }
                Out.ColliderGroups.Add(MoveTemp(Group)); // keep spring colliderGroups indices lined up
            }
        }

        // The spec has no top-level joints array; springs[].joints holds the joint objects.
        if ((*Spring)->HasField(TEXT("joints")))
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring Parser] VRM1: ignored non-spec top-level 'joints' array; joints are read from springs[].joints."));
        }

        // springs: { "name", "joints": [ { "node", "hitRadius", "stiffness", "gravityPower",
        // "gravityDir", "dragForce" } ], "colliderGroups": [indices], "center": node }
        const TArray<TSharedPtr<FJsonValue>>* Springs = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("springs"), Springs) && Springs)
        {
            for (const TSharedPtr<FJsonValue>& SV : *Springs)
            {
                const TSharedPtr<FJsonObject>* SObj = nullptr;
                if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;

                FVRMSpring S;
                (*SObj)->TryGetStringField(TEXT("name"), S.Name);

                if (!(*SObj)->TryGetNumberField(TEXT("center"), S.CenterNodeIndex))
                {
                    S.CenterNodeIndex = INDEX_NONE;
                    if ((*SObj)->HasTypedField<EJson::Object>(TEXT("center")) && AcceptNonSpec(TEXT("'center' as an object")))
                    {
                        TryGetNodeIndexFlexible(*SObj, TEXT("center"), S.CenterNodeIndex);
                    }
                }

                // Spec defaults for joint parameters (glTF units; converted with everything else).
                FVRMSpringJoint Defaults;
                Defaults.HitRadius = 0.f;
                Defaults.Stiffness = 1.f;
                Defaults.Drag = 0.5f;
                Defaults.GravityPower = 0.f;
                Defaults.GravityDir = FVector(0, -1, 0);

                // Non-spec: parameters on the spring. Joints that don't set their own inherit them.
                {
                    FVRMSpringJoint SpringLevel = Defaults;
                    const bool bHasSpringLevel = (*SObj)->HasField(TEXT("stiffness")) || (*SObj)->HasField(TEXT("dragForce")) || (*SObj)->HasField(TEXT("drag"))
                        || (*SObj)->HasField(TEXT("gravityPower")) || (*SObj)->HasField(TEXT("gravityDir")) || (*SObj)->HasField(TEXT("hitRadius"));
                    if (bHasSpringLevel && AcceptNonSpec(TEXT("spring-level stiffness/drag/gravity/hitRadius (spec: per joint)")))
                    {
                        ReadJointParameters(*SObj, SpringLevel, /*bAllowDragAlias*/ true);
                        Defaults = SpringLevel;
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* SJ = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("joints"), SJ) && SJ)
                {
                    for (const TSharedPtr<FJsonValue>& JV : *SJ)
                    {
                        const TSharedPtr<FJsonObject>* JObj = nullptr;
                        if (!JV.IsValid() || !JV->TryGetObject(JObj) || !JObj || !JObj->IsValid())
                        {
                            // A number here is not a spec joint. Older non-spec files used it as an index
                            // into a top-level joints array, which is no longer read (see above).
                            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring Parser] VRM1: spring '%s' has a joint entry that is not a joint object; skipped."), *S.Name);
                            continue;
                        }

                        FVRMSpringJoint J = Defaults;
                        (*JObj)->TryGetNumberField(TEXT("node"), J.NodeIndex);
                        ReadJointParameters(*JObj, J, /*bAllowDragAlias*/ true);
                        S.JointIndices.Add(Out.Joints.Add(MoveTemp(J)));
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (const TSharedPtr<FJsonValue>& Gv : *CG)
                    {
                        double Index = -1.0;
                        if (Gv.IsValid() && Gv->TryGetNumber(Index)) { S.ColliderGroupIndices.Add((int32)Index); }
                    }
                }

                // Spring-level fields are editor helpers; show the first joint's values.
                const FVRMSpringJoint& Shown = S.JointIndices.Num() > 0 ? Out.Joints[S.JointIndices[0]] : Defaults;
                S.Stiffness = Shown.Stiffness;
                S.Drag = Shown.Drag;
                S.GravityPower = Shown.GravityPower;
                S.GravityDir = Shown.GravityDir;
                S.HitRadius = Shown.HitRadius;

                Out.Springs.Add(MoveTemp(S));
            }
        }

        return true;
    }

    // glTF node hierarchy from nodes[].children, plus which nodes carry a mesh (not bones).
    struct FNodeHierarchy
    {
        TMap<int32, int32> Parent;
        TMap<int32, FVRMNodeChildren> Children;
        TSet<int32> MeshNodes;
    };

    static FNodeHierarchy BuildNodeHierarchy(const TSharedPtr<FJsonObject>& Root)
    {
        FNodeHierarchy H;
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
        {
            return H;
        }
        const int32 Num = Nodes->Num();
        for (int32 i = 0; i < Num; ++i)
        {
            const TSharedPtr<FJsonObject>* Node = nullptr;
            if (!(*Nodes)[i].IsValid() || !(*Nodes)[i]->TryGetObject(Node) || !Node || !Node->IsValid()) continue;
            if ((*Node)->HasField(TEXT("mesh")))
            {
                H.MeshNodes.Add(i);
            }
            const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
            if ((*Node)->TryGetArrayField(TEXT("children"), Children) && Children)
            {
                for (const TSharedPtr<FJsonValue>& Child : *Children)
                {
                    double ChildIndex = -1.0;
                    if (Child.IsValid() && Child->TryGetNumber(ChildIndex) && ChildIndex >= 0.0 && ChildIndex < Num && int32(ChildIndex) != i
                        && !H.Parent.Contains(int32(ChildIndex)))
                    {
                        H.Parent.Add(int32(ChildIndex), i);
                        H.Children.FindOrAdd(i).Children.Add(int32(ChildIndex));
                    }
                }
            }
        }
        return H;
    }

    // VRM 0.x (secondaryAnimation; no plane shapes)
    static bool ParseVRM0(const TSharedPtr<FJsonObject>& Root, FVRMSpringConfig& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (!Root->TryGetObjectField(TEXT("extensions"), Exts) || !Exts || !Exts->IsValid())
        {
            OutError = TEXT("No 'extensions' for VRM0.");
            return false;
        }

        const TSharedPtr<FJsonObject>* VrmObj = nullptr;
        if (!(*Exts)->TryGetObjectField(TEXT("VRM"), VrmObj) || !VrmObj || !VrmObj->IsValid())
        {
            OutError = TEXT("No 'VRM' extension.");
            return false;
        }

        const TSharedPtr<FJsonObject>* Sec = nullptr;
        if (!(*VrmObj)->TryGetObjectField(TEXT("secondaryAnimation"), Sec) || !Sec || !Sec->IsValid())
        {
            OutError = TEXT("No 'secondaryAnimation' in VRM 0.x.");
            return false;
        }

        Out.Spec = EVRMSpringSpec::VRM0;
        const FNodeHierarchy Hierarchy = BuildNodeHierarchy(Root);

        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        TArray<int32> GroupIndexToFirstCollider;
        if ((*Sec)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            int32 ColliderBase = 0;
            for (const TSharedPtr<FJsonValue>& GV : *ColliderGroups)
            {
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (!GV.IsValid() || !GV->TryGetObject(GObj) || !GObj || !GObj->IsValid()) continue;

                int32 NodeIndex = INDEX_NONE;
                (*GObj)->TryGetNumberField(TEXT("node"), NodeIndex);

                FVRMSpringCollider GroupColliderTemplate; // node index propagated to all colliders in the group
                GroupColliderTemplate.NodeIndex = NodeIndex;

                FVRMSpringColliderGroup Group;
                GroupIndexToFirstCollider.Add(ColliderBase);

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;

                        FVRMSpringCollider Collider = GroupColliderTemplate; // copy node index

                        FVRMSpringColliderSphere S;
                        S.Offset = ReadVec3(*CObj, TEXT("offset"));
                        (*CObj)->TryGetNumberField(TEXT("radius"), S.Radius);
                        Collider.Spheres.Add(S);

                        const int32 ThisColliderIndex = Out.Colliders.Num();
                        Group.ColliderIndices.Add(ThisColliderIndex);
                        Out.Colliders.Add(MoveTemp(Collider));
                        ColliderBase++;
                    }
                }
                Out.ColliderGroups.Add(MoveTemp(Group));
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* BoneGroups = nullptr;
        if ((*Sec)->TryGetArrayField(TEXT("boneGroups"), BoneGroups) && BoneGroups)
        {
            for (const TSharedPtr<FJsonValue>& BV : *BoneGroups)
            {
                const TSharedPtr<FJsonObject>* BObj = nullptr;
                if (!BV.IsValid() || !BV->TryGetObject(BObj) || !BObj || !BObj->IsValid()) continue;

                FVRMSpring Spring;
                (*BObj)->TryGetStringField(TEXT("comment"), Spring.Name);
                (*BObj)->TryGetNumberField(TEXT("center"), Spring.CenterNodeIndex);

                bool bUsedLegacyStiffiness = false;
                if ((*BObj)->TryGetNumberField(TEXT("stiffiness"), Spring.Stiffness))
                {
                    bUsedLegacyStiffiness = true;
                    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring Parser] VRM0: detected legacy 'stiffiness' field and mapped to 'stiffness' (value=%.3f)"), Spring.Stiffness);
                }
                (*BObj)->TryGetNumberField(TEXT("stiffness"), Spring.Stiffness);
                (*BObj)->TryGetNumberField(TEXT("dragForce"), Spring.Drag);
                Spring.GravityDir = ReadVec3(*BObj, TEXT("gravityDir"), FVector(0, -1, 0));
                (*BObj)->TryGetNumberField(TEXT("gravityPower"), Spring.GravityPower);
                (*BObj)->TryGetNumberField(TEXT("hitRadius"), Spring.HitRadius);

                if (bUsedLegacyStiffiness)
                {
                    UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] VRM0: Mapped legacy 'stiffiness' to 'stiffness' for spring '%s'"), *Spring.Name);
                }

                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (const TSharedPtr<FJsonValue>& Gv : *CG)
                    {
                        double Index = -1.0;
                        if (Gv.IsValid() && Gv->TryGetNumber(Index)) { Spring.ColliderGroupIndices.Add((int32)Index); }
                    }
                }

                // "bones" lists only the root of each chain; the whole subtree below it moves (UniVRM,
                // three-vrm). Each chain follows the first child down to a leaf, and every other child
                // starts a chain of its own, so no joint is simulated twice. All chains of the group
                // share its parameters and colliders. Nodes carrying a mesh are not bones and stop the walk.
                // A root's branches are finished before the next listed root, so a listed bone that is
                // already below an earlier root joins that root's chains rather than starting its own.
                TArray<int32> Roots;
                const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("bones"), Bones) && Bones)
                {
                    for (const TSharedPtr<FJsonValue>& BVV : *Bones)
                    {
                        double NodeIndex = -1.0;
                        if (BVV.IsValid() && BVV->TryGetNumber(NodeIndex) && NodeIndex >= 0.0) { Roots.Add((int32)NodeIndex); }
                    }
                }

                // Nodes the listed roots cover, so a later listed root inside an earlier subtree is skipped.
                TSet<int32> InGroup;
                TArray<int32> ChainStarts; // stack; the next branch on top
                for (const int32 ListedRoot : Roots)
                {
                    ChainStarts.Reset();
                    ChainStarts.Push(ListedRoot);
                    while (ChainStarts.Num() > 0)
                    {
                        FVRMSpring Chain = Spring; // name, center, parameters and collider groups
                        for (int32 Node = ChainStarts.Pop(); Node != INDEX_NONE && !InGroup.Contains(Node) && !Hierarchy.MeshNodes.Contains(Node); )
                        {
                            InGroup.Add(Node);

                            // VRM 0.x parameters belong to the bone group; every joint gets a copy.
                            FVRMSpringJoint J;
                            J.NodeIndex = Node;
                            J.Stiffness = Spring.Stiffness;
                            J.Drag = Spring.Drag;
                            J.GravityDir = Spring.GravityDir;
                            J.GravityPower = Spring.GravityPower;
                            J.HitRadius = Spring.HitRadius;
                            Chain.JointIndices.Add(Out.Joints.Add(J));

                            const FVRMNodeChildren* Kids = Hierarchy.Children.Find(Node);
                            if (!Kids || Kids->Children.Num() == 0)
                            {
                                break;
                            }
                            for (int32 k = Kids->Children.Num() - 1; k >= 1; --k) // pushed in reverse: first branch on top
                            {
                                ChainStarts.Push(Kids->Children[k]);
                            }
                            Node = Kids->Children[0];
                        }

                        if (Chain.JointIndices.Num() > 0)
                        {
                            Out.Springs.Add(MoveTemp(Chain));
                        }
                    }
                }
            }
        }

        return true;
    }
}

namespace
{
    // World (model-space) transform of every glTF node, in glTF axes and metres. Row-vector FMatrix,
    // like the translator's: Global = Local * ParentGlobal.
    static TArray<FMatrix> ComputeNodeWorldMatrices(const TSharedPtr<FJsonObject>& Root)
    {
        TArray<FMatrix> World;
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
        {
            return World;
        }

        const int32 Num = Nodes->Num();
        TArray<FMatrix> Local;
        Local.Init(FMatrix::Identity, Num);
        TArray<int32> Parent;
        Parent.Init(INDEX_NONE, Num);
        for (int32 i = 0; i < Num; ++i)
        {
            const TSharedPtr<FJsonObject>* Node = nullptr;
            if (!(*Nodes)[i].IsValid() || !(*Nodes)[i]->TryGetObject(Node) || !Node || !Node->IsValid())
            {
                continue;
            }

            const TArray<TSharedPtr<FJsonValue>>* Matrix = nullptr;
            if ((*Node)->TryGetArrayField(TEXT("matrix"), Matrix) && Matrix && Matrix->Num() == 16)
            {
                // Column-major for column vectors; the same 16 numbers row-major are the row-vector FMatrix.
                for (int32 r = 0; r < 4; ++r)
                {
                    for (int32 c = 0; c < 4; ++c)
                    {
                        double V = (r == c) ? 1.0 : 0.0;
                        if ((*Matrix)[r * 4 + c].IsValid()) { (*Matrix)[r * 4 + c]->TryGetNumber(V); }
                        Local[i].M[r][c] = V;
                    }
                }
            }
            else
            {
                FQuat Rotation = FQuat::Identity;
                const TArray<TSharedPtr<FJsonValue>>* Rot = nullptr;
                if ((*Node)->TryGetArrayField(TEXT("rotation"), Rot) && Rot && Rot->Num() == 4)
                {
                    double Q[4] = { 0.0, 0.0, 0.0, 1.0 };
                    for (int32 k = 0; k < 4; ++k) { if ((*Rot)[k].IsValid()) { (*Rot)[k]->TryGetNumber(Q[k]); } }
                    Rotation = FQuat(Q[0], Q[1], Q[2], Q[3]).GetNormalized();
                }
                Local[i] = FTransform(Rotation, ReadVec3(*Node, TEXT("translation")), ReadVec3(*Node, TEXT("scale"), FVector::OneVector)).ToMatrixWithScale();
            }

            const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
            if ((*Node)->TryGetArrayField(TEXT("children"), Children) && Children)
            {
                for (const TSharedPtr<FJsonValue>& Child : *Children)
                {
                    double ChildIndex = -1.0;
                    if (Child.IsValid() && Child->TryGetNumber(ChildIndex) && ChildIndex >= 0.0 && ChildIndex < Num)
                    {
                        Parent[int32(ChildIndex)] = i;
                    }
                }
            }
        }

        World.SetNum(Num);
        TArray<uint8> State; // 0 = not visited, 1 = in progress (a cycle in a malformed file), 2 = done
        State.Init(0, Num);
        TFunction<void(int32)> Resolve = [&](int32 i)
        {
            if (State[i] == 2) { return; }
            if (State[i] == 1) { World[i] = Local[i]; return; }
            State[i] = 1;
            const int32 P = Parent[i];
            if (P != INDEX_NONE)
            {
                Resolve(P);
                World[i] = Local[i] * World[P];
            }
            else
            {
                World[i] = Local[i];
            }
            State[i] = 2;
        };
        for (int32 i = 0; i < Num; ++i)
        {
            Resolve(i);
        }
        return World;
    }

    /**
     * Converts a parsed spring config from glTF (node-local offsets, metres) to what the importer
     * and the runtime use: Unreal axes and centimetres, with collider offsets expressed in the
     * axis-aligned bone space of the imported skeleton.
     *
     * Imported bones have identity rest rotation (see the translator's reference pose), so a
     * node-local offset is first taken through the node's original glTF world transform (rotation
     * and scale), then converted like mesh vertices (VRMCoordinateConversion.h). Collider radii
     * follow the node's world scale. Gravity is already in model space, so it only changes axes.
     *
     * VRM 0.x writes collider offsets with Z negated relative to glTF (UniVRM 0.x); three-vrm's
     * VRM 0.x loader negates it back ("z is opposite in VRM0.0"). Its gravityDir is not negated.
     * VRM 0.x models face -Z, so everything also gets the 180-degree yaw the translator applies to
     * the mesh and skeleton (FVRMAxisConvention).
     */
    static void ConvertSpringConfigToUE(const TSharedPtr<FJsonObject>& Root, FVRMSpringConfig& Config)
    {
        using namespace VRM::Coord;
        const float Scale = MetersToCentimeters;
        const TArray<FMatrix> NodeWorld = ComputeNodeWorldMatrices(Root);
        const bool bVRM0 = (Config.Spec == EVRMSpringSpec::VRM0);
        // Same facing as the translator uses for the mesh and skeleton (VRM 0.x gets a 180-degree yaw).
        const FVRMAxisConvention Convention = FVRMAxisConvention::ForVersion(bVRM0 ? EVRMVersion::VRM0 : EVRMVersion::VRM1, Scale);

        for (FVRMSpringCollider& Collider : Config.Colliders)
        {
            const FMatrix World = NodeWorld.IsValidIndex(Collider.NodeIndex) ? NodeWorld[Collider.NodeIndex] : FMatrix::Identity;
            const FMatrix NormalXf = World.Inverse().GetTransposed();
            const float RadiusScale = Scale * float(FVector(World.M[0][0], World.M[0][1], World.M[0][2]).Size());
            auto ConvertOffset = [&World, &Convention, bVRM0](FVector Offset)
            {
                if (bVRM0)
                {
                    Offset.Z = -Offset.Z;
                }
                return Convention.Position(FVector(World.TransformVector(Offset)));
            };

            for (FVRMSpringColliderSphere& Sphere : Collider.Spheres)
            {
                Sphere.Offset = ConvertOffset(Sphere.Offset);
                Sphere.Radius *= RadiusScale;
            }
            for (FVRMSpringColliderCapsule& Capsule : Collider.Capsules)
            {
                Capsule.Offset = ConvertOffset(Capsule.Offset);
                Capsule.TailOffset = ConvertOffset(Capsule.TailOffset);
                Capsule.Radius *= RadiusScale;
            }
            for (FVRMSpringColliderPlane& Plane : Collider.Planes)
            {
                Plane.Offset = ConvertOffset(Plane.Offset);
                Plane.Normal = Convention.Direction(FVector(NormalXf.TransformVector(Plane.Normal))).GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 0, 1));
            }
        }

        for (FVRMSpringJoint& Joint : Config.Joints)
        {
            Joint.HitRadius *= Scale;
            Joint.GravityDir = Convention.Direction(Joint.GravityDir).GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 0, -1));
            Joint.GravityPower *= Scale;
        }

        for (FVRMSpring& Spring : Config.Springs)
        {
            Spring.GravityDir = Convention.Direction(Spring.GravityDir).GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 0, -1));
            Spring.GravityPower *= Scale;
            Spring.HitRadius *= Scale;
        }
    }
}

namespace
{
    // Spring bones from an already parsed top-level JSON object.
    bool ParseSpringBonesFromRoot(const TSharedPtr<FJsonObject>& Root, const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        OutConfig = FVRMSpringConfig();
        OutError.Empty();
        if (ParseVRM1(Root, OutConfig, OutError)) { ConvertSpringConfigToUE(Root, OutConfig); OutConfig.RawJson = Json; UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] Parsed VRM spring bones as VRM1: Springs=%d Colliders=%d Joints=%d ColliderGroups=%d"), OutConfig.Springs.Num(), OutConfig.Colliders.Num(), OutConfig.Joints.Num(), OutConfig.ColliderGroups.Num()); return true; }
        FString Err0; FVRMSpringConfig As0; if (ParseVRM0(Root, As0, Err0)) { OutConfig = MoveTemp(As0); ConvertSpringConfigToUE(Root, OutConfig); OutConfig.RawJson = Json; OutError.Reset(); UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] Parsed VRM spring bones as VRM0: Springs=%d Colliders=%d Joints=%d ColliderGroups=%d"), OutConfig.Springs.Num(), OutConfig.Colliders.Num(), OutConfig.Joints.Num(), OutConfig.ColliderGroups.Num()); return true; }
        OutError = TEXT("No VRM spring bone data detected."); return false;
    }

    // glTF node index -> node name, for the nodes that have a name.
    void ReadNodeNames(const TSharedPtr<FJsonObject>& Root, TMap<int32, FName>& OutNodeMap)
    {
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (Root.IsValid() && Root->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
        {
            for (int32 i = 0; i < Nodes->Num(); ++i)
            {
                const TSharedPtr<FJsonValue>& V = (*Nodes)[i];
                const TSharedPtr<FJsonObject>* NObj = nullptr;
                if (V.IsValid() && V->TryGetObject(NObj) && NObj && NObj->IsValid())
                {
                    FString NameStr;
                    if ((*NObj)->TryGetStringField(TEXT("name"), NameStr) && !NameStr.IsEmpty())
                    {
                        OutNodeMap.Add(i, FName(*NameStr));
                    }
                }
            }
        }
    }

    TSharedPtr<FJsonObject> DeserializeJson(const FString& Json)
    {
        TSharedPtr<FJsonObject> Root;
        if (Json.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root))
        {
            return nullptr;
        }
        return Root;
    }
}

namespace VRM
{
    bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        OutConfig = FVRMSpringConfig();
        OutError.Empty();
        if (Json.IsEmpty()) { OutError = TEXT("Empty JSON."); return false; }
        const TSharedPtr<FJsonObject> Root = DeserializeJson(Json);
        if (!Root.IsValid()) { OutError = TEXT("Failed to parse JSON."); return false; }
        return ParseSpringBonesFromRoot(Root, Json, OutConfig, OutError);
    }

    bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError)
    {
        OutNodeMap.Reset();
        if (!ParseSpringBonesFromJson(Json, OutConfig, OutError))
        {
            return false;
        }
        ReadNodeNames(DeserializeJson(Json), OutNodeMap);
        return true;
    }

    bool ParseSpringBonesFromDocument(const FVRMDocument& Document, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, TMap<int32, int32>& OutNodeParent, TMap<int32, FVRMNodeChildren>& OutNodeChildren, FString& OutError)
    {
        OutNodeMap.Reset();
        OutNodeParent.Reset();
        OutNodeChildren.Reset();
        const TSharedPtr<FJsonObject> Root = Document.GetJsonRoot();
        if (!ParseSpringBonesFromRoot(Root, Document.GetJson(), OutConfig, OutError))
        {
            return false;
        }
        ReadNodeNames(Root, OutNodeMap);
        FNodeHierarchy Hierarchy = BuildNodeHierarchy(Root);
        OutNodeParent = MoveTemp(Hierarchy.Parent);
        OutNodeChildren = MoveTemp(Hierarchy.Children);
        return true;
    }

    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, TMap<int32, int32>& OutNodeParent, TMap<int32, FVRMNodeChildren>& OutNodeChildren, FString& OutError)
    {
        const TSharedPtr<const FVRMDocument> Document = FVRMDocument::LoadFile(Filename, OutError);
        if (!Document.IsValid())
        {
            OutConfig = FVRMSpringConfig();
            OutNodeMap.Reset();
            OutNodeParent.Reset();
            OutNodeChildren.Reset();
            return false;
        }
        return ParseSpringBonesFromDocument(*Document, OutConfig, OutNodeMap, OutNodeParent, OutNodeChildren, OutError);
    }

    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError)
    {
        TMap<int32, int32> NodeParent;
        TMap<int32, FVRMNodeChildren> NodeChildren;
        return ParseSpringBonesFromFile(Filename, OutConfig, OutNodeMap, NodeParent, NodeChildren, OutError);
    }

    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        TMap<int32, FName> NodeMap;
        return ParseSpringBonesFromFile(Filename, OutConfig, NodeMap, OutError);
    }
}
