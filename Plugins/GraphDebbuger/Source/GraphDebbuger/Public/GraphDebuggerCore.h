

#pragma once

#include "CoreMinimal.h"
//#include "UObject/NoExportTypes.h"
#include "GraphDebuggerCore.generated.h"

// Struktura przechowuj¹ca dane dla ka¿dego wykresu
USTRUCT(BlueprintType)
struct FGraphData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FName GraphName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FLinearColor LineColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    float LineThickness = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    FVector2D YAxisRange = FVector2D(0.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Graph")
    bool bAutoNormalizeYAxis = false;

    TArray<float> DataPoints; // Przechowuje wartoœci danych
};



//UCLASS()
class GRAPHDEBBUGER_API GraphDebuggerCore
{
    //GENERATED_BODY()
public:

    GraphDebuggerCore();
    ~GraphDebuggerCore();

    // Inicjalizacja parametrów wykresu
    void Initialize(const FVector2D& InPosition, const FVector2D& InScale, int32 InMaxPoints);

    // Dodanie nowego wykresu
    void AddGraph(const FGraphData& Graph);

    // Aktualizacja danych wykresu
    void UpdateGraphData(FName GraphName, float Value);

    // Rysowanie wykresów na ekranie
    void Draw() const;

private:
    FVector2D Position;         // Pozycja na ekranie
    FVector2D Scale;            // Skala wykresu
    int32 MaxDataPoints;        // Maksymalna liczba punktów na osi X

    TArray<FGraphData> Graphs;  // Lista wszystkich wykresów

    void NormalizeGraphYAxis(FGraphData& Graph) const;
};