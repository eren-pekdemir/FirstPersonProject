#include "Public/UI/InteractPromptWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

void UInteractPromptWidget::SetPromptText(const FText& InText)
{
	// BindWidgetOptional ismi WBP'deki TextBlock ile eşleşmediyse (örn. yazım
	// hatası), ağaçtaki ilk TextBlock'u otomatik bul ki widget sessizce
	// çalışmaz hale gelmesin.
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

	if (PromptText)
	{
		PromptText->SetText(InText);
	}
}
