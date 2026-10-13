// Scripted self-test for ASOPlayerController (-SOCapture=<dir> -SOCaptureRole=host|server|client).
// Roles:  host    single machine: walk, take the helm, sail, cycle every server and visual switch
//         server  stays up for a client: takes the helm and sails so the client sees a moving ship
//         client  joins 127.0.0.1: checks snapshots, clock sync, clock modes, lag, presentation modes

#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "OceanManager.h"
#include "SOCharacter.h"
#include "SOGameState.h"
#include "SOPlayerController.h"

namespace
{
	struct FStep { float At; TFunction<void(ASOPlayerController*)> Do; };
}

void ASOPlayerController::TickCapture(float Dt)
{
	static TArray<FStep> Script;
	static bool bBuilt = false;
	CaptureT += Dt;
	ASOShip* Ship = GetShip();

	// running samples for windows opened by the script
	if (Ship) Samples.Add(FMath::Abs(IsHost() ? Ship->GetDraftErrorCm() : Ship->GetDraftDeltaCm()));

	if (!bBuilt)
	{
		bBuilt = true;
		auto RelPos = [](ASOPlayerController* P) -> FVector
		{
			ASOShip* S = P->GetShip(); ASOCharacter* C = P->GetSailor();
			return (S && C) ? S->GetActorTransform().InverseTransformPosition(C->GetActorLocation()) : FVector::ZeroVector;
		};
		auto Window = [](ASOPlayerController* P) { P->Samples.Reset(); };
		auto Mean = [](ASOPlayerController* P) { float S = 0; for (float V : P->Samples) S += V; return P->Samples.Num() ? S / P->Samples.Num() : 0.f; };
		auto ToHelm = [](ASOPlayerController* P)
		{
			if (ASOShip* S = P->GetShip()) if (ASOCharacter* C = P->GetSailor())
				C->SetActorLocation(S->GetHelmStandLocation() + FVector(0, 0, 10), false, nullptr, ETeleportType::TeleportPhysics);
		};
		if (CaptureRole == TEXT("host"))
		{
			Script = {
				{ 2.5f, [](ASOPlayerController* P) { P->Shot(TEXT("00_intro")); } },
				{ 3.5f, [](ASOPlayerController* P) { P->Press(EKeys::Enter); } },
				{ 7.0f, [RelPos](ASOPlayerController* P) { P->Metrics.Add(TEXT("rel0"), (float)RelPos(P).X); P->Shot(TEXT("01_deck")); P->Hold(EKeys::W, true); } },
				{ 8.6f, [RelPos](ASOPlayerController* P) { P->Hold(EKeys::W, false); const float D = FMath::Abs((float)RelPos(P).X - P->Metrics[TEXT("rel0")]);
					P->Check(D > 120.f && P->GetSailor()->GetMovementBase() != nullptr, TEXT("deck_walk"), FString::Printf(TEXT("moved %.0f cm on deck, based=%d"), D, P->GetSailor()->GetMovementBase() != nullptr)); } },
				{ 8.8f, [](ASOPlayerController* P) { P->Metrics.Add(TEXT("look0"), (float)P->GetControlRotation().Yaw);
					for (int32 K = 0; K < 10; ++K) P->InputKey(FInputKeyParams(EKeys::MouseX, 4.0, 1.f / 60.f, 1, false)); } },
				{ 9.2f, [](ASOPlayerController* P) { const float D = FMath::Abs(FMath::FindDeltaAngleDegrees(P->Metrics[TEXT("look0")], (float)P->GetControlRotation().Yaw));
					P->Check(D > 5.f, TEXT("mouse_look"), FString::Printf(TEXT("view turned %.1f deg from injected mouse motion"), D)); } },
				{ 9.5f, [ToHelm](ASOPlayerController* P) { ToHelm(P); } },
				{ 10.0f, [](ASOPlayerController* P) { P->Press(EKeys::E); } },
				{ 11.0f, [](ASOPlayerController* P) { P->Check(P->GetSailor()->IsAtHelm(), TEXT("take_helm"), TEXT("")); P->Metrics.Add(TEXT("yaw0"), (float)P->GetShip()->GetActorRotation().Yaw); P->Hold(EKeys::W, true); } },
				{ 15.0f, [](ASOPlayerController* P) { P->Hold(EKeys::W, false); P->Hold(EKeys::D, true); } },
				{ 19.0f, [](ASOPlayerController* P) { P->Hold(EKeys::D, false);
					const float Kn = P->GetShip()->GetSpeedKnots(); const float Dy = FMath::Abs(FMath::FindDeltaAngleDegrees(P->Metrics[TEXT("yaw0")], (float)P->GetShip()->GetActorRotation().Yaw));
					P->Check(Kn > 1.5f, TEXT("sailing"), FString::Printf(TEXT("%.1f kn, sail %.2f"), Kn, P->GetShip()->Sail));
					P->Check(Dy > 8.f, TEXT("steering"), FString::Printf(TEXT("turned %.0f deg"), Dy)); } },
				{ 20.0f, [](ASOPlayerController* P) { P->Shot(TEXT("02_helm_sailing")); } },
				{ 21.0f, [](ASOPlayerController* P) { P->Press(EKeys::F1); } },
				{ 22.5f, [](ASOPlayerController* P) { P->Shot(TEXT("03_layer_gameplay")); P->Press(EKeys::F1); } },
				{ 24.0f, [](ASOPlayerController* P) { P->Shot(TEXT("04_layer_visual")); P->Press(EKeys::F1); } },
				{ 25.5f, [](ASOPlayerController* P) { P->Shot(TEXT("05_layer_tint")); P->Press(EKeys::F1); } },
				{ 26.5f, [](ASOPlayerController* P) { P->Press(EKeys::F2); } },
				{ 27.5f, [](ASOPlayerController* P) { P->Shot(TEXT("06_probes")); } },
				{ 28.0f, [](ASOPlayerController* P) { P->Press(EKeys::F5); } },	// Normal -> Storm
				{ 38.0f, [](ASOPlayerController* P) { const float Hs = AOceanManager::Get(P)->GetSignificantHeight();
					P->Check(Hs > 3.f, TEXT("storm_state"), FString::Printf(TEXT("Hs %.2f m"), Hs)); P->Shot(TEXT("07_storm")); P->Press(EKeys::F6); } },
				{ 39.5f, [](ASOPlayerController* P) { P->Shot(TEXT("08_storm_overlay")); P->Press(EKeys::F6); P->Press(EKeys::F9); } },
				{ 44.0f, [](ASOPlayerController* P) { const FRotator R = P->GetShip()->GetActorRotation();
					P->Check(FMath::Abs(R.Pitch) < 35.f && FMath::Abs(R.Roll) < 40.f, TEXT("planefit_stable"), FString::Printf(TEXT("pitch %.1f roll %.1f"), R.Pitch, R.Roll));
					P->Shot(TEXT("09_planefit")); P->Press(EKeys::F9); P->Press(EKeys::F10); P->Press(EKeys::F10); } },
				{ 49.0f, [](ASOPlayerController* P) { const ASOGameState* G = ASOGameState::Get(P);
					P->Check(G && G->Settings.PhysicsHz == 10, TEXT("physics_10hz"), TEXT("")); P->Shot(TEXT("10_physics_10hz")); P->Press(EKeys::F10); } },
				{ 50.0f, [](ASOPlayerController* P) { P->Press(EKeys::LeftBracket); P->Press(EKeys::LeftBracket); P->Press(EKeys::LeftBracket); } },
				{ 54.0f, [](ASOPlayerController* P) { const ASOGameState* G = ASOGameState::Get(P);
					P->Check(G && G->Settings.ProbeCount == 1, TEXT("probe_count"), FString::Printf(TEXT("%d probes"), G ? G->Settings.ProbeCount : -1));
					P->Shot(TEXT("11_one_probe")); P->Press(EKeys::RightBracket); P->Press(EKeys::RightBracket); P->Press(EKeys::RightBracket); P->Press(EKeys::F2); } },
				{ 55.0f, [](ASOPlayerController* P) { P->Press(EKeys::F5); } },	// Storm -> Calm
				{ 64.0f, [](ASOPlayerController* P) { P->Shot(TEXT("12_calm")); P->Press(EKeys::F5);
					P->Metrics.Add(TEXT("gpu_ms"), (float)AOceanManager::Get(P)->GetEvalMs()); P->Lab(ESOLab::VisualGPU); } },	// back to Normal, visual band to CPU
				{ 65.5f, [](ASOPlayerController* P) { const float CpuMs = (float)AOceanManager::Get(P)->GetEvalMs();
					P->Check(CpuMs > P->Metrics[TEXT("gpu_ms")], TEXT("visual_band_gpu"), FString::Printf(TEXT("ocean CPU time %.2f ms with the visual band on the GPU, %.2f ms on the CPU"), P->Metrics[TEXT("gpu_ms")], CpuMs));
					P->Shot(TEXT("12b_visual_cpu")); P->Lab(ESOLab::VisualGPU); } },
				{ 66.0f, [](ASOPlayerController* P) { P->Press(EKeys::Tab); } },
				{ 67.0f, [](ASOPlayerController* P) { P->Shot(TEXT("13_lab")); P->Press(EKeys::Tab); P->Press(EKeys::H); } },
				{ 68.5f, [](ASOPlayerController* P) { P->Shot(TEXT("14_help")); P->Press(EKeys::H); P->Press(EKeys::E); } },
				{ 70.0f, [](ASOPlayerController* P) { P->Check(!P->GetSailor()->IsAtHelm(), TEXT("leave_helm"), TEXT(""));
					const AOceanManager* Oc = AOceanManager::Get(P);
					P->Check(P->FrameMs < 40.f, TEXT("frame_time"), FString::Printf(TEXT("%.1f ms/frame, ocean CPU %.2f ms"), P->FrameMs, Oc ? Oc->GetEvalMs() : -1.0)); } },
				{ 71.0f, [](ASOPlayerController* P) { P->ConsoleCommand(TEXT("quit")); } },
			};
		}
		else if (CaptureRole == TEXT("server"))
		{
			Script = {
				{ 2.0f, [](ASOPlayerController* P) { P->Press(EKeys::Enter); } },
				{ 3.0f, [ToHelm](ASOPlayerController* P) { ToHelm(P); } },
				{ 3.5f, [](ASOPlayerController* P) { P->Press(EKeys::E); } },
				{ 4.5f, [](ASOPlayerController* P) { P->Hold(EKeys::W, true); } },
				{ 8.0f, [](ASOPlayerController* P) { P->Hold(EKeys::W, false); P->Hold(EKeys::A, true); } },
				{ 9.0f, [](ASOPlayerController* P) { P->Hold(EKeys::A, false); } },
				{ 110.0f, [](ASOPlayerController* P) { P->ConsoleCommand(TEXT("quit")); } },
			};
		}
		else	// client
		{
			Script = {
				{ 6.0f, [](ASOPlayerController* P) { P->Shot(TEXT("c00_intro")); P->Press(EKeys::Enter); } },
				{ 9.0f, [](ASOPlayerController* P) {
					ASOShip* S = P->GetShip();
					P->Check(S && S->GetNewestSnapshot() != nullptr, TEXT("snapshots"), FString::Printf(TEXT("%.1f kbit/s, %d bits each"), S ? S->GetSnapshotKbps() : 0.f, FSOShipState::LastSerializedBits));
					USONetClock* C = P->GetNetClock();
					P->Check(C->bHasSample && C->PongsReceived > 5, TEXT("clock_sync"), FString::Printf(TEXT("rtt %.0f ms (min %.0f), offset %.3f s, jitter %.1f ms"), C->LastRttMs, C->MinRttMs, C->SmoothedOffset, C->OffsetJitterMs)); } },
				{ 10.0f, [Window](ASOPlayerController* P) { P->Shot(TEXT("c01_synced")); Window(P); } },
				{ 14.0f, [Mean, Window](ASOPlayerController* P) { P->Metrics.Add(TEXT("synced"), Mean(P)); P->Press(EKeys::F3); Window(P); } },
				{ 18.0f, [Mean, Window](ASOPlayerController* P) { P->Metrics.Add(TEXT("engine"), Mean(P)); P->Shot(TEXT("c02_engine_clock")); P->Press(EKeys::F3); Window(P); } },
				{ 22.0f, [Mean](ASOPlayerController* P) { const float L = Mean(P), S = P->Metrics[TEXT("synced")];
					P->Check(L > S * 2.f + 10.f, TEXT("local_clock_breaks"), FString::Printf(TEXT("mean |draft delta vs server|: synced %.0f cm, engine %.0f cm, local %.0f cm, sea time off by %.1f s"), S, P->Metrics[TEXT("engine")], L, P->GetNetClock()->ModeErrorMs() * 0.001f));
					P->Shot(TEXT("c03_local_clock")); P->Press(EKeys::F3); } },
				{ 24.0f, [](ASOPlayerController* P) { P->Press(EKeys::F7); P->Press(EKeys::F7); } },	// 200 ms
				{ 30.0f, [Window](ASOPlayerController* P) { const float R = P->GetNetClock()->LastRttMs;
					P->Check(R > 140.f && R < 330.f, TEXT("net_lag"), FString::Printf(TEXT("rtt %.0f ms with 200 ms emulated; clock error %.1f ms"), R, P->GetNetClock()->ModeErrorMs())); Window(P); } },
				{ 34.0f, [Mean, Window](ASOPlayerController* P) { P->Metrics.Add(TEXT("p_interp"), Mean(P)); P->Shot(TEXT("c04_interp_lag")); P->Press(EKeys::F4); Window(P); } },
				{ 38.0f, [Mean, Window](ASOPlayerController* P) { P->Metrics.Add(TEXT("p_rewind"), Mean(P)); P->Shot(TEXT("c05_rewind")); P->Press(EKeys::F4); Window(P); } },
				{ 42.0f, [Mean](ASOPlayerController* P) { const float Rc = Mean(P);
					P->Check(Rc <= P->Metrics[TEXT("p_interp")] + 5.f, TEXT("reconstruct"), FString::Printf(TEXT("mean |draft delta vs server|: interp %.0f cm, rewind %.0f cm, reconstruct %.0f cm"), P->Metrics[TEXT("p_interp")], P->Metrics[TEXT("p_rewind")], Rc));
					P->Shot(TEXT("c06_reconstruct")); P->Press(EKeys::F8); } },
				{ 43.5f, [](ASOPlayerController* P) { P->Shot(TEXT("c07_ghost")); P->Press(EKeys::F8); P->Press(EKeys::F4); P->Press(EKeys::F7); P->Press(EKeys::F7); } },
				{ 46.0f, [RelPos](ASOPlayerController* P) { P->Metrics.Add(TEXT("rel0"), (float)RelPos(P).X); P->Hold(EKeys::W, true); } },
				{ 47.5f, [RelPos](ASOPlayerController* P) { P->Hold(EKeys::W, false); const float D = FMath::Abs((float)RelPos(P).X - P->Metrics[TEXT("rel0")]);
					P->Check(D > 100.f, TEXT("client_deck_walk"), FString::Printf(TEXT("moved %.0f cm relative to the ship"), D)); } },
				{ 49.0f, [](ASOPlayerController* P) { P->Shot(TEXT("c08_deck")); } },
				{ 50.0f, [](ASOPlayerController* P) { P->ConsoleCommand(TEXT("quit")); } },
			};
		}
	}
	while (CaptureStep < Script.Num() && CaptureT >= Script[CaptureStep].At)
	{
		Script[CaptureStep].Do(this);
		++CaptureStep;
	}
}
