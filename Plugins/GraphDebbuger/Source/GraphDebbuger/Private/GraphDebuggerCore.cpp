


#include "GraphDebuggerCore.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/KismetMathLibrary.h"


GraphDebuggerCore::GraphDebuggerCore()
    : Position(FVector2D(50.0f, 50.0f))
    , Scale(FVector2D(300.0f, 100.0f))
    , MaxDataPoints(100)
{
}

GraphDebuggerCore::~GraphDebuggerCore()
{
}

void GraphDebuggerCore::Initialize(const FVector2D& InPosition, const FVector2D& InScale, int32 InMaxPoints)
{
    Position = InPosition;
    Scale = InScale;
    MaxDataPoints = InMaxPoints;
}

void GraphDebuggerCore::AddGraph(const FGraphData& Graph)
{
    Graphs.Add(Graph);
}

void GraphDebuggerCore::UpdateGraphData(FName GraphName, float Value)
{
    for (FGraphData& Graph : Graphs)
    {
        if (Graph.GraphName == GraphName)
        {
            Graph.DataPoints.Add(Value);

            // Usuñ nadmiarowe dane, jeœli przekroczono MaxDataPoints
            if (Graph.DataPoints.Num() > MaxDataPoints)
            {
                Graph.DataPoints.RemoveAt(0);
            }

            // Normalizacja osi Y, jeœli w³¹czona
            if (Graph.bAutoNormalizeYAxis)
            {
                NormalizeGraphYAxis(Graph);
            }

            return;
        }
    }
}

void GraphDebuggerCore::Draw() const
{

#if WITH_EDITOR

    FCanvas* Canvas = nullptr;

    if (GEditor && GEditor->GetActiveViewport())
    {
        FViewport* Viewport = GEditor->GetActiveViewport();
        Canvas = Viewport->GetDebugCanvas();
    }

    if (!Canvas)
    {   
        UE_LOG(LogTemp, Warning, TEXT("Graph Debugger: Can not draw any item becouse Canvas object is NULL"));
        return;
    }

    for (const FGraphData& Graph : Graphs)
    {
        if (Graph.DataPoints.Num() < 2) continue;

        // Rysowanie linii na podstawie danych
        for (int32 i = 1; i < Graph.DataPoints.Num(); i++)
        {
            float X1 = Position.X + ((i - 1) / (float)MaxDataPoints) * Scale.X;
            float Y1 = Position.Y + Scale.Y - UKismetMathLibrary::MapRangeClamped(
                Graph.DataPoints[i - 1],
                Graph.YAxisRange.X,
                Graph.YAxisRange.Y,
                0.0f,
                Scale.Y
            );

            float X2 = Position.X + (i / (float)MaxDataPoints) * Scale.X;
            float Y2 = Position.Y + Scale.Y - UKismetMathLibrary::MapRangeClamped(
                Graph.DataPoints[i],
                Graph.YAxisRange.X,
                Graph.YAxisRange.Y,
                0.0f,
                Scale.Y
            );

            // Utwórz liniê na Canvas
            FCanvasLineItem LineItem(FVector2D(X1, Y1), FVector2D(X2, Y2));
            LineItem.SetColor(Graph.LineColor);
            LineItem.LineThickness = Graph.LineThickness;

            UE_LOG(LogTemp, Log, TEXT("Graph Debugger: Trying to Draw ITEM"));

            Canvas->DrawItem(LineItem);
        }
    }
#endif

}

void GraphDebuggerCore::NormalizeGraphYAxis(FGraphData& Graph) const
{
    if (Graph.DataPoints.Num() == 0) return;

    float MinValue = TNumericLimits<float>::Max();
    float MaxValue = TNumericLimits<float>::Lowest();

    for (float Value : Graph.DataPoints)
    {
        MinValue = FMath::Min(MinValue, Value);
        MaxValue = FMath::Max(MaxValue, Value);
    }

    Graph.YAxisRange = FVector2D(MinValue, MaxValue);
}