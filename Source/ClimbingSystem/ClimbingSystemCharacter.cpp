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
#include "Kismet/KismetMathLibrary.h"

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
	
	WallDetectionLength = 50.f;
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
	if(CharacterMovementMode == Walking) {
		if (Controller != nullptr)
		{
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
	} else if(CharacterMovementMode == Climbing) {
		// TODO: 1. 完成爬墙的上下左右
		// 做一次向前的LineTrace，得到当前Actor前方的点的Normal，使用这个Normal的切线向量作为移动的方向，上下左右都同理
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);
		bool Result = GetWorld()->LineTraceSingleByChannel(HitResult, GetActorLocation(), GetActorLocation() + GetActorForwardVector() * (WallDistance + 10.f), ECC_Visibility, Params);

		AddMovementInput(GetUpVectorOfCurrentVector(HitResult.ImpactNormal), MovementVector.Y);	// 朝着检测到的面的上切线方向移动
		AddMovementInput(GetRightVectorOfCurrentVector(HitResult.ImpactNormal), MovementVector.X * -1);	// 朝着检测到的面的右切线方向移动
		
		// 2. 在向下的时候进行触地检测
		if(MovementVector.Y < 0) {
			DetectShouldExitClimbing();
		} else if(MovementVector.Y > 0) {
			// TODO: 做Mantle的检测，看看能不能爬到顶上去
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
	if(CharacterMovementMode == ECharacterMovementMode::Walking) {
		// 1. 判断前面是不是墙
		FHitResult PelvisHitResult, HeadHitResult;
		if(ClimbWallDetection(PelvisHitResult, HeadHitResult)) {
			// 2. 如果是墙，则进入攀爬状态
			GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			GetCharacterMovement()->bOrientRotationToMovement = false;
			FQuat Rotation = FQuat::FindBetweenNormals(GetCapsuleComponent()->GetForwardVector(), HeadHitResult.Normal);
			
			FLatentActionInfo LatentInfo;
			LatentInfo.CallbackTarget = this;
			UKismetSystemLibrary::MoveComponentTo(RootComponent, HeadHitResult.Location + HeadHitResult.Normal * WallDistance, Rotation.Rotator(), false, false, 0.4, false, EMoveComponentAction::Type::Move, LatentInfo);
			this->CharacterMovementMode = Climbing;
			return;
		}

		// 3. 如果不是，则跳跃
		this->Jump();
		this->CharacterMovementMode = Jumping;
	} else if(CharacterMovementMode == Climbing) {
		// 1. 检测通过这次条约已经到达了顶端
		// 2. 如果不是，往上跳一大格
		// 3. 如果是，则MoveComponent到具体的位置，然后播放到达顶端的动画
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
	DrawDebugLineTraceSingle(GetWorld(), PelvisStart, PelvisEnd, EDrawDebugTrace::ForDuration, Result, PelvisHitResult, FColor::Red, FColor::Green, 5.f);
	if(!Result) { return false; }
	
	FVector HeadStart = DetectionArrowHead->GetComponentLocation();
	FVector HeadEnd = DetectionArrowHead->GetForwardVector() * WallDetectionLength + HeadStart;
	Result = GetWorld()->LineTraceSingleByChannel(HeadHitResult, HeadStart, HeadEnd, ECC_Visibility, Params);
	DrawDebugLineTraceSingle(GetWorld(), HeadStart, HeadEnd, EDrawDebugTrace::ForDuration, Result, HeadHitResult, FColor::Red, FColor::Green, 5.f);
	if(!Result) { return false; }
	
	return true;
}

void AClimbingSystemCharacter::DetectShouldExitClimbing() {
	// 向下做检测
	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	bool Result = GetWorld()->LineTraceSingleByChannel(HitResult, GetActorLocation(), GetActorLocation() + (GetActorUpVector() * -1.f * ExitClimbingDetection), ECC_Visibility, Params);
	DrawDebugLineTraceSingle(GetWorld(), GetActorLocation(), GetActorLocation() + (GetActorUpVector() * -1.f * ExitClimbingDetection), EDrawDebugTrace::ForOneFrame, Result, HitResult, FColor::Black, FColor::Black, 5.f);
	if(Result) {
		// 退出攀爬模式
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		GetCharacterMovement()->bOrientRotationToMovement = true;
		// 调整Actor的Rotation使其垂直于XY平面(地面)
		FRotator CurrentRotation = this->GetActorRotation();
		this->SetActorRotation(FRotator(CurrentRotation.Pitch, CurrentRotation.Yaw, 0));
		this->CharacterMovementMode = Walking;
	}
}

FVector AClimbingSystemCharacter::GetUpVectorOfCurrentVector(const FVector& DetectedNormal) {
	FVector RightVector = FVector::CrossProduct(FVector::UpVector, DetectedNormal);
	return FVector::CrossProduct(DetectedNormal, RightVector);
}

FVector AClimbingSystemCharacter::GetRightVectorOfCurrentVector(const FVector& DetectedNormal) {
	return FVector::CrossProduct(FVector::UpVector, DetectedNormal);
}