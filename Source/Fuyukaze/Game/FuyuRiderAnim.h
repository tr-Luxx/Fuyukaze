#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "FuyuRiderAnim.generated.h"

/**
 *  Works out the riding pose. The project has no riding animation, so the pose is made here: starting from the
 *  skeleton's reference pose, the thighs are swung out and forward round the horse's barrel, the shins hang down
 *  its sides, and the arms come forward as if to the reins.
 */
USTRUCT()
struct FFuyuRiderProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FFuyuRiderProxy() = default;
	explicit FFuyuRiderProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual bool Evaluate(FPoseContext& Output) override;
};

/** Animation for the samurai's stand-in body while she is on horseback. It has no graph: the proxy above poses it */
UCLASS(Transient, NotBlueprintable)
class UFuyuRiderAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FFuyuRiderProxy(this); }
};
