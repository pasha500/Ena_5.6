

#include "JWAN_PoseSnapShotLogic.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

void FJWAN_PoseSnapShotLogic::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
    //Initialize_AnyThread(Context);
    SourcePose.Initialize(Context);
    if (IsValid(AnimInst) == false)
    {
        AnimInst = Cast<UAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject());
    }

}

void FJWAN_PoseSnapShotLogic::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
    //SourcePose.CacheBones(Context);
}


void FJWAN_PoseSnapShotLogic::Evaluate_AnyThread(FPoseContext& Output)
{
    // Najpierw oceniamy przychodz¹c?poz?
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
    FPoseContext SourceData(Output);
    SourcePose.Evaluate(SourceData);
    Output = SourceData;

    // Make Basic Values For FPoseSnapshot
    FPoseSnapshot PoseSnapshot;
    PoseSnapshot.SnapshotName = SnapshotName; 
    PoseSnapshot.bIsValid = true;
    PoseSnapshot.SkeletalMeshName = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetSkeletalMeshAsset()->GetFName();

    // Musimy przekszta³ci?FCompactPose na TArray<FTransform>
    PoseSnapshot.LocalTransforms.SetNumUninitialized(Output.Pose.GetNumBones());
    Output.Pose.CopyBonesTo(PoseSnapshot.LocalTransforms);

    //Save LocalVariable To Global
    Snapshot = PoseSnapshot;

    if (SaveBonesNameToPose == true)
    {
        // Pobierz USkeletalMeshComponent z AnimInstanceProxy
        USkeletalMeshComponent* SkeletalMeshComponent = Output.AnimInstanceProxy->GetSkelMeshComponent();
        if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshAsset() && SkeletalMeshComponent->GetSkeletalMeshAsset()->GetSkeleton())
        {
            // Pobierz USkeleton
            const USkeleton* Skeleton = SkeletalMeshComponent->GetSkeletalMeshAsset()->GetSkeleton();

            // Zarezerwuj odpowiedni?iloœæ miejsca dla nazw koœci
            PoseSnapshot.BoneNames.Reserve(Output.Pose.GetNumBones());

            // Iteruj przez wszystkie indeksy koœci w FCompactPose
            for (FCompactPoseBoneIndex BoneIndex : Output.Pose.ForEachBoneIndex())
            {
                // Pobierz nazw?koœci z FReferenceSkeleton
                const FName BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(Output.Pose.GetBoneContainer().MakeMeshPoseIndex(BoneIndex).GetInt());
                PoseSnapshot.BoneNames.Add(BoneName);
            }
        }
    }

}

void FJWAN_PoseSnapShotLogic::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
    // Run update on input pose nodes
    SourcePose.Update(Context);
}


void FJWAN_PoseSnapShotLogic::InitializeSnapshot(FPoseSnapshot* InSnapshot)
{
    return;
}

FPoseSnapshot FJWAN_PoseSnapShotLogic::GetSavedPoseStructure()
{
    return Snapshot;
}
