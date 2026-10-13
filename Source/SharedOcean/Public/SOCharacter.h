#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SOCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ASOShip;

/**
 * A sailor (UE5 mannequin, normal proportions). Walks on the moving deck with CharacterMovement's based
 * movement (report ch.3.5 / 4.5); takes the helm with E. The camera stays level while the deck rolls,
 * unless the lab switch "camera follows roll" is on (report ch.6.5).
 */
UCLASS()
class SHAREDOCEAN_API ASOCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ASOCharacter();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated) TObjectPtr<ASOShip> HelmShip;
	bool IsAtHelm() const { return HelmShip != nullptr; }

	USpringArmComponent* GetBoom() const { return Boom; }
	UCameraComponent* GetCamera() const { return Camera; }
	bool bCameraFollowsRoll = false;

	// server
	void TakeHelm(ASOShip* Ship);
	void LeaveHelm();
	void RespawnOnShip();

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	float OverboardTimer = 0.f;
};
