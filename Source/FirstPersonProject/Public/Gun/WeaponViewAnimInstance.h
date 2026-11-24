#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Data/WeaponDataAsset.h"
#include "WeaponViewAnimInstance.generated.h"

/** One moving part of the view mesh while it plays (offset already in the bone's local space) */
struct FWeaponPartPose
{
	int32 BoneIndex = INDEX_NONE;
	FVector LocalOffset = FVector::ZeroVector;
	float Alpha = 0.f;
};

/** Evaluates the reference pose with the moving parts pushed back by their current amount */
struct FWeaponViewAnimInstanceProxy : public FAnimInstanceProxy
{
	FWeaponViewAnimInstanceProxy() = default;
	explicit FWeaponViewAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	TArray<FWeaponPartPose> Parts;
};

/**
 *  Anim instance for weapon models without their own Anim Blueprint.
 *  Kicks the moving parts back on every shot (pistol slide, shotgun pump) and holds a slide open on an empty magazine.
 */
UCLASS(Transient, NotBlueprintable)
class FIRSTPERSONPROJECT_API UWeaponViewAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	/** Sets the parts to move. Call after the skeletal mesh is set. */
	void SetupParts(const TArray<FWeaponPartKick>& InKicks);

	/** Starts every part's movement for one shot */
	void KickParts();

	/** Holds the parts that lock open on an empty magazine (pistol slide) */
	void SetEmpty(bool bNewEmpty) { bEmpty = bNewEmpty; }

	const TArray<FWeaponPartPose>& GetPartPoses() const { return PartPoses; }

protected:

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:

	/** Movement amount of a part TimeSinceKick seconds after the shot */
	static float EvaluateKick(const FWeaponPartKick& Kick, float TimeSinceKick);

	TArray<FWeaponPartKick> Kicks;
	TArray<FWeaponPartPose> PartPoses;
	float TimeSinceKick = TNumericLimits<float>::Max();
	bool bEmpty = false;
};
