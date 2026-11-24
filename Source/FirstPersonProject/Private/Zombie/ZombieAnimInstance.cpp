#include "Zombie/ZombieAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "GameFramework/Pawn.h"

namespace ZombieAnim
{
	/** Speed under which the zombie counts as standing */
	constexpr float StandSpeed = 8.f;
	/** Speed over which only the move animation plays */
	constexpr float FullMoveSpeed = 60.f;
	constexpr float MoveBlendSpeed = 8.f;
	constexpr float MinMoveRate = 0.6f;
	constexpr float MaxMoveRate = 2.4f;
}

void FZombieAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	if (const UZombieAnimInstance* Zombie = Cast<UZombieAnimInstance>(InAnimInstance))
	{
		State = Zombie->GetPoseState();
	}
}

void FZombieAnimInstanceProxy::Sample(const UAnimSequence* Sequence, double Time, FPoseContext& Output) const
{
	FAnimationPoseData PoseData(Output);
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time, false));

	// In place: the root stays at the reference pose
	const FCompactPoseBoneIndex RootIndex(0);
	if (Output.Pose.IsValidIndex(RootIndex))
	{
		Output.Pose[RootIndex] = Output.Pose.GetRefPose(RootIndex);
	}
}

bool FZombieAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (!State.Idle)
	{
		Output.ResetToRefPose();
		return true;
	}

	Sample(State.Idle, State.IdleTime, Output);

	if (State.Move && State.MoveAlpha > KINDA_SMALL_NUMBER)
	{
		FPoseContext MovePose(this);
		Sample(State.Move, State.MoveTime, MovePose);

		FPoseContext Blended(this);
		FAnimationPoseData BlendedData(Blended);
		FAnimationRuntime::BlendTwoPosesTogether(FAnimationPoseData(MovePose), FAnimationPoseData(Output), State.MoveAlpha, BlendedData);
		Output = Blended;
	}

	if (State.Action && State.ActionAlpha > KINDA_SMALL_NUMBER)
	{
		FPoseContext ActionPose(this);
		Sample(State.Action, State.ActionTime, ActionPose);

		FPoseContext Blended(this);
		FAnimationPoseData BlendedData(Blended);
		FAnimationRuntime::BlendTwoPosesTogether(FAnimationPoseData(ActionPose), FAnimationPoseData(Output), State.ActionAlpha, BlendedData);
		Output = Blended;
	}

	return true;
}

void UZombieAnimInstance::SetLocomotion(UAnimSequence* InIdle, UAnimSequence* InMove, float InMoveAnimSpeed)
{
	Idle = InIdle;
	Move = InMove;
	MoveAnimSpeed = FMath::Max(InMoveAnimSpeed, 1.f);

	// Every zombie starts at another point of its cycles so a crowd does not move in step
	PoseState.IdleTime = Idle ? FMath::FRandRange(0.f, Idle->GetPlayLength()) : 0.0;
	PoseState.MoveTime = Move ? FMath::FRandRange(0.f, Move->GetPlayLength()) : 0.0;
}

float UZombieAnimInstance::PlayAction(UAnimSequence* Sequence, float PlayRate, float BlendIn, float BlendOut, float StartTime, float EndTime)
{
	if (!Sequence)
	{
		return 0.f;
	}

	Action = Sequence;
	ActionRate = FMath::Max(PlayRate, 0.05f);
	ActionBlendIn = FMath::Max(BlendIn, 0.01f);
	ActionBlendOut = FMath::Max(BlendOut, 0.01f);
	bActionEnding = false;

	const double Length = Sequence->GetPlayLength();
	PoseState.ActionTime = FMath::Clamp(static_cast<double>(StartTime), 0.0, Length);
	ActionEndTime = EndTime > 0.f ? FMath::Clamp(static_cast<double>(EndTime), PoseState.ActionTime, Length) : Length;
	return (ActionEndTime - PoseState.ActionTime) / ActionRate;
}

void UZombieAnimInstance::StopAction(float BlendOut)
{
	if (Action)
	{
		ActionBlendOut = FMath::Max(BlendOut, 0.01f);
		bActionEnding = true;
	}
}

void UZombieAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APawn* Pawn = TryGetPawnOwner();
	const float Speed = Pawn ? Pawn->GetVelocity().Size2D() : 0.f;

	// Locomotion
	const float TargetMoveAlpha = FMath::GetMappedRangeValueClamped(
		FVector2f(ZombieAnim::StandSpeed, ZombieAnim::FullMoveSpeed), FVector2f(0.f, 1.f), Speed);
	PoseState.MoveAlpha = FMath::FInterpTo(PoseState.MoveAlpha, TargetMoveAlpha, DeltaSeconds, ZombieAnim::MoveBlendSpeed);

	if (Idle)
	{
		PoseState.IdleTime = FMath::Fmod(PoseState.IdleTime + DeltaSeconds, static_cast<double>(FMath::Max(Idle->GetPlayLength(), 0.01f)));
	}

	if (Move)
	{
		const float Rate = FMath::Clamp(Speed / MoveAnimSpeed, ZombieAnim::MinMoveRate, ZombieAnim::MaxMoveRate);
		PoseState.MoveTime = FMath::Fmod(PoseState.MoveTime + DeltaSeconds * Rate, static_cast<double>(FMath::Max(Move->GetPlayLength(), 0.01f)));
	}

	// One shot action
	if (Action)
	{
		const double Length = ActionEndTime;
		PoseState.ActionTime = FMath::Min(PoseState.ActionTime + DeltaSeconds * ActionRate, Length);

		const double TimeLeft = (Length - PoseState.ActionTime) / ActionRate;
		if (TimeLeft <= ActionBlendOut)
		{
			bActionEnding = true;
		}

		if (bActionEnding)
		{
			PoseState.ActionAlpha = FMath::Max(0.f, PoseState.ActionAlpha - DeltaSeconds / ActionBlendOut);
			if (PoseState.ActionAlpha <= 0.f)
			{
				Action = nullptr;
			}
		}
		else
		{
			PoseState.ActionAlpha = FMath::Min(1.f, PoseState.ActionAlpha + DeltaSeconds / ActionBlendIn);
		}
	}

	PoseState.Idle = Idle;
	PoseState.Move = Move;
	PoseState.Action = Action;
}

FAnimInstanceProxy* UZombieAnimInstance::CreateAnimInstanceProxy()
{
	return new FZombieAnimInstanceProxy(this);
}

void UZombieAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
