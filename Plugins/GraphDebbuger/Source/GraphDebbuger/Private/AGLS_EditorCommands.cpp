


#include "AGLS_EditorCommands.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

FAutoConsoleCommand UAGLS_EditorCommands::ToggleVisibilityCommand = FAutoConsoleCommand(
    TEXT("AGLS.AutoNavLinksGenerator.Show"),
    TEXT("Toggles visibility of all instances of AutoNavLinksGenerator on the scene."),
    FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
        {
            // Sprawdzenie argumentów komendy
            if (Args.Num() > 0)
            {
                const bool bHide = Args[0].ToLower() == TEXT("true");
                UAGLS_EditorCommands::SetInstancesVisibility(!bHide, TEXT("NavLinksAutoGenerator"), TEXT("NavLinksAutoGenerator"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Usage: AGLS.AutoNavLinksGenerator.Show.ToggleVisibility <true|false>"));
            }
        })
);


FAutoConsoleCommand UAGLS_EditorCommands::AGLS_ClimbNavVisiblity = FAutoConsoleCommand(
    TEXT("AGLS.ClimbNavigationData.Show"),
    TEXT("Toggles visibility of all instances of BP_ClimbNav_NavigationData on the scene."),
    FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
        {
            // Sprawdzenie argumentów komendy
            if (Args.Num() > 0)
            {
                const bool bHide = Args[0].ToLower() == TEXT("true");
                UAGLS_EditorCommands::SetInstancesVisibility(!bHide, TEXT("ClimbNavigationStorageActor"), TEXT("ClimbNav_NavigationData"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Usage: AGLS.ClimbNavigationData.Show.ToggleVisibility <true|false>"));
            }
        })
);



void UAGLS_EditorCommands::SetInstancesVisibility(bool bHide, FName TagName, FString ActorName)
{
#if WITH_EDITOR
    // Pobierz œwiat globalnie
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid world found!"));
        return;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor)
        {
            // Ukrycie w edytorze
            //Actor->SetIsTemporarilyHiddenInEditor(bHide);

            int TagIndex = Actor->Tags.Find(TagName);

            if (TagIndex != -1 || Actor->GetName().Contains(ActorName))
            {
                for (UActorComponent* Component : Actor->GetComponents())
                {
                    if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
                    {
                        PrimitiveComponent->SetVisibility(!bHide, true);
                        //PrimitiveComponent->SetHiddenInGame(bHide);
                    }
                }

                UE_LOG(LogTemp, Log, TEXT("Actor %s visibility set to %s"), *Actor->GetName(), bHide ? TEXT("Hidden") : TEXT("Visible"));
            }
        }
    }
#endif
}