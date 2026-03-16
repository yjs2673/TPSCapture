// Copyright Epic Games, Inc. All Rights Reserved.

#include "TSPCaptureCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// ATSPCaptureCharacter

ATSPCaptureCharacter::ATSPCaptureCharacter()
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
}

//////////////////////////////////////////////////////////////////////////
// Input

void ATSPCaptureCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void ATSPCaptureCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATSPCaptureCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATSPCaptureCharacter::Look);

		// Punching
		EnhancedInputComponent->BindAction(PunchAction, ETriggerEvent::Started, this, &ATSPCaptureCharacter::Punch);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ATSPCaptureCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

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
}

void ATSPCaptureCharacter::Look(const FInputActionValue& Value)
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

void ATSPCaptureCharacter::Punch(const FInputActionValue& Value)
{
	if (bIsPunching)
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Already Punching"));
		return;
	}

	if (!PunchMontage || !GetMesh() || !GetMesh()->GetAnimInstance())
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("PunchMontage or AnimInstance is not valid"));
		return;
	}

	bIsPunching = true;
	UE_LOG(LogTemplateCharacter, Warning, TEXT("Punch Start"));

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	const float Duration = AnimInstance->Montage_Play(PunchMontage);

	if (Duration > 0.f)
	{
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ATSPCaptureCharacter::OnPunchMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, PunchMontage);
	}
	else
	{
		EndPunch();
	}
}

void ATSPCaptureCharacter::EndPunch()
{
	bIsPunching = false;
	UE_LOG(LogTemplateCharacter, Warning, TEXT("Punch End"));
}

void ATSPCaptureCharacter::PerformPunchHit()
{
	if (!GetWorld())
	{
		return;
	}

	const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const FVector End = Start + (GetActorForwardVector() * PunchRange);

	FCollisionShape Sphere = FCollisionShape::MakeSphere(PunchRadius);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult HitResult;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		HitResult,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		Sphere,
		QueryParams
	);

	const FColor DebugColor = bHit ? FColor::Red : FColor::Green;
	DrawDebugCapsule(
		GetWorld(),
		(Start + End) * 0.5f,
		PunchRange * 0.5f,
		PunchRadius,
		FRotationMatrix::MakeFromX(End - Start).ToQuat(),
		DebugColor,
		false,
		1.5f
	);

	if (bHit && HitResult.GetActor())
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Hit: %s"), *HitResult.GetActor()->GetName());

		UGameplayStatics::ApplyDamage(
			HitResult.GetActor(),
			PunchDamage,
			GetController(),
			this,
			UDamageType::StaticClass()
		);
	}
}

void ATSPCaptureCharacter::TriggerPunchHit()
{
	PerformPunchHit();
}

void ATSPCaptureCharacter::OnPunchMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != PunchMontage)
	{
		return;
	}

	UE_LOG(LogTemplateCharacter, Warning, TEXT("Punch Montage Ended (Interrupted: %s)"), bInterrupted ? TEXT("true") : TEXT("false"));

	EndPunch();
}