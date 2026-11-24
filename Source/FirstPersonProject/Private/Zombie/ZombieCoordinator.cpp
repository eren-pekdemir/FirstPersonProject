#include "Zombie/ZombieCoordinator.h"

#include "GameFramework/Pawn.h"
#include "Zombie/ZombieAIController.h"
#include "Zombie/ZombieCharacter.h"

void UZombieCoordinator::Prune()
{
	Zombies.RemoveAll([](const TWeakObjectPtr<AZombieAIController>& Zombie) { return !Zombie.IsValid(); });
	Attackers.RemoveAll([](const TWeakObjectPtr<AZombieAIController>& Zombie) { return !Zombie.IsValid() || !Zombie->IsAlive(); });
}

void UZombieCoordinator::Register(AZombieAIController* Zombie)
{
	if (Zombie)
	{
		Zombies.AddUnique(Zombie);
	}
}

void UZombieCoordinator::Unregister(AZombieAIController* Zombie)
{
	Zombies.Remove(Zombie);
	Attackers.Remove(Zombie);
}

bool UZombieCoordinator::RequestAttackSlot(AZombieAIController* Zombie)
{
	Prune();

	if (Attackers.Contains(Zombie))
	{
		return true;
	}

	if (Attackers.Num() < MaxAttackers)
	{
		Attackers.Add(Zombie);
		return true;
	}

	return false;
}

void UZombieCoordinator::ReleaseAttackSlot(AZombieAIController* Zombie)
{
	Attackers.Remove(Zombie);
}

bool UZombieCoordinator::HasAttackSlot(const AZombieAIController* Zombie) const
{
	return Attackers.ContainsByPredicate([Zombie](const TWeakObjectPtr<AZombieAIController>& Attacker) { return Attacker.Get() == Zombie; });
}

void UZombieCoordinator::BroadcastAlert(const FVector& Location, APawn* Target, float Radius, const AZombieAIController* Source)
{
	Prune();

	const float RadiusSquared = FMath::Square(Radius);
	for (const TWeakObjectPtr<AZombieAIController>& Zombie : Zombies)
	{
		const APawn* Pawn = Zombie.IsValid() ? Zombie->GetPawn() : nullptr;
		if (Zombie.Get() != Source && Pawn && FVector::DistSquared(Pawn->GetActorLocation(), Location) <= RadiusSquared)
		{
			// Further ones react a little later, so the alarm visibly spreads through the horde
			const float Delay = FMath::Sqrt(FVector::DistSquared(Pawn->GetActorLocation(), Location)) / 2500.f + FMath::FRandRange(0.1f, 0.5f);
			Zombie->HearAlert(Target, Location, Delay);
		}
	}
}

void UZombieCoordinator::BroadcastFrenzy(const FVector& Location, float Radius, float Duration)
{
	Prune();

	const float RadiusSquared = FMath::Square(Radius);
	for (const TWeakObjectPtr<AZombieAIController>& Zombie : Zombies)
	{
		AZombieCharacter* Body = Zombie.IsValid() ? Cast<AZombieCharacter>(Zombie->GetPawn()) : nullptr;
		if (Body && Body->IsAlive() && FVector::DistSquared(Body->GetActorLocation(), Location) <= RadiusSquared)
		{
			Body->SetFrenzied(Duration);
		}
	}
}

void UZombieCoordinator::ReportStalkerDistance(float Distance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - StalkerReportTime > 0.5f || Distance < StalkerDistance)
	{
		StalkerDistance = Distance;
		StalkerReportTime = Now;
	}
}

float UZombieCoordinator::GetStalkerDistance() const
{
	return GetWorld()->GetTimeSeconds() - StalkerReportTime < 0.6f ? StalkerDistance : TNumericLimits<float>::Max();
}

int32 UZombieCoordinator::CountType(uint8 InType) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AZombieAIController>& Zombie : Zombies)
	{
		const AZombieCharacter* Body = Zombie.IsValid() ? Cast<AZombieCharacter>(Zombie->GetPawn()) : nullptr;
		Count += (Body && Body->IsAlive() && static_cast<uint8>(Body->GetZombieType()) == InType) ? 1 : 0;
	}
	return Count;
}
