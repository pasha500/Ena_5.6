// Copyright Pasha, All Rights Reserved.

#include "PashaCableComponentBPLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PashaCableComponent.h"

UPashaCableComponentBPLibrary::UPashaCableComponentBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

ECollisionTraceFlag UPashaCableComponentBPLibrary::GetCollisionTraceFlag(UStaticMeshComponent* Object)
{
    UStaticMesh* Mesh = Object->GetStaticMesh();
    if (Mesh != nullptr)
    {
        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (BodySetup != nullptr)
        {
            return BodySetup->CollisionTraceFlag;
        }
    }
    return ECollisionTraceFlag::CTF_UseDefault;
}

float UPashaCableComponentBPLibrary::PerformPIDControl(float CurrentValue, float TargetValue, float IntegralSum, float PreviousError, float Kp, float Ki, float Kd, float dt)
{
    return 0.0f;
}
