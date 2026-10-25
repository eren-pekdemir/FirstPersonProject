#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractPromptWidget.generated.h"

class UTextBlock;

/**
 *  On-screen prompt shown while the player looks at something they can interact with.
 */
UCLASS(Abstract)
class FIRSTPERSONPROJECT_API UInteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetPromptText(const FText& InText);

protected:

	virtual void NativeOnInitialized() override;

	/** Bound by name. Falls back to the first TextBlock in the widget tree if no widget is named "PromptText". */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText;
};
