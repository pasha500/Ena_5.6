


#include "AGLS_NavLinkCustomComponent.h"

#if WITH_EDITOR
void UAGLS_NavLinkCustomComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	GEngine->AddOnScreenDebugMessage(32, 2, FColor::Emerald, TEXT("JAPA"));
	if (GetWorld())
	{
		FVector DirectionStart = GetStartPoint() - GetEndPoint();
		DirectionStart.Normalize();

		FVector DirectionEnd = GetEndPoint() - GetStartPoint();
		DirectionEnd.Normalize();


		FBox BB = FBox(ForceInit);
		BB += GetStartPoint() + (DirectionStart * StartRelativeBoundOffset.X);
		BB += GetEndPoint() + (DirectionEnd * EndRelativeBoundOffset.X);


		DrawDebugBox(GetWorld(), GetNavigationBounds().GetCenter(), GetNavigationBounds().GetExtent(), FColor::Blue, false, 1.0, 0, 1.0);
		DrawDebugBox(GetWorld(), BB.GetCenter(), BB.GetExtent(), FColor::Emerald, false, 1.0, 0, 1.0);
	}
}
#endif


bool UAGLS_NavLinkCustomComponent::OnLinkMoveStarted(UObject* PathComp, const FVector& DestPoint)
{
	GEngine->AddOnScreenDebugMessage(32, 2, FColor::Emerald, DestPoint.ToString());

	DrawDebugBox(GetWorld(), GetNavigationBounds().GetCenter(), GetNavigationBounds().GetExtent(), FColor::Emerald, false, 1.0, 0, 0.6);

	return Super::OnLinkMoveStarted(PathComp, DestPoint);
}

void UAGLS_NavLinkCustomComponent::CalcAndCacheBounds() const
{

	FVector DirectionStart = GetStartPoint() - GetEndPoint();
	DirectionStart.Normalize();

	FVector DirectionEnd = GetEndPoint() - GetStartPoint();
	DirectionEnd.Normalize();


	Bounds = FBox(ForceInit);
	Bounds += GetStartPoint() + (DirectionStart * StartRelativeBoundOffset.X);
	Bounds += GetEndPoint() + (DirectionEnd * EndRelativeBoundOffset.X);

	if (bCreateBoxObstacle)
	{
		FBox ObstacleBounds = FBox::BuildAABB(ObstacleOffset, ObstacleExtent);
		Bounds += ObstacleBounds.TransformBy(GetOwner()->GetTransform());
	}
}

bool UAGLS_NavLinkCustomComponent::HasReachedLinkStart(const UObject* PathComp, const FVector& CurrentLocation, const FNavPathPoint& LinkStart, const FNavPathPoint& LinkEnd) const
{
	return Super::HasReachedLinkStart(PathComp, CurrentLocation, LinkStart, LinkEnd);
}

