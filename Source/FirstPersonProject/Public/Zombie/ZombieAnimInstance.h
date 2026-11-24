#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ZombieAnimInstance.generated.h"

class UAnimSequence;

/** What the zombie body is doing this frame, copied to the animation worker thread */
struct FZombiePoseState
{
	TObjectPtr<const UAnimSequence> Idle;
	TObjectPtr<const UAnimSequence> Move;
	TObjectPtr<const UAnimSequence> Action;
	double IdleTime = 0.0;
	double MoveTime = 0.0;
	double ActionTime = 0.0;
	float MoveAlpha = 0.f;
	float ActionAlpha = 0.f;
};

/** Samples and blends the zombie sequences: idle and move by speed, a one shot action (attack, scream) on top */
struct FZombieAnimInstanceProxy : public FAnimInstanceProxy
{
	FZombieAnimInstanceProxy() = default;
	explicit FZombieAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:

	/** Samples Sequence at Time with the root kept in place (the capsule moves the zombie, not the animation) */
	void Sample(const UAnimSequence* Sequence, double Time, FPoseContext& Output) const;

	FZombiePoseState State;
};

/**
 *  Native animation for zombies, so they need no Anim Blueprint.
 *  Blends idle and walk (or run) by ground speed, matching the play rate to the speed so the feet do not slide,
 *  and plays one shot actions (attacks, screams, hit reactions) over the whole body with short blends.
 */
UCLASS(Transient, NotBlueprintable)
class FIRSTPERSONPROJECT_API UZombieAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	void SetLocomotion(UAnimSequence* InIdle, UAnimSequence* InMove, float InMoveAnimSpeed);

	/**
	 *  Plays a one shot over the locomotion, from StartTime to EndTime of the sequence (EndTime <= 0: its end).
	 *  Returns how long it plays in seconds at that rate.
	 */
	float PlayAction(UAnimSequence* Sequence, float PlayRate = 1.f, float BlendIn = 0.15f, float BlendOut = 0.25f, float StartTime = 0.f, float EndTime = -1.f);

	void StopAction(float BlendOut = 0.2f);

	bool IsPlayingAction() const { return Action != nullptr && !bActionEnding; }

	const FZombiePoseState& GetPoseState() const { return PoseState; }

protected:

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:

	UPROPERTY()
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Move;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Action;

	/** Ground speed at which the move animation plays at rate 1 */
	float MoveAnimSpeed = 100.f;

	float ActionRate = 1.f;
	double ActionEndTime = 0.0;
	float ActionBlendIn = 0.15f;
	float ActionBlendOut = 0.25f;
	bool bActionEnding = false;

	FZombiePoseState PoseState;
};
