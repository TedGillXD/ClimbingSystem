// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ClimbingSystemCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UENUM(BlueprintType)
enum ECharacterMovementMode {
	Walking = 0,
	Climbing = 1,
	Jumping = 2,		// 跳跃后正在滞空的情况
};

UCLASS(config=Game)
class AClimbingSystemCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Movement, meta = (AllowPrivateAccess = "true"))
	TEnumAsByte<ECharacterMovementMode> CharacterMovementMode;				// 表示当前的运动状态

private:	// Climbing System Components

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	UArrowComponent* DetectionArrowHead;		// 用来在攀爬的时候进行标记LineTrace起点的Arrow

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	UArrowComponent* DetectionArrowPelvis;		// 用来在攀爬的时候进行标记LineTrace起点的Arrow

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float WallDetectionLength;			// 墙壁的检测长度

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float WallDistance;			// 在攀爬过程中离墙的距离

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float ExitClimbingDetection;		// 当在攀爬状态下向下行走的过程中，不断的向下进行LineTrace，当在这个距离内检测到地面了，则脱出攀爬模式回到Walking

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	UAnimMontage* IdleToOnWallMontage;	// 从Walking到抓在墙上的Montage

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	UAnimMontage* MantleMontage;		// Mantle的时候用的Montage

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	float WallDistanceOffset;
	
public:
	AClimbingSystemCharacter();
	

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);
	
	/** Character jump */
	void CharacterJump();

	/** Character stop jumping */
	void CharacterStopJump();

private:
	/**
	 * 检测角色前方是否有一堵能爬的墙
	 * @return 前面的墙是否能爬
	 */
	bool ClimbWallDetection(FHitResult& PelvisHitResult, FHitResult& HeadHitResult) const;

	bool DetectShouldExitClimbing();
	void EnterClimbing(const FHitResult& HitResult);
	void EnterClimbingWithoutMontage(const FHitResult& HitResult);
	void ExitClimbing();

	// 计算当前检测到的面的向上的切线
	static FVector GetUpVectorOfCurrentVector(const FVector& DetectedNormal);

	// 计算当前检测到的面的向右的切线
	static FVector GetRightVectorOfCurrentVector(const FVector& DetectedNormal);
	
	// 检查目前是否满足Mantle的条件，返回是否能站到顶上以及目标位置
	bool CheckMantle(FVector& MantleTargetLocation) const;

	void Mantle(const FVector& TargetLocation);

protected:
	// APawn interface
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	// To add mapping context
	virtual void BeginPlay();

	virtual void Tick(float DeltaSeconds) override;

public:
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

