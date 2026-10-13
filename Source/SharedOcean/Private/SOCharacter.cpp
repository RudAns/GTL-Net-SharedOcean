#include "SOCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "OceanManager.h"
#include "SOShip.h"
#include "UObject/ConstructorHelpers.h"

ASOCharacter::ASOCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);
	bUseControllerRotationYaw = false;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshF(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimF(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (MeshF.Succeeded()) GetMesh()->SetSkeletalMesh(MeshF.Object);
	if (AnimF.Succeeded()) GetMesh()->SetAnimInstanceClass(AnimF.Class);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));

	UCharacterMovementComponent* M = GetCharacterMovement();
	M->bOrientRotationToMovement = true;
	M->RotationRate = FRotator(0, 600.f, 0);
	M->MaxWalkSpeed = 430.f;
	M->JumpZVelocity = 480.f;
	M->AirControl = 0.35f;
	M->bIgnoreBaseRotation = false;
	M->PerchRadiusThreshold = 10.f;
	M->MaxStepHeight = 40.f;
	M->SetWalkableFloorAngle(50.f);

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(GetRootComponent());
	Boom->TargetArmLength = 420.f;
	Boom->SocketOffset = FVector(0, 55.f, 75.f);
	Boom->bUsePawnControlRotation = true;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 12.f;
	Boom->bDoCollisionTest = true;
	Boom->ProbeSize = 10.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Boom);
	Camera->FieldOfView = 80.f;
}

void ASOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASOCharacter, HelmShip);
}

void ASOCharacter::TakeHelm(ASOShip* Ship)
{
	if (!HasAuthority() || !Ship || (Ship->Helmsman && Ship->Helmsman != this)) return;
	HelmShip = Ship;
	Ship->Helmsman = this;
	SetActorLocation(Ship->GetHelmStandLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	FRotator R = Ship->GetActorRotation();
	SetActorRotation(FRotator(0, R.Yaw, 0));
}

void ASOCharacter::LeaveHelm()
{
	if (!HasAuthority() || !HelmShip) return;
	if (HelmShip->Helmsman == this) HelmShip->Helmsman = nullptr;
	HelmShip = nullptr;
}

void ASOCharacter::RespawnOnShip()
{
	for (TActorIterator<ASOShip> It(GetWorld()); It; ++It)
	{
		const FVector P = It->GetActorTransform().TransformPosition(FVector(150.f, 0.f, 120.f));
		SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		return;
	}
}

void ASOCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocallyControlled())
	{
		const bool bHelm = IsAtHelm();
		Boom->TargetArmLength = FMath::FInterpTo(Boom->TargetArmLength, bHelm ? 2300.f : 420.f, DeltaSeconds, 3.f);
		const FVector WantOffset = bHelm ? FVector(0, 0, 380.f) : FVector(0, 55.f, 75.f);
		Boom->SocketOffset = FMath::VInterpTo(Boom->SocketOffset, WantOffset, DeltaSeconds, 3.f);
		// Level horizon by default; optionally inherit the deck's roll to feel the difference.
		float Roll = 0.f;
		if (bCameraFollowsRoll)
			if (const UPrimitiveComponent* Base = GetMovementBase())
				Roll = Base->GetComponentRotation().Roll;
		Camera->SetRelativeRotation(FRotator(0, 0, FMath::FInterpTo(Camera->GetRelativeRotation().Roll, Roll, DeltaSeconds, 6.f)));
	}
	if (HasAuthority())
	{
		// Overboard: no swimming in this demo, the sea returns you to the deck.
		if (AOceanManager* Ocean = AOceanManager::Get(this))
		{
			const float Water = Ocean->HeightCm(FVector2D(GetActorLocation()), 2);
			const bool bUnder = GetActorLocation().Z < Water + 20.f && !GetMovementBase();
			OverboardTimer = bUnder ? OverboardTimer + DeltaSeconds : 0.f;
			if (OverboardTimer > 1.2f || GetActorLocation().Z < -3000.f) { LeaveHelm(); RespawnOnShip(); OverboardTimer = 0.f; }
		}
		if (HelmShip)
		{
			// Keep the helmsman glued to the wheel while the ship moves.
			const FVector Want = HelmShip->GetHelmStandLocation();
			if (FVector::Dist(Want, GetActorLocation()) > 60.f) SetActorLocation(Want, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}
