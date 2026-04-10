#include "Raid/RaidCompassWidget.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

float URaidCompassWidget::GetCompassRatio(FVector TargetLocation, float CompassFOV) const
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC || !PC->PlayerCameraManager) return 0.0f;

    const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
    if ((TargetLocation - CamLoc).IsNearlyZero())
    {
        return 0.0f;
    }

    const float CamYaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
    const float TargetYaw = (TargetLocation - CamLoc).Rotation().Yaw;
    const float DeltaYaw = FMath::FindDeltaAngleDegrees(CamYaw, TargetYaw);

    const float HalfFOV = FMath::Max(1.0f, CompassFOV / 2.0f);
    const float Ratio = DeltaYaw / HalfFOV;
    // UI 밖으로 튀어 marker가 사라지는 것을 방지
    return FMath::Clamp(Ratio, -1.0f, 1.0f);
}

float URaidCompassWidget::GetGuidanceOpacity(float Urgency) const
{
    const float U = FMath::Clamp(Urgency, 0.0f, 1.0f);
    // 낮은 구간은 거의 보이지 않게, 길을 잃었을 때만 눈에 띄게 증가
    return FMath::InterpEaseInOut(0.08f, 1.0f, U, 2.0f);
}
