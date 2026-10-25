#include "UI/InteractPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

void UInteractPromptWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!PromptText && WidgetTree)
	{
		WidgetTree->ForEachWidget([this](UWidget* Widget)
		{
			if (!PromptText)
			{
				PromptText = Cast<UTextBlock>(Widget);
			}
		});
	}
}

void UInteractPromptWidget::SetPromptText(const FText& InText)
{
	if (PromptText)
	{
		PromptText->SetText(InText);
	}
}
