// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClimbingSystemCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "DrawDebugHelpers.h"
#include "KismetTraceUtils.h"
#include "Components/ArrowComponent.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// AClimbingSystemCharacter

AClimbingSystemCharacter::AClimbingSystemCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
	
	DetectionArrowHead = CreateDefaultSubobject<UArrowComponent>(TEXT("DetectionArrowHead"));
	DetectionArrowHead->SetupAttachment(GetMesh());

	DetectionArrowPelvis = CreateDefaultSubobject<UArrowComponent>(TEXT("DetectionArrowPelvis"));
	DetectionArrowPelvis->SetupAttachment(GetMesh());
	
	WallDetectionLength = 75.f;
	WallDistanceOffset = 3.f;
	WallDistance = GetCapsuleComponent()->GetScaledCapsuleRadius();
	ExitClimbingDetection = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 50.f;
}

void AClimbingSystemCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AClimbingSystemCharacter::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);

	if(GetCharacterMovement()->IsFalling()) {
		if(FHitResult PelvisHitResult, HeadHitResult; ClimbWallDetection(PelvisHitResult, HeadHitResult)) {
			EnterClimbingWithoutMontage(PelvisHitResult);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AClimbingSystemCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AClimbingSystemCharacter::CharacterJump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AClimbingSystemCharacter::CharacterStopJump);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AClimbingSystemCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AClimbingSystemCharacter::Look);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AClimbingSystemCharacter::Move(const FInputActionValue& Value)
{
    // input is a Vector2D
    FVector2D MovementVector = Value.Get<FVector2D>();
    GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Red, MovementVector.ToString());

    if (CharacterMovementMode == Walking || CharacterMovementMode == Jumping) {
        if (Controller != nullptr) {
            // find out which way is forward
            const FRotator Rotation = Controller->GetControlRotation();
            const FRotator YawRotation(0, Rotation.Yaw, 0);

            // get forward vector
            const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

            // get right vector 
            const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

            // add movement 
            AddMovementInput(ForwardDirection, MovementVector.Y);
            AddMovementInput(RightDirection, MovementVector.X);
        }
    } else if (CharacterMovementMode == Climbing) {
        // 1. 爬墙的上下左右
        FHitResult HitResult;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);

        // 使用线追踪获取墙面法线
        if (GetWorld()->LineTraceSingleByChannel(HitResult, GetActorLocation(), GetActorLocation() + GetActorForwardVector() * (WallDistance + 50.f), ECC_Visibility, Params)) {
            FVector WallNormal = HitResult.ImpactNormal;
            FVector RightDirection = GetRightVectorOfCurrentVector(WallNormal);
            FVector UpDirection = GetUpVectorOfCurrentVector(WallNormal);
        	
            AddMovementInput(-RightDirection, MovementVector.X);
        	AddMovementInput(UpDirection, MovementVector.Y);

            // 调整角色位置和旋转，使其保持靠近墙面
			FVector TargetLocation = HitResult.ImpactPoint + WallNormal * (WallDistance + WallDistanceOffset);
			SetActorLocation(FMath::VInterpTo(GetActorLocation(), TargetLocation, GetWorld()->GetDeltaSeconds(), 5.f));
        	const FRotator DesiredRotation = FRotationMatrix::MakeFromX(-HitResult.Normal).Rotator();
        	SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredRotation, GetWorld()->GetDeltaSeconds(), 5.f));
        }

    	// 2. 进行站立检测
    	if(DetectShouldExitClimbing()) {
    		return;
    	}

    	// 3. 向上爬的时候检测现在是否已经能爬上去了
    	if (MovementVector.Y > 0) {
    		// 检查是否能站到顶上
    		FVector MantleTargetLocation;
    		if (CheckMantle(MantleTargetLocation)) {
    			// 爬到顶上能站的地方
    			GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Red, "Can Mantle!");
    			Mantle(MantleTargetLocation);
    			return;
		    }
    	}
    }
}

void AClimbingSystemCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AClimbingSystemCharacter::CharacterJump() {
	// 判断当前的状态
	if (CharacterMovementMode == ECharacterMovementMode::Walking) {
		// 1. 判断前面是不是墙
		FHitResult PelvisHitResult, HeadHitResult;
		if (ClimbWallDetection(PelvisHitResult, HeadHitResult)) {
			// 2. 如果是墙，则进入攀爬状态
			EnterClimbing(HeadHitResult);
			return;
		}

		// 3. 如果不是墙，则执行跳跃
		this->Jump();
		this->CharacterMovementMode = Jumping;
	} else if (CharacterMovementMode == Climbing) {
		// 在攀爬状态下处理进一步的跳跃逻辑
		// 1. 检测通过这次跳跃是否到达了顶端
		// 2. 如果不是，往上跳一大格
		// 3. 如果是，则MoveComponent到具体的位置，然后播放到达顶端的动画
		// 这里你需要实现具体的逻辑来判断是否到达顶端并执行相应的动作
	}
}

void AClimbingSystemCharacter::CharacterStopJump() {
	if(CharacterMovementMode == Jumping) {
		this->StopJumping();
		this->CharacterMovementMode = Walking;
	}
}

bool AClimbingSystemCharacter::ClimbWallDetection(FHitResult& PelvisHitResult, FHitResult& HeadHitResult) const {
	FVector PelvisStart = DetectionArrowPelvis->GetComponentLocation();
	FVector PelvisEnd = DetectionArrowPelvis->GetForwardVector() * WallDetectionLength + PelvisStart;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	bool Result = GetWorld()->LineTraceSingleByChannel(PelvisHitResult, PelvisStart, PelvisEnd, ECC_Visibility, Params);
	if(!Result) { return false; }
	
	FVector HeadStart = DetectionArrowHead->GetComponentLocation();
	FVector HeadEnd = DetectionArrowHead->GetForwardVector() * WallDetectionLength + HeadStart;
	Result = GetWorld()->LineTraceSingleByChannel(HeadHitResult, HeadStart, HeadEnd, ECC_Visibility, Params);
	if(!Result) { return false; }
	
	return true;
}

bool AClimbingSystemCharacter::DetectShouldExitClimbing() {
	// 向下做检测
	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	bool Result = GetWorld()->LineTraceSingleByChannel(HitResult, GetActorLocation(), GetActorLocation() + (GetActorUpVector() * -1.f * ExitClimbingDetection), ECC_Visibility, Params);
	if(Result) {
		ExitClimbing();
		return true;
	}

	// 检查角色与墙面的夹角
	double DotValue = FVector::DotProduct(GetActorUpVector(), FVector::UpVector);
	double CosineToAngle = FMath::Acos(DotValue);
	if(double AngleInDegrees = FMath::RadiansToDegrees(CosineToAngle); AngleInDegrees >= 30.0) {
		ExitClimbing();
		return true;
	}

	return false;
}

void AClimbingSystemCharacter::EnterClimbing(const FHitResult& HitResult) {
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	GetCharacterMovement()->bOrientRotationToMovement = false;
	
	const FRotator DesiredRotation = FRotationMatrix::MakeFromX(-HitResult.Normal).Rotator();

	float PlayTime = GetMesh()->GetAnimInstance()->Montage_Play(IdleToOnWallMontage);	// 播放蒙太奇
	
	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	UKismetSystemLibrary::MoveComponentTo(
		RootComponent, 
		HitResult.Location + HitResult.Normal * (WallDistance + WallDistanceOffset), 
		DesiredRotation, 
		true, false, PlayTime, false, 
		EMoveComponentAction::Type::Move, 
		LatentInfo
	);

	// 调整飞行的速度为攀爬的速度
	GetCharacterMovement()->MaxFlySpeed = 100.f;
	GetCharacterMovement()->BrakingDecelerationFlying = 2048.f;
	this->CharacterMovementMode = Climbing;
}

void AClimbingSystemCharacter::EnterClimbingWithoutMontage(const FHitResult& HitResult) {
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	GetCharacterMovement()->bOrientRotationToMovement = false;

	const FRotator DesiredRotation = FRotationMatrix::MakeFromX(-HitResult.Normal).Rotator();
	
	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	UKismetSystemLibrary::MoveComponentTo(
		RootComponent, 
		HitResult.Location + HitResult.Normal * WallDistance, 
		DesiredRotation, 
		true, false, 0.3, false, 
		EMoveComponentAction::Type::Move, 
		LatentInfo
	);

	// 调整飞行的速度为攀爬的速度
	GetCharacterMovement()->MaxFlySpeed = 100.f;
	GetCharacterMovement()->BrakingDecelerationFlying = 2048.f;
	this->CharacterMovementMode = Climbing;
}

void AClimbingSystemCharacter::ExitClimbing() {
	// 退出攀爬模式
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	// 调整Actor的Rotation使其垂直于XY平面(地面)
	FRotator CurrentRotation = this->GetActorRotation();
	this->SetActorRotation(FRotator(CurrentRotation.Pitch, CurrentRotation.Yaw, 0));
	GetCharacterMovement()->MaxFlySpeed = 600.f;
	GetCharacterMovement()->BrakingDecelerationFlying = 0.f;
	this->CharacterMovementMode = Walking;

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
}

FVector AClimbingSystemCharacter::GetUpVectorOfCurrentVector(const FVector& DetectedNormal) {
	FVector RightVector = FVector::CrossProduct(FVector::UpVector, DetectedNormal);
	return FVector::CrossProduct(DetectedNormal, RightVector);
}

FVector AClimbingSystemCharacter::GetRightVectorOfCurrentVector(const FVector& DetectedNormal) {
	return FVector::CrossProduct(FVector::UpVector, DetectedNormal);
}

bool AClimbingSystemCharacter::CheckMantle(FVector& MantleTargetLocation) const {
	const FVector ActorForward = GetActorForwardVector();
	FVector TrueForwardVector = ActorForward.GetSafeNormal2D();

	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	bool Result = GetWorld()->LineTraceSingleByChannel(HitResult, GetActorLocation(), GetActorLocation() + TrueForwardVector * WallDetectionLength, ECC_Visibility, Params);
	if(!Result) {
		return false;
	}

	FVector Start = HitResult.ImpactPoint + HitResult.ImpactNormal * -50.f + FVector::UpVector * GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector End = Start + FVector::DownVector * GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f;
	FHitResult MantleHitResult;
	Result = GetWorld()->LineTraceSingleByChannel(MantleHitResult, Start, End, ECC_Visibility, Params);
	if(!Result) {
		return false;
	}
	
	// 检测到一个位置了，检查这个位置是否能走
	bool Ret = GetCharacterMovement()->IsWalkable(MantleHitResult);
	MantleTargetLocation = MantleHitResult.ImpactPoint;
	return Ret;
}

void AClimbingSystemCharacter::Mantle(const FVector& TargetLocation) {
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);

	const float PlayTime = GetMesh()->GetAnimInstance()->Montage_Play(MantleMontage);	// 播放蒙太奇
	this->CameraBoom->bDoCollisionTest = false;
	
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([this, TargetLocation, PlayTime]() {
		// 将组件移动到指定的位置
		FLatentActionInfo LatentInfo;
		LatentInfo.CallbackTarget = this;
		UKismetSystemLibrary::MoveComponentTo(
			RootComponent, 
			GetActorLocation() + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 
			GetActorRotation(), 
			false, false, 0.1, false, 
			EMoveComponentAction::Type::Move, 
			LatentInfo
		);
		
		UKismetSystemLibrary::MoveComponentTo(
			RootComponent, 
			TargetLocation + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 
			GetActorRotation(), 
			false, false, 0.1, false, 
			EMoveComponentAction::Type::Move, 
			LatentInfo
		);
		// this->SetActorLocation(TargetLocation + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		ExitClimbing();

		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([this]() {
			this->CameraBoom->bDoCollisionTest = true;
		}), 0.3f, false);
	}), PlayTime, false);
}