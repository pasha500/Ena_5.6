#include "Pasha_PoseSnapShotGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"

FText UPasha_PoseSnapShotGraph::GetTooltipText() const
{
    return FText::FromString("Saves the current pose to a snapshot.");
}

FText UPasha_PoseSnapShotGraph::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return FText::FromString("Save Current Pose To Snapshot");
}
