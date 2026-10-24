/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_phase2.h"

#include <base/log.h>

#include <engine/shared/config.h>

#include <game/gamecore.h>

#include <algorithm>

namespace AmfImpact
{
	static bool HasSafePlatformContact(const SImpactGameplayFrame &Frame)
	{
		// One collision/support observation can be a transient edge contact while a
		// frozen Tee is still moving through the problem. It is therefore evidence,
		// not yet a positive autonomous recovery fact. Deep freeze also cannot be
		// cleared by ordinary map support.
		return !Frame.m_DeepFrozen && Frame.m_SafeCollisionPositionKnown && Frame.m_SafeCollisionPosition &&
			Frame.m_SupportKnown && Frame.m_Supported;
	}

	static bool HasStableSafePlatform(const SImpactGameplayFrame &Frame, const SImpactGameplayFrame *pPrevious)
	{
		if(!HasSafePlatformContact(Frame))
			return false;
		// An unfrozen safe-platform frame immediately before a freeze is a positive
		// current baseline: the target was already safely resting when the problem
		// began. A frozen safe-platform contact, however, needs continuity with the
		// preceding frozen frame; a single transient supported snapshot must not be
		// promoted to self-recovery.
		if(pPrevious == nullptr)
			return !Frame.m_Frozen;
		if(!HasSafePlatformContact(*pPrevious))
			return false;
		if(!Frame.m_Frozen)
			return true;

		// A frozen platform is self-safe only when the current authoritative frame
		// still has a finite, active ordinary freeze timer. A timer that is absent,
		// expired, or being held by a different freeze state cannot prove that
		// natural expiry will restore the Tee.
		if(Frame.m_DeepFrozen || Frame.m_FreezeEnd <= 0 || Frame.m_FreezeEnd <= Frame.m_ServerTick)
			return false;
		if(!pPrevious->m_Frozen)
			return true;
		if(pPrevious->m_DeepFrozen || pPrevious->m_FreezeEnd <= 0 || pPrevious->m_FreezeEnd <= pPrevious->m_ServerTick)
			return false;

		// Safe collision/support is already checked for both authoritative frames.
		// Do not require byte-identical coordinates: normal quantization and a small
		// autonomous slide across the same safe platform are still continuous safe
		// recovery. Freeze identity and a non-increasing end tick are the existing
		// authoritative refresh signals; a changed start or extended end means that
		// the apparent platform contact is not a natural-expiry baseline.
		if(Frame.m_FreezeStart < 0 || pPrevious->m_FreezeStart < 0 || Frame.m_FreezeStart != pPrevious->m_FreezeStart)
			return false;
		return Frame.m_FreezeEnd <= pPrevious->m_FreezeEnd;
	}

	static bool HasImmediateSafePlatformBaseline(const SImpactGameplayFrame &Frame)
	{
		// This is used only for the first-action counterfactual. A raw authoritative
		// frame immediately before the first helper action is enough to establish that
		// the platform already existed; requiring a second safe frame would let a
		// needless hook become the apparent creator of an already observed platform.
		// The positive checks remain strict: ordinary finite freeze, safe collision,
		// support, no active freeze tile, and no teammate/dynamic support.
		return Frame.m_Frozen && !Frame.m_DeepFrozen &&
			Frame.m_FreezeEnd > Frame.m_ServerTick && Frame.m_FreezeStart >= 0 &&
			HasSafePlatformContact(Frame) &&
			Frame.m_OrdinaryFreezeTileKnown && !Frame.m_OrdinaryFreezeTile &&
			(!Frame.m_DynamicSupportKnown || !Frame.m_DynamicSupport) &&
			(!Frame.m_HookKnown || (Frame.m_HookState != HOOK_GRABBED && Frame.m_HookedPlayer < 0));
	}

	// Save-side self-safe evidence must be a positive autonomous result. A
	// pre-existing platform is retained as causality metadata, but support or a
	// safe collision sample alone cannot answer the counterfactual "would this
	// Tee have recovered without the helper?" question.
	static bool HasPositiveAutonomousSelfSafeFact(const SImpactGameplayFrame &Frame)
	{
		const bool SafeTeleDestination = Frame.m_TeleportDestinationKnown && Frame.m_TeleportDestinationSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportDestinationClearsDeepFreeze);
		const bool SafeTeleOutputSet = Frame.m_TeleportOutputSetKnown && Frame.m_TeleportOutputSetAllSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportOutputSetClearsDeepFreeze);
		return (Frame.m_AutonomousRecoveryKnown && Frame.m_AutonomousRecoveryPossible) ||
			SafeTeleDestination || SafeTeleOutputSet;
	}

	static bool HasFreezeRefreshBetweenFrames(const SImpactGameplayFrame &Current, const SImpactGameplayFrame *pPrevious)
	{
		if(pPrevious == nullptr || !Current.m_Frozen || !pPrevious->m_Frozen)
			return false;
		if(Current.m_FreezeStart >= 0 && pPrevious->m_FreezeStart >= 0 && Current.m_FreezeStart > pPrevious->m_FreezeStart)
			return true;
		return Current.m_FreezeEnd > 0 && pPrevious->m_FreezeEnd > 0 && Current.m_FreezeEnd > pPrevious->m_FreezeEnd;
	}

	// This is deliberately a predicate, not a latched decision state. It answers
	// only whether the current authoritative target frame contains a positive,
	// autonomous recovery fact. Missing evidence is not converted into a Fall at
	// freeze entry; the active problem episode remains in the observed state until
	// a later physical frame supplies a result.
	static bool CanSelfRecover(const SImpactGameplayFrame &Frame, const SImpactGameplayFrame *pPrevious = nullptr)
	{
		const bool SafeTeleDestination = Frame.m_TeleportDestinationKnown && Frame.m_TeleportDestinationSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportDestinationClearsDeepFreeze);
		const bool SafeTeleOutputSet = Frame.m_TeleportOutputSetKnown && Frame.m_TeleportOutputSetAllSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportOutputSetClearsDeepFreeze);
		return (Frame.m_AutonomousRecoveryKnown && Frame.m_AutonomousRecoveryPossible) ||
			HasStableSafePlatform(Frame, pPrevious) ||
			SafeTeleDestination || SafeTeleOutputSet;
	}

	static bool HasCurrentHelplessPhysicalEvidence(const SImpactGameplayFrame &Frame)
	{
		// A frozen snapshot is not enough. A current authoritative unsafe collision
		// position is positive physical problem evidence; a transient support bit is
		// not required and must not turn an unsafe frozen target into self-recovery.
		// Missing geometry remains non-decisive. An explicit authoritative
		// autonomous=false result is valid for deep-freeze too; deep-freeze itself is
		// not that result.
		if(Frame.m_AutonomousRecoveryKnown && !Frame.m_AutonomousRecoveryPossible)
			return true;
		// The native shadow evaluator is deliberately positive-only. Once it has
		// actually evaluated this frozen frame and returned UNRESOLVED, the generic
		// unsafe-collision fallback must not reinterpret that airborne snapshot as
		// harmful merely because no safe boundary has been observed yet.
		if(Frame.m_AutonomousRecoveryEvaluated)
			return false;
		if(Frame.m_DeepFrozen)
			return false;
		return Frame.m_SafeCollisionPositionKnown && !Frame.m_SafeCollisionPosition;
	}

	// The first target-specific action is compared with the physical state that
	// existed immediately before that action.  A safe snapshot from an older
	// point in the episode is not a safe baseline after the target has left that
	// position.  When the action and unfreeze share one authoritative snapshot,
	// the preceding frozen frame is the only usable pre-action state.
	static bool WasSelfRecoverableImmediatelyBeforeInteraction(const SImpactGameplayFrame &Current,
		const SImpactGameplayFrame *pPrevious, const SImpactGameplayFrame *pBeforePrevious)
	{
		if(pPrevious == nullptr)
			// The action is already present in Current. Without the immediately
			// preceding authoritative target frame there is no pre-action baseline;
			// treating the action frame itself as that baseline would turn a rescue
			// result into "already safe".
			return false;
		// The previous authoritative target frame is the state immediately before
		// the action. The action is allowed to change the current frame itself, and
		// an unfreeze on that frame is the restoration boundary, so a safe previous
		// frame must not be invalidated merely because the target is now unfrozen.
		// A continuing frozen frame is different: if it is no longer autonomous-safe,
		// the old contact/platform was only transient and must not poison the Save
		// baseline for a later rescue. Stale safety is therefore excluded without a
		// timeout or a latched post-action result.
		if(HasImmediateSafePlatformBaseline(*pPrevious))
			return true;
		return CanSelfRecover(*pPrevious, pBeforePrevious) && (!Current.m_Frozen || CanSelfRecover(Current, pPrevious));
	}

	// Save-side recovery is allowed to use an attributable teammate support
	// observation. That support is intentionally not included in CanSelfRecover:
	// teammate help can create a Save, but it can never turn a rescued target into
	// a self-safe target for the independent Fall resolver.
	static bool HasObservedRecoveryFact(const SImpactGameplayFrame &Frame)
	{
		if(!Frame.m_DeepFrozen && !Frame.m_Frozen)
			return true;
		const bool SafeTele =
			(Frame.m_TeleportDestinationKnown && Frame.m_TeleportDestinationSafe &&
				(!Frame.m_DeepFrozen || Frame.m_TeleportDestinationClearsDeepFreeze)) ||
			(Frame.m_TeleportOutputSetKnown && Frame.m_TeleportOutputSetAllSafe &&
				(!Frame.m_DeepFrozen || Frame.m_TeleportOutputSetClearsDeepFreeze));
		const bool SafeMapPlatform = !Frame.m_DeepFrozen && Frame.m_SafeCollisionPositionKnown &&
			Frame.m_SafeCollisionPosition && Frame.m_SupportKnown && Frame.m_Supported;
		return SafeTele || SafeMapPlatform ||
			(Frame.m_AutonomousRecoveryKnown && Frame.m_AutonomousRecoveryPossible) ||
			(Frame.m_DynamicSupportKnown && Frame.m_DynamicSupport);
	}

	static bool IsValidPhase2Target(const SImpactGameplayFrame &Frame, int TeamId)
	{
		return Frame.m_Confirmed && Frame.m_Present && Frame.m_CurrentCharacterKnown && Frame.m_Active &&
			Frame.m_Eligible && Frame.m_AliveKnown && Frame.m_Alive && Frame.m_FreezeKnown &&
			Frame.m_Participant.IsValid() && TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS &&
			Frame.m_TeamId == TeamId && Frame.m_SoloKnown && !Frame.m_Solo &&
			Frame.m_Participant.m_ClientId >= 0 && Frame.m_Participant.m_ClientId < MAX_CLIENTS;
	}

	EImpactPhysicalOutcome CImpactPhase2::EvaluatePhysicalOutcome(const SImpactGameplayFrame &Frame)
	{
		if(!Frame.m_CurrentCharacterKnown || !Frame.m_AliveKnown || !Frame.m_Alive || !Frame.m_FreezeKnown)
			return EImpactPhysicalOutcome::UNKNOWN;

		// These are authoritative harmful observations and must take precedence over
		// the generic unfrozen result. In particular, an unfrozen snapshot can still
		// be the first observation of a death tile or a harmful tele destination.
		if((Frame.m_TeleportDestinationKnown && Frame.m_TeleportDestinationHarmful) ||
			(Frame.m_TeleportOutputSetKnown && Frame.m_TeleportOutputSetAllHarmful))
			return EImpactPhysicalOutcome::HARMFUL;
		if(Frame.m_HarmfulOutcomeCandidateKnown && Frame.m_HarmfulOutcomeCandidate)
			return EImpactPhysicalOutcome::HARMFUL;

		// A confirmed live, unfrozen frame is the observed safe outcome. This is
		// deliberately an outcome observation, not an inference from a future Save.
		// A stale/inconsistent deep-freeze bit is not treated as recovered merely
		// because the ordinary freeze bit dropped; the deep-freeze state must first
		// be cleared by an authoritative recovery observation.
		if(!Frame.m_Frozen && !Frame.m_DeepFrozen)
			return EImpactPhysicalOutcome::SAFE;

		// A safe numbered output set is sufficient only when every possible output
		// is safe for this target's freeze state. For DeepFrozen, the producer marks
		// this only for a map outcome that clears DeepFrozen (DUNFREEZE or an
		// authoritative post-teleport clear), never for an ordinary platform.
		const bool SafeTeleDestination = Frame.m_TeleportDestinationKnown && Frame.m_TeleportDestinationSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportDestinationClearsDeepFreeze);
		const bool SafeTeleOutputSet = Frame.m_TeleportOutputSetKnown && Frame.m_TeleportOutputSetAllSafe &&
			(!Frame.m_DeepFrozen || Frame.m_TeleportOutputSetClearsDeepFreeze);
		if(SafeTeleDestination || SafeTeleOutputSet)
			return EImpactPhysicalOutcome::SAFE;

		// An explicit autonomous-recovery result is a physical producer field, not
		// RecoveryNeed. It is allowed to resolve the outcome in either direction.
		// A positive impossible result must not be hidden by a merely supported map
		// position; support is not proof that the target can finish recovery.
		if(Frame.m_AutonomousRecoveryKnown)
			return Frame.m_AutonomousRecoveryPossible ? EImpactPhysicalOutcome::SAFE : EImpactPhysicalOutcome::HARMFUL;

		// A finite DDNet freeze that is still authoritative after its server expiry is
		// persistent harmful physical evidence. This is not a freeze-entry shortcut:
		// a normal freeze remains UNKNOWN while its timer is running, and a natural
		// unfreeze reports SAFE before this condition can be observed. DeepFrozen is
		// deliberately excluded because that state alone is not a Fall outcome.
		if(!Frame.m_DeepFrozen && Frame.m_Frozen && Frame.m_FreezeEnd > 0 &&
			Frame.m_ServerTick >= Frame.m_FreezeEnd)
			return EImpactPhysicalOutcome::HARMFUL;

		// Dynamic character support is a positive observed physical outcome for an
		// ordinary frozen target. The live adapter sets this only after authoritative
		// current/previous character snapshots show collision contact, stabilization,
		// and an unrefreshed ordinary freeze timer. It is deliberately not sufficient
		// for DeepFrozen: a Tee or platform that merely holds a deep-frozen target does
		// not prove that DeepFreeze will be removed.
		if(!Frame.m_DeepFrozen && Frame.m_DynamicSupportKnown && Frame.m_DynamicSupport)
			return EImpactPhysicalOutcome::SAFE;

		// Deep-freeze is state evidence only. It does not identify a harmful
		// trajectory or rule out a later safe outcome. Leave that distinction to
		// the authoritative harmful observations above or to subsequent frames.
		// In particular, a single frozen snapshot on a map-ground collision position
		// is not yet an autonomous-safe outcome. The episode resolver may promote a
		// repeated, unrefreshed safe-position observation, or the actual unfrozen
		// snapshot may resolve it as SAFE.
		return EImpactPhysicalOutcome::UNKNOWN;
	}

	SImpactGameplayFrame NormalizeGameplayFrame(const SImpactGameplayInput &Input)
	{
		SImpactGameplayFrame Frame;
		Frame.m_Participant = Input.m_Participant;
		Frame.m_TeamId = Input.m_TeamId;
		Frame.m_ServerTick = Input.m_ServerTick;
		Frame.m_Present = Input.m_Present;
		Frame.m_CurrentCharacterKnown = Input.m_CurrentCharacterKnown;
		Frame.m_PreviousCharacterKnown = Input.m_PreviousCharacterKnown;
		Frame.m_PhysicallyPausedCharacter = Input.m_PhysicallyPausedCharacter;
		Frame.m_PositionKnown = Input.m_PositionKnown && Input.m_CurrentCharacterKnown;
		Frame.m_VelocityKnown = Input.m_VelocityKnown && Input.m_CurrentCharacterKnown;
		Frame.m_SupportKnown = Input.m_SupportKnown && Input.m_CurrentCharacterKnown && Frame.m_PositionKnown;
		Frame.m_Supported = Frame.m_SupportKnown && Input.m_Supported;
		Frame.m_DynamicSupportKnown = Input.m_DynamicSupportKnown && Input.m_CurrentCharacterKnown && Frame.m_PositionKnown;
		Frame.m_DynamicSupport = Frame.m_DynamicSupportKnown && Input.m_DynamicSupport;
		Frame.m_SafeCollisionPositionKnown = Input.m_SafeCollisionPositionKnown && Frame.m_PositionKnown;
		Frame.m_SafeCollisionPosition = Frame.m_SafeCollisionPositionKnown && Input.m_SafeCollisionPosition;
		Frame.m_OrdinaryFreezeTileKnown = Input.m_OrdinaryFreezeTileKnown && Frame.m_PositionKnown;
		Frame.m_OrdinaryFreezeTile = Frame.m_OrdinaryFreezeTileKnown && Input.m_OrdinaryFreezeTile;
		Frame.m_AutonomousRecoveryKnown = Input.m_AutonomousRecoveryKnown && Frame.m_PositionKnown;
		Frame.m_AutonomousRecoveryPossible = Frame.m_AutonomousRecoveryKnown && Input.m_AutonomousRecoveryPossible;
		Frame.m_PreActionAutonomousSelfSafeTick = Input.m_PreActionAutonomousSelfSafeTick;
		Frame.m_AliveKnown = Input.m_AliveKnown && Input.m_CurrentCharacterKnown;
		Frame.m_Alive = Frame.m_AliveKnown && Input.m_Alive;
		Frame.m_Active = Frame.m_CurrentCharacterKnown;
		Frame.m_FreezeKnown = Input.m_FreezeKnown && Input.m_CurrentCharacterKnown;
		Frame.m_DeepFrozen = Frame.m_FreezeKnown && Input.m_DeepFrozen;
		Frame.m_LiveFrozen = Frame.m_FreezeKnown && Input.m_LiveFrozen;
		Frame.m_FreezeEnd = Frame.m_FreezeKnown ? Input.m_FreezeEnd : 0;
		Frame.m_FreezeStart = Frame.m_FreezeKnown ? Input.m_FreezeStart : -1;
		Frame.m_Frozen = Frame.m_FreezeKnown && Input.m_Frozen;
		Frame.m_JumpState = Frame.m_CurrentCharacterKnown ? Input.m_JumpState : EImpactJumpState::UNKNOWN;
		Frame.m_DownwardShotKnown = Frame.m_CurrentCharacterKnown && Input.m_DownwardShotKnown;
		Frame.m_DownwardShot = Frame.m_DownwardShotKnown && Input.m_DownwardShot;
		Frame.m_SelfInitiatedFreezeKnown = Frame.m_CurrentCharacterKnown && Input.m_SelfInitiatedFreezeKnown;
		Frame.m_SelfInitiatedFreeze = Frame.m_SelfInitiatedFreezeKnown && Input.m_SelfInitiatedFreeze;
		Frame.m_HarmfulOutcomeCandidateKnown = Frame.m_CurrentCharacterKnown && Input.m_HarmfulOutcomeCandidateKnown;
		Frame.m_HarmfulOutcomeCandidate = Frame.m_HarmfulOutcomeCandidateKnown && Input.m_HarmfulOutcomeCandidate;
		Frame.m_TeleportType = Frame.m_CurrentCharacterKnown ? Input.m_TeleportType : EImpactPositionalTeleportType::NONE;
		Frame.m_TeleportNumber = Frame.m_CurrentCharacterKnown ? Input.m_TeleportNumber : 0;
		Frame.m_TeleportOutputSetKnown = Frame.m_CurrentCharacterKnown && Input.m_TeleportOutputSetKnown;
		Frame.m_TeleportOutputSetAllSafe = Frame.m_TeleportOutputSetKnown && Input.m_TeleportOutputSetAllSafe;
		Frame.m_TeleportOutputSetAllHarmful = Frame.m_TeleportOutputSetKnown && Input.m_TeleportOutputSetAllHarmful;
		Frame.m_TeleportOutputSetClearsDeepFreeze = Frame.m_TeleportOutputSetKnown && Input.m_TeleportOutputSetClearsDeepFreeze;
		Frame.m_TeleportDestinationKnown = Frame.m_CurrentCharacterKnown && Input.m_TeleportDestinationKnown;
		Frame.m_TeleportDestinationSafe = Frame.m_TeleportDestinationKnown && Input.m_TeleportDestinationSafe;
		Frame.m_TeleportDestinationHarmful = Frame.m_TeleportDestinationKnown && Input.m_TeleportDestinationHarmful;
		Frame.m_TeleportDestinationClearsDeepFreeze = Frame.m_TeleportDestinationKnown && Input.m_TeleportDestinationClearsDeepFreeze;
		Frame.m_SoloKnown = Input.m_SoloKnown && Input.m_CurrentCharacterKnown;
		Frame.m_Solo = Frame.m_SoloKnown && Input.m_Solo;
		Frame.m_Eligible = Input.m_Eligible;
		Frame.m_Engaged = Input.m_Engaged;
		Frame.m_Finalized = Input.m_Finalized;
		Frame.m_RecoveryNeed = Input.m_RecoveryNeed;
		Frame.m_HarmCause = Input.m_HarmCause;
		Frame.m_HarmCausalConfidence = Input.m_HarmCausalConfidence;
		if(!Frame.m_FreezeKnown || !Frame.m_Frozen || !Frame.m_PositionKnown || !Frame.m_VelocityKnown || !Frame.m_SupportKnown)
			Frame.m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		Frame.m_Finished = Input.m_Finished;
		Frame.m_FinishTimeKnown = Input.m_FinishTimeKnown && Input.m_FinishTimeMillis >= 0;
		Frame.m_FinishTimeMillis = Frame.m_FinishTimeKnown ? Input.m_FinishTimeMillis : -1;
		Frame.m_Confirmed = Input.m_Confirmed;
		Frame.m_X = Input.m_X;
		Frame.m_Y = Input.m_Y;
		Frame.m_VelX = Input.m_VelX;
		Frame.m_VelY = Input.m_VelY;
		Frame.m_HookKnown = Input.m_HookKnown && Input.m_CurrentCharacterKnown;
		Frame.m_HookedPlayer = Frame.m_HookKnown ? Input.m_HookedPlayer : -1;
		Frame.m_HookState = Frame.m_HookKnown ? Input.m_HookState : HOOK_IDLE;
		Frame.m_HookStateConfirmed = Frame.m_HookKnown;
		Frame.m_MeaningfulHookSupportKnown = Frame.m_HookKnown && Input.m_MeaningfulHookSupportKnown;
		Frame.m_MeaningfulHookSupport = Frame.m_MeaningfulHookSupportKnown && Input.m_MeaningfulHookSupport;
		Frame.m_AttackKnown = Input.m_AttackKnown && Input.m_CurrentCharacterKnown;
		Frame.m_AttackTick = Frame.m_AttackKnown ? Input.m_AttackTick : -1;
		Frame.m_Weapon = Frame.m_AttackKnown ? Input.m_Weapon : -1;
		Frame.m_TargetX = Input.m_TargetX;
		Frame.m_TargetY = Input.m_TargetY;
		Frame.m_HammerTargetEvidenceKnown = Frame.m_AttackKnown && Input.m_HammerTargetEvidenceKnown;
		Frame.m_HammerTargetEvidence = Frame.m_HammerTargetEvidenceKnown && Input.m_HammerTargetEvidence && Input.m_Weapon == WEAPON_HAMMER;
		Frame.m_PhysicalOutcome = CImpactPhase2::EvaluatePhysicalOutcome(Frame);
		return Frame;
	}

	static bool IsValidClientId(int ClientId)
	{
		return ClientId >= 0 && ClientId < MAX_CLIENTS;
	}

	static int64_t ConfirmationDeadline(const SImpactPendingEpisode &Episode)
	{
		const int64_t StartTick = Episode.m_ConfirmedRecoveryStartTick >= 0 ?
			Episode.m_ConfirmedRecoveryStartTick : Episode.m_RestoreTick;
		return StartTick >= 0 && Episode.m_RequiredTicks >= 0 ? StartTick + Episode.m_RequiredTicks : -1;
	}

	static bool IsFallConfirmed(const SImpactPendingEpisode &Episode)
	{
		return Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
			(Episode.m_FallAlreadyConfirmed || Episode.m_State == EImpactEpisodeState::CONFIRMED_FALL);
	}

	static void FillDebugFrame(SImpactPhase2Debug &Debug, const SImpactGameplayFrame &Frame)
	{
		Debug.m_TeamId = Frame.m_TeamId;
		Debug.m_ServerTick = Frame.m_ServerTick;
		Debug.m_Present = Frame.m_Present;
		Debug.m_TargetSnapshotAvailable = Frame.m_Confirmed && Frame.m_Present && Frame.m_CurrentCharacterKnown;
		Debug.m_TargetPredictedStateAvailable = false; // Phase2 consumes authoritative frames, not render prediction.
		Debug.m_AliveKnown = Frame.m_AliveKnown;
		Debug.m_Alive = Frame.m_Alive;
		Debug.m_Started = Frame.m_Eligible;
		Debug.m_Eligible = Frame.m_Eligible;
		Debug.m_HarmCause = Frame.m_HarmCause;
		Debug.m_HarmCausalConfidence = Frame.m_HarmCausalConfidence;
		Debug.m_FreezeKnown = Frame.m_FreezeKnown;
		Debug.m_FrozenNow = Frame.m_Frozen;
		Debug.m_DeepFrozen = Frame.m_DeepFrozen;
		Debug.m_FreezeStart = Frame.m_FreezeStart;
		Debug.m_FreezeEnd = Frame.m_FreezeEnd;
		Debug.m_JumpStateBeforeShot = Frame.m_JumpState;
		Debug.m_DownwardShotKnown = Frame.m_DownwardShotKnown;
		Debug.m_DownwardShot = Frame.m_DownwardShot;
		Debug.m_SelfOriginEvidence = Frame.m_HarmCause == EImpactHarmCause::SELF_CAUSED;
		Debug.m_ExternalCauseEvidence = Frame.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
		Debug.m_TeammateCause = Frame.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
		Debug.m_PositionKnown = Frame.m_PositionKnown;
		Debug.m_VelocityKnown = Frame.m_VelocityKnown;
		Debug.m_SupportKnown = Frame.m_SupportKnown;
		Debug.m_Supported = Frame.m_Supported;
		Debug.m_SafeCollisionPositionKnown = Frame.m_SafeCollisionPositionKnown;
		Debug.m_SafeCollisionPosition = Frame.m_SafeCollisionPosition;
		Debug.m_OrdinaryFreezeTileKnown = Frame.m_OrdinaryFreezeTileKnown;
		Debug.m_OrdinaryFreezeTile = Frame.m_OrdinaryFreezeTile;
		Debug.m_AutonomousRecoveryKnown = Frame.m_AutonomousRecoveryKnown;
		Debug.m_AutonomousRecoveryPossible = Frame.m_AutonomousRecoveryPossible;
		Debug.m_AutonomousRecoveryEvaluated = Frame.m_AutonomousRecoveryEvaluated;
		Debug.m_PhysicalOutcome = CImpactPhase2::EvaluatePhysicalOutcome(Frame);
		Debug.m_X = Frame.m_X;
		Debug.m_Y = Frame.m_Y;
		Debug.m_VelX = Frame.m_VelX;
		Debug.m_VelY = Frame.m_VelY;
		Debug.m_HookKnown = Frame.m_HookKnown;
		Debug.m_HookedPlayer = Frame.m_HookedPlayer;
		Debug.m_HookState = Frame.m_HookState;
		Debug.m_HammerTargetEvidenceKnown = Frame.m_HammerTargetEvidenceKnown;
	}

	static void FillDebugRecoveryProof(SImpactPhase2Debug &Debug, const SImpactCausalRecoveryProof &Proof)
	{
		if(!Proof.m_Valid)
			return;
		Debug.m_RelatedFallEpisodeId = Proof.m_FallEpisodeId;
		Debug.m_RelatedRecoveryEpisodeId = Proof.m_RecoveryEpisodeId;
		Debug.m_RescueProofId = Proof.m_ProofId;
		Debug.m_HarmStartTick = Proof.m_HarmStartTick;
		Debug.m_FirstProvenTick = Proof.m_FirstProvenTick;
		Debug.m_ActionTick = Proof.m_ActionTick;
		Debug.m_PreActionSafe = Proof.m_TargetAlreadySafeBeforeAction;
		// This compatibility record is not a permission gate. Report only the
		// autonomous fact that was actually captured before the first action; a
		// materialized recovery must not manufacture a "known safe" baseline.
		Debug.m_PreActionSafeKnown = Proof.m_TargetAlreadySafeBeforeAction;
		Debug.m_RecoveryCreated = true;
		Debug.m_CandidateCreated = Proof.m_RecoveryEpisodeId != 0;
		Debug.m_PreActionOutcome = Proof.m_PreActionOutcome;
		Debug.m_PostActionOutcome = Proof.m_PostActionOutcome;
		Debug.m_PhysicalChangeEvidence = Proof.m_PhysicalChangeEvidence;
		Debug.m_DecisivePhysicalEvidence = Proof.m_SafeOutcomeEvidence;
		Debug.m_TargetSpecificEvidence = Proof.m_TargetSpecificEvidence;
		Debug.m_HookEvidence = Proof.m_HookEvidence;
		Debug.m_HammerEvidence = Proof.m_HammerEvidence;
		Debug.m_HookFirstTick = Proof.m_HookFirstTick;
		Debug.m_HookLastTick = Proof.m_HookLastTick;
		Debug.m_OwnershipStatus = Proof.m_OwnershipStatus;
		Debug.m_CausalConfidence = Proof.m_Confidence;
		Debug.m_vContributors = Proof.m_vContributors;
	}

	static void FillDebugEpisode(SImpactPhase2Debug &Debug, const SImpactPendingEpisode &Episode)
	{
		Debug.m_ServerTick = Episode.m_RestoreTick >= 0 ? Episode.m_RestoreTick : Episode.m_StartTick;
		Debug.m_Target = Episode.m_Target;
		Debug.m_PotentialSaver = Episode.m_Actor;
		Debug.m_TeamId = Episode.m_TeamId;
		Debug.m_RelatedFallEpisodeId = Episode.m_RelatedFallEpisodeId;
		Debug.m_RelatedRecoveryEpisodeId = Episode.m_RelatedRecoveryEpisodeId;
		Debug.m_Started = Episode.m_TargetWasEligible;
		Debug.m_Eligible = Episode.m_TargetWasEligible;
		// Explicit ALED candidates are not inferred from a frozen frame. Keep the
		// debug record honest instead of presenting generic pending-episode defaults
		// as authoritative freeze evidence.
		Debug.m_FreezeKnown = Episode.m_Type != EImpactEventType::ALED_CONFIRMED;
		Debug.m_FrozenBefore = Episode.m_Type != EImpactEventType::ALED_CONFIRMED;
		Debug.m_FreezeStart = Episode.m_StartTick;
		Debug.m_HookEvidence = Episode.m_HookEvidence;
		Debug.m_HammerEvidence = Episode.m_HammerEvidence;
		Debug.m_HammerTargetEvidenceKnown = Episode.m_HammerEvidence;
		Debug.m_UnfreezeCause = Episode.m_UnfreezeCause;
		Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
		Debug.m_PhysicalOutcome = Episode.m_PhysicalOutcome;
		Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed != EImpactRecoveryNeed::UNKNOWN ?
			Episode.m_InitialRecoveryNeed : Episode.m_RecoveryNeed;
		Debug.m_FallAlreadyConfirmed = Episode.m_Type == EImpactEventType::FALL_CONFIRMED ?
			IsFallConfirmed(Episode) : Episode.m_RelatedFallAlreadyConfirmed;
		Debug.m_ConfirmedFall = Debug.m_FallAlreadyConfirmed;
		Debug.m_SelfOriginEvidence = Episode.m_OriginSelfOriginEvidence || Episode.m_HarmCause == EImpactHarmCause::SELF_CAUSED;
		Debug.m_ExternalCauseEvidence = Episode.m_OriginExternalCauseEvidence || Episode.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
		Debug.m_TeammateCause = Episode.m_OriginExternalCauseEvidence || Episode.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
		Debug.m_HarmCause = Episode.m_HarmCause;
		Debug.m_HarmCausalConfidence = Episode.m_HarmCausalConfidence;
		Debug.m_CausalConfidence = Episode.m_CausalConfidence;
		Debug.m_CandidateCreated = Episode.m_RelatedRecoveryEpisodeId != 0 || Episode.m_Type != EImpactEventType::FALL_CONFIRMED;
		FillDebugRecoveryProof(Debug, Episode.m_CausalRecoveryProof);
	}

	static void FillDebugPendingEpisodeContext(SImpactPhase2Debug &Debug, const SImpactPendingEpisode &Episode)
	{
		// Recovery diagnostics are often built from the current target frame, while
		// the authoritative lifecycle identifiers live on the pending episode. Copy
		// the durable context last so a sparse/dead frame cannot replace it with an
		// empty actor, target, or episode id.
		Debug.m_EpisodeId = Episode.m_EpisodeId;
		Debug.m_RelatedFallEpisodeId = Episode.m_RelatedFallEpisodeId;
		Debug.m_RelatedRecoveryEpisodeId = Episode.m_RelatedRecoveryEpisodeId;
		Debug.m_Target = Episode.m_Target;
		Debug.m_PotentialSaver = Episode.m_Actor;
		Debug.m_TeamId = Episode.m_TeamId;
		Debug.m_StartTick = Episode.m_StartTick;
		Debug.m_RestorationTick = Episode.m_RestoreTick;
		Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
		Debug.m_HookEvidence = Episode.m_HookEvidence;
		Debug.m_HammerEvidence = Episode.m_HammerEvidence;
		Debug.m_HookFirstTick = Episode.m_HookFirstTick;
		Debug.m_HookLastTick = Episode.m_HookLastTick;
		Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
		Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
		Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed != EImpactRecoveryNeed::UNKNOWN ?
			Episode.m_InitialRecoveryNeed : Episode.m_RecoveryNeed;
		Debug.m_EpisodeState = Episode.m_State;
		Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
		Debug.m_CausalConfidence = Episode.m_CausalConfidence;
		Debug.m_CandidateCreated = Episode.m_Type != EImpactEventType::FALL_CONFIRMED || Episode.m_RelatedRecoveryEpisodeId != 0;
		Debug.m_FallAlreadyConfirmed = Episode.m_Type == EImpactEventType::FALL_CONFIRMED ?
			IsFallConfirmed(Episode) : Episode.m_RelatedFallAlreadyConfirmed;
		Debug.m_ConfirmedFall = Debug.m_FallAlreadyConfirmed;
		FillDebugRecoveryProof(Debug, Episode.m_CausalRecoveryProof);
	}

	bool CImpactPhase2::IsValidTeam(int TeamId)
	{
		return TeamId > TEAM_FLOCK && TeamId < NUM_DDRACE_TEAMS;
	}

	bool CImpactPhase2::IsContinuouslyAliveUnfrozen(const SImpactGameplayFrame &Frame)
	{
		return Frame.m_Confirmed && Frame.m_Present && Frame.m_CurrentCharacterKnown && Frame.m_Active &&
			Frame.m_AliveKnown && Frame.m_Alive && Frame.m_FreezeKnown && !Frame.m_Frozen;
	}

	bool CImpactPhase2::SameParticipant(const SImpactGameplayFrame &Left, const SImpactGameplayFrame &Right)
	{
		return Left.m_Participant == Right.m_Participant;
	}

	const SImpactGameplayFrame *CImpactPhase2::FindParticipant(const std::vector<SImpactGameplayFrame> &vFrames, const SParticipantId &Id)
	{
		for(const SImpactGameplayFrame &Frame : vFrames)
			if(Frame.m_Participant == Id)
				return &Frame;
		return nullptr;
	}

	const SImpactGameplayFrame *CImpactPhase2::FindClient(const std::vector<SImpactGameplayFrame> &vFrames, int ClientId)
	{
		for(const SImpactGameplayFrame &Frame : vFrames)
			if(Frame.m_Participant.m_ClientId == ClientId)
				return &Frame;
		return nullptr;
	}

	void CImpactPhase2::AddEngagementProof(std::vector<SImpactGameplayFrame> &vFrames, const SParticipantId &Participant,
		int64_t Tick, const char *pReason, SImpactPhase2Output *pOutput)
	{
		if(!Participant.IsValid())
			return;
		for(SImpactGameplayFrame &Frame : vFrames)
		{
			if(Frame.m_Participant != Participant || !Frame.m_Confirmed || !Frame.m_Present || !Frame.m_Eligible ||
				Frame.m_Finalized || Frame.m_Engaged)
				continue;
			Frame.m_Engaged = true;
			if(pOutput == nullptr)
				return;
			const auto Existing = std::find_if(pOutput->m_vEngagementProofs.begin(), pOutput->m_vEngagementProofs.end(),
				[&Participant](const SImpactPhase2Output::SEngagementProof &Proof) { return Proof.m_Participant == Participant; });
			if(Existing == pOutput->m_vEngagementProofs.end())
				pOutput->m_vEngagementProofs.push_back({Participant, Tick, pReason == nullptr ? "PHASE2_EVIDENCE" : pReason});
			return;
		}
	}

	bool CImpactPhase2::HasPotentialEngagementEvidence(const std::vector<SImpactGameplayFrame> &vFrames, int TeamId,
		const std::vector<SImpactGameplayMessage> &vMessages)
	{
		auto IsCurrentRosterFrame = [TeamId](const SImpactGameplayFrame &Frame) {
			return Frame.m_Confirmed && Frame.m_Present && Frame.m_Eligible && !Frame.m_Finalized &&
				Frame.m_Participant.IsValid() && Frame.m_TeamId == TeamId && Frame.m_CurrentCharacterKnown &&
				Frame.m_Active && Frame.m_AliveKnown && Frame.m_Alive && Frame.m_SoloKnown && !Frame.m_Solo;
		};
		for(const SImpactGameplayFrame &Target : vFrames)
		{
			if(IsCurrentRosterFrame(Target) && !Target.m_Engaged && Target.m_FreezeKnown && Target.m_Frozen)
				return true;
		}
		for(const SImpactGameplayFrame &Actor : vFrames)
		{
			if(!IsCurrentRosterFrame(Actor))
				continue;
			for(const SImpactGameplayFrame &Target : vFrames)
			{
				if(Target.m_Participant == Actor.m_Participant || !IsCurrentRosterFrame(Target) ||
					(Actor.m_Engaged && Target.m_Engaged))
					continue;
				if(HasHookEvidence(Actor, Target.m_Participant.m_ClientId) ||
					(Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence))
					return true;
			}
		}
		for(const SImpactGameplayMessage &Message : vMessages)
		{
			if(!Message.m_Confirmed || (Message.m_TeamId != 0 && Message.m_TeamId != TeamId))
				continue;
			auto IsUnengagedFrame = [&vFrames, TeamId](const SParticipantId &Id) {
				return Id.IsValid() && std::any_of(vFrames.begin(), vFrames.end(), [&Id, TeamId](const SImpactGameplayFrame &Frame) {
					return Frame.m_Participant == Id && Frame.m_TeamId == TeamId && Frame.m_Confirmed && Frame.m_Present &&
						Frame.m_Eligible && !Frame.m_Finalized && !Frame.m_Engaged;
				});
			};
			const bool EngagementSignal = Message.m_Type == EImpactGameplayMessageType::CONFIRMED_HARM ||
				Message.m_Type == EImpactGameplayMessageType::CONFIRMED_ALED ||
				Message.m_Type == EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH ||
				Message.m_Type == EImpactGameplayMessageType::CONFIRMED_PREVENTED_PROGRESS_LOSS ||
				Message.m_Type == EImpactGameplayMessageType::CONFIRMED_RECOVERY_DIFFICULTY_INCREASE ||
				Message.m_Type == EImpactGameplayMessageType::CONFIRMED_JUMP_RESET;
			if(EngagementSignal && (IsUnengagedFrame(Message.m_Actor) || IsUnengagedFrame(Message.m_Target)))
				return true;
		}
		return false;
	}

	bool CImpactPhase2::HasCanonicalSaveSelfRecoveryProof(const SImpactGameplayFrame &Frame) const
	{
		if(!Frame.m_Participant.IsValid() || !IsValidClientId(Frame.m_Participant.m_ClientId))
			return false;

		const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
		if(!Observed.m_Valid || !SameParticipant(Observed.m_Frame, Frame) || Observed.m_ProblemStartTick < 0 ||
			!Observed.m_HasLastKnownFreeze)
			return false;

		// A positive autonomous result from the current frame is valid for Clutch
		// only before target-specific physical help changed this same frozen episode.
		// Once an interaction moved the Tee, a newly self-recoverable post-action
		// state cannot erase the provisional crisis that was observed beforehand;
		// the restored target is excluded from the final saver-roster proof.
		int64_t FirstPhysicalChangeTick = -1;
		for(const SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
		{
			if(Interaction.m_Target != Frame.m_Participant || !Interaction.m_RelevantToThisFreezeEpisode ||
				!Interaction.m_TargetSpecificEvidence || !Interaction.m_PhysicalChangeObserved ||
				!Interaction.m_PhysicalChangeWhileFrozen || Interaction.m_FirstPhysicalChangeTick < Observed.m_ProblemStartTick)
				continue;
			if(FirstPhysicalChangeTick < 0 || Interaction.m_FirstPhysicalChangeTick < FirstPhysicalChangeTick)
				FirstPhysicalChangeTick = Interaction.m_FirstPhysicalChangeTick;
		}
		const bool CurrentProofPredatesPhysicalHelp = FirstPhysicalChangeTick < 0 ||
			(Frame.m_ServerTick >= 0 && Frame.m_ServerTick < FirstPhysicalChangeTick);
		if(CurrentProofPredatesPhysicalHelp && HasPositiveAutonomousSelfSafeFact(Frame))
			return true;

		const bool NativeProofBelongsToCurrentEpisode = Observed.m_AutonomousSelfRecoveryNativeProof &&
			Observed.m_AutonomousSelfRecoverySourceTick >= Observed.m_ProblemStartTick;
		const bool NativeResultBelongsToCurrentEpisode = Observed.m_NativeAutonomousResultObserved &&
			Observed.m_NativeAutonomousResultSelfSafe && Observed.m_NativeAutonomousResultTick >= Observed.m_ProblemStartTick &&
			(FirstPhysicalChangeTick < 0 || Observed.m_NativeAutonomousResultTick < FirstPhysicalChangeTick);
		if(NativeProofBelongsToCurrentEpisode || NativeResultBelongsToCurrentEpisode)
			return true;
		return std::any_of(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
			[&Observed](const SCausalInteraction &Interaction) {
				return Interaction.m_FirstTick >= Observed.m_ProblemStartTick &&
					(Interaction.m_PreActionAutonomousSafe || Interaction.m_PreActionNativeSelfSafe);
			});
	}

	bool CImpactPhase2::FindSoleFunctionalParticipant(const std::vector<SImpactGameplayFrame> &vFrames, int TeamId,
		const SParticipantId *pExcludedParticipant, bool RequireResolved, SParticipantId *pParticipant,
		std::string *pDiagnostic, int *pUnknownCount, int *pFunctionalCount,
		std::vector<SImpactFunctionalRosterEntry> *pFunctionalRoster)
	{
		if(pParticipant == nullptr)
			return false;
		*pParticipant = {};
		if(pUnknownCount != nullptr)
			*pUnknownCount = 0;
		if(pFunctionalCount != nullptr)
			*pFunctionalCount = 0;
		const SImpactGameplayFrame *pSole = nullptr;
		int FunctionalCount = 0;
		int UnknownCount = 0;
		bool FirstDiagnosticParticipant = true;
		auto AddDiagnosticParticipant = [&](const SImpactGameplayFrame &Frame, const char *pDecision, const char *pReason) {
			if(pFunctionalRoster != nullptr && Frame.m_Participant.IsValid() && pFunctionalRoster->size() < MAX_PARTICIPANTS)
			{
				SImpactFunctionalRosterEntry Entry;
				Entry.m_Participant = Frame.m_Participant;
				Entry.m_RosterMember = Frame.m_Eligible;
				Entry.m_Engaged = Frame.m_Engaged;
				Entry.m_Finalized = Frame.m_Finalized;
				if(pDecision != nullptr && !std::strcmp(pDecision, "FUNCTIONAL"))
					Entry.m_Classification = EImpactFunctionalClass::FUNCTIONAL;
				else if(pDecision != nullptr && !std::strcmp(pDecision, "NON_FUNCTIONAL"))
					Entry.m_Classification = EImpactFunctionalClass::NON_FUNCTIONAL;
				else if(pDecision != nullptr && !std::strcmp(pDecision, "IGNORED"))
					Entry.m_Classification = EImpactFunctionalClass::IGNORED;
				else if(pDecision != nullptr && !std::strcmp(pDecision, "EXCLUDED"))
					Entry.m_Classification = EImpactFunctionalClass::EXCLUDED;
				Entry.m_Reason = pReason == nullptr ? "UNKNOWN" : pReason;
				Entry.m_AliveKnown = Frame.m_AliveKnown;
				Entry.m_Alive = Frame.m_Alive;
				Entry.m_Frozen = Frame.m_Frozen;
				Entry.m_AutonomousRecoveryKnown = Frame.m_AutonomousRecoveryKnown;
				Entry.m_AutonomousRecoveryPossible = Frame.m_AutonomousRecoveryPossible;
				pFunctionalRoster->push_back(std::move(Entry));
			}
			if(pDiagnostic == nullptr)
				return;
			if(!FirstDiagnosticParticipant)
				*pDiagnostic += ';';
			FirstDiagnosticParticipant = false;
			*pDiagnostic += "participant=" + std::to_string(Frame.m_Participant.m_ClientId) + ":" +
				std::to_string(Frame.m_Participant.m_SlotGeneration) +
				" eligible=" + std::to_string(Frame.m_Eligible ? 1 : 0) +
				" alive_known=" + std::to_string(Frame.m_AliveKnown ? 1 : 0) +
				" alive=" + std::to_string(Frame.m_Alive ? 1 : 0) +
				" frozen=" + std::to_string(Frame.m_Frozen ? 1 : 0) +
				" recovery_need=" + RecoveryNeedName(Frame.m_RecoveryNeed) +
				" autonomous_known=" + std::to_string(Frame.m_AutonomousRecoveryKnown ? 1 : 0) +
				" autonomous_possible=" + std::to_string(Frame.m_AutonomousRecoveryPossible ? 1 : 0) +
				" functional=" + std::string(pDecision == nullptr ? "UNKNOWN" : pDecision) +
				" reason=" + (pReason == nullptr ? "UNKNOWN" : pReason);
		};
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			if(pExcludedParticipant != nullptr && Frame.m_Participant == *pExcludedParticipant)
			{
				AddDiagnosticParticipant(Frame, "EXCLUDED", "TARGET_EXCLUDED");
				continue;
			}
			// Keep the existing authoritative team/participant gate. This predicate
			// only refines the functional state of an eligible participant; it does not
			// create a second recovery classifier.
			if(!Frame.m_Confirmed || !Frame.m_Present || !Frame.m_Eligible || !Frame.m_Participant.IsValid() ||
				Frame.m_TeamId != TeamId || Frame.m_Participant.m_ClientId < 0 || Frame.m_Participant.m_ClientId >= MAX_CLIENTS)
			{
				AddDiagnosticParticipant(Frame, "IGNORED", "IDENTITY_OR_TEAM_GATE");
				continue;
			}
			if(!Frame.m_Engaged || Frame.m_Finalized)
			{
				AddDiagnosticParticipant(Frame, "IGNORED", Frame.m_Finalized ? "FINALIZED" : "NOT_ENGAGED");
				continue;
			}
			if(!Frame.m_SoloKnown)
			{
				++UnknownCount;
				AddDiagnosticParticipant(Frame, "UNKNOWN", "SOLO_UNKNOWN");
				continue;
			}
			if(Frame.m_Solo)
			{
				AddDiagnosticParticipant(Frame, "IGNORED", "SOLO");
				continue;
			}
			if(!Frame.m_CurrentCharacterKnown || !Frame.m_Active || !Frame.m_AliveKnown || !Frame.m_FreezeKnown)
			{
				++UnknownCount;
				AddDiagnosticParticipant(Frame, "UNKNOWN", "AUTHORITATIVE_STATE_UNKNOWN");
				continue;
			}
			if(!Frame.m_Alive)
			{
				AddDiagnosticParticipant(Frame, "NON_FUNCTIONAL", "DEAD");
				continue;
			}

			if(!Frame.m_Frozen)
			{
				AddDiagnosticParticipant(Frame, "FUNCTIONAL", "UNFROZEN");
				pSole = &Frame;
				++FunctionalCount;
				if(FunctionalCount > 1 && pDiagnostic == nullptr)
					return false;
				continue;
			}

			// This is the exact Save-side positive veto: if the existing Save path
			// has proved that this frozen Tee would not need teammate assistance,
			// the Tee remains functional for Clutch. Do not substitute the raw
			// frozen bit or a separate Clutch recovery interpretation.
			const bool SaveSelfRecoveryProof = HasCanonicalSaveSelfRecoveryProof(Frame);
			if(SaveSelfRecoveryProof)
			{
				AddDiagnosticParticipant(Frame, "FUNCTIONAL", "SAVE_SELF_RECOVERY_PROOF");
				pSole = &Frame;
				++FunctionalCount;
				if(FunctionalCount > 1 && pDiagnostic == nullptr)
					return false;
				continue;
			}

			// Consume the already-authoritative Fall result for this exact physical
			// problem episode. A confirmed harmful Fall is the existing Phase2 proof
			// that this still-frozen Tee did not recover independently. Bind it to the
			// participant generation and current problem start; never use a historical
			// Fall count or an episode from another generation/team.
			const SImpactPendingEpisode *pCurrentConfirmedFall = nullptr;
			if(Frame.m_Frozen && Frame.m_FreezeKnown && Frame.m_AliveKnown && Frame.m_Alive &&
				Frame.m_Participant.IsValid() && IsValidClientId(Frame.m_Participant.m_ClientId))
			{
				const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
				if(Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame) && Observed.m_ProblemStartTick >= 0)
				{
					for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
					{
						if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
							Episode.m_Target == Frame.m_Participant && Episode.m_TeamId == TeamId &&
							Episode.m_StartTick == Observed.m_ProblemStartTick && IsFallConfirmed(Episode))
						{
							pCurrentConfirmedFall = &Episode;
							break;
						}
					}
				}
			}
			if(pCurrentConfirmedFall != nullptr)
			{
				AddDiagnosticParticipant(Frame, "NON_FUNCTIONAL", "CURRENT_FALL_CONFIRMED");
				continue;
			}

			// Only an actual negative/external Save-side fact can establish that this
			// Tee is unavailable for Clutch. An evaluated native simulation that is
			// UNRESOLVED is still UNKNOWN: absence of a positive self-safe veto is not
			// proof that external recovery is required.
			const SObservedState *pObserved = nullptr;
			if(Frame.m_Participant.IsValid() && IsValidClientId(Frame.m_Participant.m_ClientId))
			{
				const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
				if(Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame))
					pObserved = &Observed;
			}
			const bool SaveExternalRecoveryEvidence = Frame.m_DeepFrozen ||
				(Frame.m_AutonomousRecoveryKnown && !Frame.m_AutonomousRecoveryPossible) ||
				Frame.m_RecoveryNeed == EImpactRecoveryNeed::EXTERNAL_REQUIRED ||
				(pObserved != nullptr && pObserved->m_AutonomousHarmfulEvidenceProven);
			if(SaveExternalRecoveryEvidence)
			{
				AddDiagnosticParticipant(Frame, "NON_FUNCTIONAL", "SAVE_EXTERNAL_RECOVERY_ELIGIBLE");
				continue;
			}

			++UnknownCount;
			AddDiagnosticParticipant(Frame, "UNKNOWN", "SAVE_RECOVERY_NOT_RESOLVED");
			continue;
		}
		if(pUnknownCount != nullptr)
			*pUnknownCount = UnknownCount;
		if(pFunctionalCount != nullptr)
			*pFunctionalCount = FunctionalCount;
		if(pDiagnostic != nullptr)
		{
			*pDiagnostic += ";proof=";
			const bool SoleFunctionalProven = UnknownCount == 0 && FunctionalCount == 1 && pSole != nullptr;
			*pDiagnostic += (SoleFunctionalProven ? "PROVEN" : "NOT_PROVEN");
			*pDiagnostic += " functional_count=" + std::to_string(FunctionalCount) +
				" unknown_blockers=" + std::to_string(UnknownCount) +
				" sole_candidate=" + std::to_string(FunctionalCount == 1 && pSole != nullptr ? 1 : 0) +
				" crisis_proven=" + std::to_string(SoleFunctionalProven ? 1 : 0) +
				" target_excluded=" + std::to_string(pExcludedParticipant != nullptr ? 1 : 0);
			if(pSole != nullptr)
				*pDiagnostic += " sole=" + std::to_string(pSole->m_Participant.m_ClientId) + ":" + std::to_string(pSole->m_Participant.m_SlotGeneration);
		}
		if((RequireResolved && UnknownCount > 0) || FunctionalCount != 1 || pSole == nullptr)
			return false;
		*pParticipant = pSole->m_Participant;
		return true;
	}

	bool CImpactPhase2::HasHookEvidence(const SImpactGameplayFrame &Actor, int TargetClientId)
	{
		return Actor.m_HookKnown && Actor.m_HookedPlayer == TargetClientId && Actor.m_HookState == HOOK_GRABBED;
	}

	static bool HasObservedMovementChange(const SImpactGameplayFrame &Previous, const SImpactGameplayFrame &Current)
	{
		if(Previous.m_PositionKnown && Current.m_PositionKnown &&
			(Previous.m_X != Current.m_X || Previous.m_Y != Current.m_Y))
			return true;
		return Previous.m_VelocityKnown && Current.m_VelocityKnown &&
			(Previous.m_VelX != Current.m_VelX || Previous.m_VelY != Current.m_VelY);
	}

	CImpactPhase2::SCausalInteraction *CImpactPhase2::FindInteraction(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target)
	{
		for(SCausalInteraction &Interaction : vInteractions)
			if(Interaction.m_Actor == Actor && Interaction.m_Target == Target)
				return &Interaction;
		return nullptr;
	}

	bool CImpactPhase2::HasMeaningfulHookControl(const SCausalInteraction &Interaction)
	{
		// Meaningful control is an explicit target-specific evidence bit collected
		// by the episode-scoped spans. A grabbed observation is only hook evidence;
		// neither one observation nor an arbitrary observation count proves that the
		// hook materially supported recovery. Rehooks and anchor changes remain part
		// of the same interaction when the evidence is available.
		for(const SHookSpan &Span : Interaction.m_vHookSpans)
			if(Span.m_MeaningfulSupport)
				return true;
		return false;
	}

	void CImpactPhase2::ObserveInteractionPhysicalEvidence(SCausalInteraction &Interaction, const SImpactGameplayFrame &Target, bool ActionActive, int64_t SafeOutcomeTick, bool AllowCausalSafeTransition)
	{
		if(!ActionActive || Interaction.m_ActionStartTick < 0 || Target.m_ServerTick <= Interaction.m_ActionStartTick)
			return;
		// Later touches remain part of the episode history, but cannot replace the
		// action that had already produced the first observed safe outcome.
		if(SafeOutcomeTick >= 0 && Target.m_ServerTick > SafeOutcomeTick)
			return;

		// The raw Save path uses the positive recovery facts directly. The legacy
		// PhysicalOutcome classifier remains metadata for old readers, but it must
		// not turn an UNKNOWN/HARMFUL label into a Save permission gate.
		const bool SafeOutcomeObserved = HasObservedRecoveryFact(Target);
		if(SafeOutcomeObserved)
			Interaction.m_PostActionOutcome = EImpactPhysicalOutcome::SAFE;

		bool PhysicalChange = false;
		if(Interaction.m_ActionStartPositionKnown && Target.m_PositionKnown &&
			(Interaction.m_ActionStartX != Target.m_X || Interaction.m_ActionStartY != Target.m_Y))
			PhysicalChange = true;
		if(Interaction.m_ActionStartVelocityKnown && Target.m_VelocityKnown &&
			(Interaction.m_ActionStartVelX != Target.m_VelX || Interaction.m_ActionStartVelY != Target.m_VelY))
			PhysicalChange = true;
		if(!PhysicalChange)
			return;

		if(!Interaction.m_PhysicalChangeObserved)
			Interaction.m_FirstPhysicalChangeTick = Target.m_ServerTick;
		Interaction.m_PhysicalChangeObserved = true;
		if(Target.m_Frozen)
			Interaction.m_PhysicalChangeWhileFrozen = true;
		Interaction.m_LastPhysicalChangeTick = Target.m_ServerTick;

		// The raw Save path needs only the current problem episode, target-specific
		// attribution, physical change, and a later observed safe boundary. The
		// compatibility problem-state/outcome fields are deliberately not gates:
		// sparse snapshots can omit the frame immediately before an interaction even
		// though the episode and the interaction are authoritative.
		const bool PreActionWasSafe = Interaction.m_PreActionAutonomousSafe;
		if(PreActionWasSafe || !AllowCausalSafeTransition ||
			!Interaction.m_TargetSpecificEvidence ||
			!SafeOutcomeObserved)
			return;

		// This is the episode-level causal evidence used for Save ownership: the
		// target-specific interaction changed the observed physical state and a later
		// authoritative SAFE boundary followed it. The interaction remains scoped to
		// the whole episode, so release/unfreeze ordering cannot erase it.
		// A stable autonomous-safe observation that pre-dates this action is not a
		// causal rescue. It is intentionally retained as negative evidence so a
		// later hook cannot steal ownership from an already-safe trajectory.
		Interaction.m_CausalContributionObserved = true;
		if(!Interaction.m_CausalSafeTransitionObserved)
		{
			// The first proven safe transition is the causal proof. A later rehook or
			// touch belongs to the same episode but must not replace that proof with a
			// last-touch attribution.
			Interaction.m_CausalSafeTransitionObserved = true;
			Interaction.m_SafeTransitionTick = Target.m_ServerTick;
		}
	}

	void CImpactPhase2::UpdateHookSpan(SCausalInteraction &Interaction, int64_t Tick, bool MeaningfulSupportEvidence)
	{
		SHookSpan *pSpan = nullptr;
		if(!Interaction.m_vHookSpans.empty() && Interaction.m_vHookSpans.back().m_Active)
			pSpan = &Interaction.m_vHookSpans.back();
		else
		{
			if(Interaction.m_vHookSpans.size() >= MAX_HOOK_SPANS)
				return;
			Interaction.m_vHookSpans.push_back({});
			pSpan = &Interaction.m_vHookSpans.back();
			pSpan->m_StartTick = Tick;
			pSpan->m_LastGrabbedTick = Tick;
			pSpan->m_ConfirmedObservations = 1;
			pSpan->m_MeaningfulSupport = MeaningfulSupportEvidence;
			pSpan->m_Active = true;
		}
		pSpan->m_MeaningfulSupport |= MeaningfulSupportEvidence;
		if(Tick > pSpan->m_LastGrabbedTick)
		{
			pSpan->m_LastGrabbedTick = Tick;
			if(pSpan->m_ConfirmedObservations < UINT32_MAX)
				++pSpan->m_ConfirmedObservations;
		}
		Interaction.m_HookControlContinuity = HasMeaningfulHookControl(Interaction);
	}

	void CImpactPhase2::CloseHookSpan(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target, int64_t Tick)
	{
		SCausalInteraction *pInteraction = FindInteraction(vInteractions, Actor, Target);
		if(pInteraction == nullptr || pInteraction->m_vHookSpans.empty())
			return;
		SHookSpan &Span = pInteraction->m_vHookSpans.back();
		// A release is a one-time transition. Do not re-enable post-action tracking
		// on every later idle-hook snapshot: a different actor may have started a
		// new action in the meantime, and subsequent target movement belongs to that
		// newer action rather than being retroactively attributed to this released
		// span. With no intervening actor action, the one true release continues to
		// preserve its causal evidence through later unfreeze/restoration.
		if(!Span.m_Active)
			return;
		Span.m_EndTick = Tick;
		Span.m_Active = false;
		pInteraction->m_HookControlContinuity = HasMeaningfulHookControl(*pInteraction);
		pInteraction->m_ActionActive = false;
		pInteraction->m_TrackPostActionOutcome = true;
	}

	SImpactEvent CImpactPhase2::MakeEvent(EImpactEventType Type, int TeamId, const SParticipantId &Actor, const SParticipantId &Target, int64_t StartTick, int64_t ResolveTick, const char *pReason)
	{
		SImpactEvent Event;
		Event.m_Type = Type;
		Event.m_Actor = Actor;
		Event.m_Target = Target;
		Event.m_TeamId = TeamId;
		Event.m_StartTick = StartTick;
		Event.m_ResolveTick = ResolveTick;
		Event.m_Confirmed = true;
		Event.m_EpisodeState = EImpactEpisodeState::CONFIRMED_RECOVERY;
		Event.m_CausalConfidence = ECausalConfidence::CONFIRMED;
		Event.m_OwnershipStatus = Actor.IsValid() ? EOwnershipStatus::ACTOR : EOwnershipStatus::NONE;
		Event.m_CausalReason = pReason ? pReason : "";
		return Event;
	}

	void CImpactPhase2::AddHelper(std::vector<SParticipantId> &vHelpers, const SParticipantId &Id)
	{
		if(!Id.IsValid() || vHelpers.size() >= MAX_EVENT_HELPERS)
			return;
		for(const SParticipantId &Existing : vHelpers)
			if(Existing == Id)
				return;
		vHelpers.push_back(Id);
	}

	void CImpactPhase2::AddInteraction(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target, bool HammerEvidence, bool HookEvidence, bool MeaningfulSupportEvidence, bool HammerAttributionKnown, bool AutonomousSafeObserved, int64_t Tick, const SImpactGameplayFrame &TargetFrame)
	{
		if(!Actor.IsValid() || !Target.IsValid() || (!HammerEvidence && !HookEvidence))
			return;
		SCausalInteraction *pInteraction = FindInteraction(vInteractions, Actor, Target);
		if(pInteraction == nullptr)
		{
			if(vInteractions.size() >= MAX_PARTICIPANTS)
				return;
			SCausalInteraction Interaction;
			Interaction.m_Actor = Actor;
			Interaction.m_Target = Target;
			Interaction.m_FirstTick = Tick;
			Interaction.m_HammerAttributionKnown = HammerAttributionKnown;
			Interaction.m_RelevantToThisFreezeEpisode = true;
			Interaction.m_CausalConfidence = ECausalConfidence::HIGH;
			Interaction.m_PreActionOutcome = EvaluatePhysicalOutcome(TargetFrame);
			Interaction.m_PreActionProblemStateKnown = TargetFrame.m_Frozen;
			Interaction.m_PreActionAutonomousSafe = AutonomousSafeObserved;
			// The target frame contains the action observation itself. Its current
			// collision bit therefore cannot be used as a pre-action baseline: the
			// action may already have put the target on that position. Preserve map
			// position evidence only when it was latched before this first action.
			Interaction.m_PreActionSafeCollisionPositionKnown = AutonomousSafeObserved && TargetFrame.m_SafeCollisionPositionKnown;
			Interaction.m_PreActionSafeCollisionPosition = Interaction.m_PreActionSafeCollisionPositionKnown && TargetFrame.m_SafeCollisionPosition;
			Interaction.m_PreActionFreezeEnd = TargetFrame.m_FreezeEnd;
			Interaction.m_PreActionDeepFrozen = TargetFrame.m_DeepFrozen;
			Interaction.m_PreActionDynamicSupportKnown = TargetFrame.m_DynamicSupportKnown;
			Interaction.m_PreActionDynamicSupport = TargetFrame.m_DynamicSupport;
			Interaction.m_PreActionTeleportDestinationKnown = TargetFrame.m_TeleportDestinationKnown;
			Interaction.m_PreActionTeleportDestinationSafe = TargetFrame.m_TeleportDestinationSafe;
			Interaction.m_ActionStartTick = Tick;
			Interaction.m_ActionStartPositionKnown = TargetFrame.m_PositionKnown;
			Interaction.m_ActionStartX = TargetFrame.m_X;
			Interaction.m_ActionStartY = TargetFrame.m_Y;
			Interaction.m_ActionStartVelocityKnown = TargetFrame.m_VelocityKnown;
			Interaction.m_ActionStartVelX = TargetFrame.m_VelX;
			Interaction.m_ActionStartVelY = TargetFrame.m_VelY;
			Interaction.m_ActionActive = true;
			Interaction.m_TrackPostActionOutcome = true;
			vInteractions.push_back(std::move(Interaction));
			pInteraction = &vInteractions.back();
		}
		else if(!pInteraction->m_ActionActive)
		{
			// Re-hook/re-attack starts a new physical action while retaining the
			// same episode-level interaction record and its prior contributors. Once
			// an action has already proved the safe transition, preserve its original
			// baseline and proof; a rehook is only additional history.
			// The interaction record has one immutable pre-action baseline for the
			// whole problem episode. Re-hooking starts another action/span, but must
			// never replace that baseline with a later post-action SAFE/contact frame.
			if(!pInteraction->m_CausalSafeTransitionObserved)
			{
				pInteraction->m_ActionStartTick = Tick;
				pInteraction->m_ActionStartPositionKnown = TargetFrame.m_PositionKnown;
				pInteraction->m_ActionStartX = TargetFrame.m_X;
				pInteraction->m_ActionStartY = TargetFrame.m_Y;
				pInteraction->m_ActionStartVelocityKnown = TargetFrame.m_VelocityKnown;
				pInteraction->m_ActionStartVelX = TargetFrame.m_VelX;
				pInteraction->m_ActionStartVelY = TargetFrame.m_VelY;
			}
			pInteraction->m_ActionActive = true;
			pInteraction->m_TrackPostActionOutcome = true;
		}

		pInteraction->m_HammerEvidence |= HammerEvidence;
		pInteraction->m_HammerAttributionKnown &= HammerAttributionKnown;
		pInteraction->m_HookEvidence |= HookEvidence;
		pInteraction->m_TargetSpecificEvidence = true;
		pInteraction->m_RelevantToThisFreezeEpisode = true;
		pInteraction->m_CausalConfidence = ECausalConfidence::HIGH;
		if(HookEvidence)
		{
			if(pInteraction->m_HookFirstTick < 0)
				pInteraction->m_HookFirstTick = Tick;
			pInteraction->m_HookLastTick = std::max(pInteraction->m_HookLastTick, Tick);
			UpdateHookSpan(*pInteraction, Tick, MeaningfulSupportEvidence);
		}
		if(HammerEvidence)
		{
			if(pInteraction->m_HammerFirstTick < 0)
				pInteraction->m_HammerFirstTick = Tick;
			pInteraction->m_HammerLastTick = std::max(pInteraction->m_HammerLastTick, Tick);
		}
		pInteraction->m_ActionActive = true;
		pInteraction->m_TrackPostActionOutcome = true;
		if(pInteraction->m_FirstTick < 0)
			pInteraction->m_FirstTick = Tick;
		pInteraction->m_LastTick = std::max(pInteraction->m_LastTick, Tick);
	}

	static EImpactForensicReason ForensicReasonFromDetail(const std::string &Detail)
	{
		if(Detail.find("SELF_SAFE") != std::string::npos || Detail.find("already safe before") != std::string::npos)
			return EImpactForensicReason::SELF_SAFE_ALREADY_PROVEN;
		if(Detail.find("no target-specific interaction") != std::string::npos ||
			Detail.find("no uniquely provable decisive saver") != std::string::npos)
			return EImpactForensicReason::NO_RELIABLE_ACTOR;
		if(Detail.find("physical change was observed only after recovery") != std::string::npos ||
			Detail.find("no observed physical recovery") != std::string::npos)
			return EImpactForensicReason::NON_CAUSAL_INTERACTION;
		if(Detail.find("requires an observed unfrozen restoration") != std::string::npos)
			return EImpactForensicReason::RESTORATION_NOT_OBSERVED;
		if(Detail.find("became frozen again before confirmation") != std::string::npos)
			return EImpactForensicReason::REFROZEN_BEFORE_CONFIRMATION;
		if(Detail.find("target generation changed") != std::string::npos || Detail.find("target generation mismatch") != std::string::npos)
			return EImpactForensicReason::TARGET_GENERATION_CHANGED;
		if(Detail.find("actor generation changed") != std::string::npos || Detail.find("actor generation mismatch") != std::string::npos)
			return EImpactForensicReason::ACTOR_GENERATION_CHANGED;
		if(Detail.find("participant finalized") != std::string::npos || Detail.find("participant disconnected") != std::string::npos)
			return EImpactForensicReason::PARTICIPANT_FINALIZED;
		if(Detail.find("team boundary") != std::string::npos || Detail.find("team reset") != std::string::npos || Detail.find("team wipe") != std::string::npos)
			return EImpactForensicReason::TEAM_BOUNDARY;
		if(Detail.find("quorum") != std::string::npos)
			return EImpactForensicReason::QUORUM_LOST;
		if(Detail.find("processing paused") != std::string::npos)
			return EImpactForensicReason::PROCESSING_PAUSED;
		if(Detail.find("stale episode") != std::string::npos)
			return EImpactForensicReason::STALE_EPISODE;
		if(Detail.find("OTHER_UNKNOWN") != std::string::npos || Detail.find("UNKNOWN") != std::string::npos)
			return EImpactForensicReason::UNKNOWN_EVIDENCE;
		if(Detail.find("EPISODE_ALREADY_CONSUMED") != std::string::npos)
			return EImpactForensicReason::EPISODE_ALREADY_CONSUMED;
		if(Detail.find("OTHER_FUNCTIONAL") != std::string::npos || Detail.find("another functional") != std::string::npos)
			return EImpactForensicReason::OTHER_FUNCTIONAL_PARTICIPANT;
		if(Detail.find("ROSTER_NOT_READY") != std::string::npos)
			return EImpactForensicReason::ROSTER_NOT_READY;
		return EImpactForensicReason::EXISTING_GENERIC_REJECT;
	}

	void CImpactPhase2::AddDebug(SImpactPhase2Output *pOutput, const SImpactPhase2Debug &Debug)
	{
		if(pOutput == nullptr)
			return;
		if(pOutput->m_vDebug.size() >= MAX_PHASE2_DEBUG_RECORDS)
		{
			if(m_ForensicTraceEnabled)
			{
				pOutput->m_ForensicTraceTruncated = true;
				++pOutput->m_ForensicDroppedEvents;
			}
			return;
		}
		pOutput->m_vDebug.push_back(Debug);
		if(!m_ForensicTraceEnabled)
			return;

		auto AddForensic = [&](EImpactForensicType Type) {
			if(pOutput->m_vForensic.size() >= MAX_PHASE2_DEBUG_RECORDS * 2)
			{
				pOutput->m_ForensicTraceTruncated = true;
				++pOutput->m_ForensicDroppedEvents;
				return;
			}
			SImpactForensicEvent Event;
			Event.m_Type = Type;
			Event.m_EventId = Debug.m_EventId;
			Event.m_ProblemEpisodeId = Debug.m_ProblemEpisodeId != 0 ? Debug.m_ProblemEpisodeId :
				(Debug.m_RelatedFallEpisodeId != 0 ? Debug.m_RelatedFallEpisodeId :
					(Debug.m_EventType == EImpactEventType::FALL_CONFIRMED ? Debug.m_EpisodeId : 0));
			Event.m_FallEpisodeId = Debug.m_RelatedFallEpisodeId != 0 ? Debug.m_RelatedFallEpisodeId :
				(Debug.m_EventType == EImpactEventType::FALL_CONFIRMED ? Debug.m_EpisodeId : 0);
			const bool SaveLifecycleRecord = Debug.m_DebugLabel == "SAVE_RESTORE" || Debug.m_DebugLabel == "SAVE_PENDING" ||
				Debug.m_DebugLabel == "SAVE_REJECTED" || Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE" ||
				Debug.m_DebugLabel == "SAVE_CONFIRMED" || Debug.m_DebugLabel == "SAVE_CANDIDATE_CREATED";
			Event.m_SaveCandidateId = SaveLifecycleRecord || Debug.m_CandidateCreated || Debug.m_RecoveryCreated ? Debug.m_EpisodeId : 0;
			Event.m_ClutchEpisodeId = Debug.m_ClutchEpisodeId;
			Event.m_ActorInteractionId = Debug.m_InteractionId;
			Event.m_Tick = Debug.m_ServerTick >= 0 ? Debug.m_ServerTick : Debug.m_EmissionTick;
			Event.m_TeamId = Debug.m_TeamId;
			Event.m_Target = Debug.m_Target;
			Event.m_Actor = Debug.m_PotentialSaver;
			Event.m_ReasonDetail = Debug.m_Reason.substr(0, MAX_FORENSIC_DETAIL_LENGTH);
			Event.m_Reason = ForensicReasonFromDetail(Debug.m_Reason);
			Event.m_EpisodeStateBefore = Debug.m_EpisodeStateBefore;
			Event.m_EpisodeStateAfter = Debug.m_EpisodeStateAfter != EImpactEpisodeState::NONE ?
				Debug.m_EpisodeStateAfter : Debug.m_EpisodeState;
			Event.m_PhysicalOutcome = Debug.m_PhysicalOutcome;
			Event.m_TargetSnapshotAvailable = Debug.m_TargetSnapshotAvailable;
			Event.m_ActorSnapshotAvailable = Debug.m_ActorSnapshotAvailable;
			Event.m_TargetPredictedStateAvailable = Debug.m_TargetPredictedStateAvailable;
			Event.m_ActorTargetInteractionObservable = Debug.m_ActorTargetInteractionObservable;
			Event.m_PhysicalChangeEvidence = Debug.m_PhysicalChangeEvidence;
			Event.m_DecisivePhysicalEvidence = Debug.m_DecisivePhysicalEvidence;
			Event.m_DecisionEligible = Debug.m_Eligible;
			Event.m_DecisionAccepted = Debug.m_Started;
			Event.m_AutonomousEvidenceEvaluated = Debug.m_AutonomousRecoveryEvaluated;
			Event.m_AutonomousEvidenceKnown = Debug.m_AutonomousRecoveryKnown;
			Event.m_AutonomousRecoveryPossible = Debug.m_AutonomousRecoveryPossible;
			Event.m_SelfSafeProven = Type != EImpactForensicType::SELF_SAFE_INVALIDATED &&
				(Debug.m_AutonomousSelfRecoveryProven || Debug.m_PreActionSafe);
			Event.m_SelfSafeEvidenceTick = Debug.m_SelfSafeEvidenceTick;
			Event.m_RestorationTick = Debug.m_RestorationTick;
			Event.m_ConfirmationStartTick = Debug.m_ConfirmationStartTick;
			Event.m_ConfirmationDeadlineTick = Debug.m_ConfirmationDeadlineTick;
			Event.m_ConfirmedTicks = Debug.m_ConfirmedTicks;
			Event.m_RequiredTicks = Debug.m_RequiredTicks;
			Event.m_RawDelta = Debug.m_RawDelta;
			Event.m_vFunctionalRoster = Debug.m_vFunctionalRoster;
			pOutput->m_vForensic.push_back(std::move(Event));
		};

		if(Debug.m_DebugLabel == "FALL_TRACE")
			AddForensic(Debug.m_Stage == EImpactPhase2DebugStage::CANCELLED ? EImpactForensicType::PROBLEM_EPISODE_END : EImpactForensicType::PROBLEM_EPISODE_START);
		else if(Debug.m_DebugLabel == "FALL_CANDIDATE_CREATED")
			AddForensic(EImpactForensicType::FALL_CANDIDATE_CREATED);
		else if(Debug.m_DebugLabel == "FALL_CONFIRM" && Debug.m_ConfirmedFall)
			AddForensic(EImpactForensicType::FALL_CONFIRMED);
		else if(Debug.m_DebugLabel == "FALL_NOT_EMITTED")
			AddForensic(EImpactForensicType::FALL_REJECTED);
		else if(Debug.m_DebugLabel == "SELF_SAFE_PROVEN")
			AddForensic(EImpactForensicType::SELF_SAFE_PROVEN);
		else if(Debug.m_DebugLabel == "SELF_SAFE_INVALIDATED")
			AddForensic(EImpactForensicType::SELF_SAFE_INVALIDATED);
		else if(Debug.m_DebugLabel == "RESTORATION_OBSERVED")
			AddForensic(EImpactForensicType::RESTORATION_OBSERVED);
		else if(Debug.m_DebugLabel == "SAVE_INTERACTION")
		{
			AddForensic(EImpactForensicType::INTERACTION_OBSERVED);
			AddForensic(Debug.m_PreActionSafe ? EImpactForensicType::ACTOR_CAUSAL_EVIDENCE_REJECTED :
				Debug.m_DecisivePhysicalEvidence ? EImpactForensicType::ACTOR_CAUSAL_EVIDENCE_POSITIVE :
				EImpactForensicType::ACTOR_CAUSAL_EVIDENCE_UNKNOWN);
		}
		else if(Debug.m_DebugLabel == "SAVE_RESTORE")
		{
			AddForensic(EImpactForensicType::RESTORATION_OBSERVED);
			if(Debug.m_CandidateCreated)
				AddForensic(EImpactForensicType::SAVE_CANDIDATE_CREATED);
		}
		else if(Debug.m_DebugLabel == "SAVE_PENDING")
		{
			if(Debug.m_Reason == "confirmation baseline started")
				AddForensic(EImpactForensicType::SAVE_CONFIRMATION_STARTED);
			else if(Debug.m_CandidateCreated || Debug.m_RecoveryCreated)
				AddForensic(EImpactForensicType::SAVE_CANDIDATE_CREATED);
		}
		else if(Debug.m_DebugLabel == "SAVE_REJECTED_REFREEZE")
		{
			AddForensic(EImpactForensicType::REFREEZE_BEFORE_CONFIRMATION);
			AddForensic(EImpactForensicType::SAVE_CONFIRMATION_FAILED);
		}
		else if(Debug.m_DebugLabel == "SAVE_REJECTED")
			AddForensic(Debug.m_CandidateCreated || Debug.m_ConfirmationStartTick >= 0 ?
				EImpactForensicType::SAVE_CONFIRMATION_FAILED : EImpactForensicType::SAVE_CANDIDATE_REJECTED);
		else if(Debug.m_DebugLabel == "SAVE_CONFIRMED")
		{
			AddForensic(EImpactForensicType::SAVE_CONFIRMED);
			if(Debug.m_RawDelta.m_Clutches > 0)
				AddForensic(EImpactForensicType::CLUTCH_CONFIRMED);
		}
		else if(Debug.m_DebugLabel == "CLUTCH_EPISODE_ARM")
			AddForensic(EImpactForensicType::CLUTCH_EPISODE_START);
		else if(Debug.m_DebugLabel == "CLUTCH_CANDIDATE")
			AddForensic(EImpactForensicType::CLUTCH_CANDIDATE_CREATED);
		else if(Debug.m_DebugLabel == "CLUTCH_FINAL_PROOF" && Debug.m_Reason.find("final=REJECTED") != std::string::npos)
			AddForensic(EImpactForensicType::CLUTCH_CANDIDATE_REJECTED);
		else if(Debug.m_DebugLabel == "CLUTCH_CRISIS_EVAL")
			AddForensic(EImpactForensicType::CLUTCH_ROSTER_EVALUATED);
		else if(Debug.m_DebugLabel == "CLUTCH_EPISODE_ENDED")
			AddForensic(EImpactForensicType::CLUTCH_EPISODE_END);
	}

	void CImpactPhase2::AddDeathDebug(SImpactPhase2Output *pOutput, const SImpactGameplayMessage &Message, const SParticipantId &Target, bool LastCharacterContext, bool ConfirmedFall, const char *pReason)
	{
		if(pOutput == nullptr)
			return;
		SImpactPhase2Debug Debug;
		const int ClientId = Target.IsValid() ? Target.m_ClientId : Message.m_Victim;
		if(IsValidClientId(ClientId) && m_aObserved[ClientId].m_Valid)
			FillDebugFrame(Debug, m_aObserved[ClientId].m_Frame);
		Debug.m_DebugLabel = "HARM_DEATH_RESOLVE";
		Debug.m_ServerTick = Message.m_ServerTick;
		Debug.m_Target = Target;
		Debug.m_PotentialSaver = Message.m_Actor;
		Debug.m_Killer = Message.m_Killer;
		Debug.m_Weapon = Message.m_Weapon;
		Debug.m_ManualKill = Message.m_Weapon == WEAPON_SELF;
		Debug.m_Reset = Message.m_Weapon == WEAPON_GAME;
		// A server killer id closes the lifecycle, but it is not physical
		// trajectory evidence for a teammate-caused Fall.
		Debug.m_TeammateCause = false;
		Debug.m_ConfirmedFall = ConfirmedFall;
		Debug.m_ParticipantFound = IsValidClientId(ClientId) && m_aObserved[ClientId].m_Valid && m_aObserved[ClientId].m_Frame.m_Participant.IsValid();
		Debug.m_LastCharacterContext = LastCharacterContext;
		if(IsValidClientId(ClientId) && m_aObserved[ClientId].m_Valid)
		{
			Debug.m_HarmCause = m_aObserved[ClientId].m_Frame.m_HarmCause;
			Debug.m_HarmCausalConfidence = m_aObserved[ClientId].m_Frame.m_HarmCausalConfidence;
		}
		Debug.m_CausalConfidence = Message.m_CausalConfidence;
		Debug.m_Reason = pReason ? pReason : "server death resolution";
		for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
		{
			if(Episode.m_Target != Target)
				continue;
			// Death messages can arrive after the current character/roster entry has
			// disappeared. Preserve the participant and origin attribution captured
			// when this episode was created; a late sparse/dead frame must not turn a
			// valid historical Fall into an unbound/unknown death diagnostic.
			Debug.m_Started = Episode.m_TargetWasEligible;
			Debug.m_Eligible = Episode.m_TargetWasEligible;
			Debug.m_SelfOriginEvidence = Episode.m_OriginSelfOriginEvidence || Episode.m_HarmCause == EImpactHarmCause::SELF_CAUSED;
			Debug.m_ExternalCauseEvidence = Episode.m_OriginExternalCauseEvidence || Episode.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
			Debug.m_TeammateCause = Debug.m_ExternalCauseEvidence;
			Debug.m_HarmCause = Episode.m_HarmCause;
			Debug.m_HarmCausalConfidence = Episode.m_HarmCausalConfidence;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_RelatedFallEpisodeId = Episode.m_RelatedFallEpisodeId;
			Debug.m_RelatedRecoveryEpisodeId = Episode.m_RelatedRecoveryEpisodeId;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed != EImpactRecoveryNeed::UNKNOWN ?
				Episode.m_InitialRecoveryNeed : Episode.m_RecoveryNeed;
			Debug.m_FallAlreadyConfirmed = IsFallConfirmed(Episode);
			Debug.m_HookEvidence = Episode.m_HookEvidence;
			Debug.m_HammerEvidence = Episode.m_HammerEvidence;
			Debug.m_HookFirstTick = Episode.m_HookFirstTick;
			Debug.m_HookLastTick = Episode.m_HookLastTick;
			Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			Debug.m_PhysicalOutcome = Episode.m_PhysicalOutcome;
			break;
		}
		AddDebug(pOutput, Debug);
	}

	void CImpactPhase2::CancelPending(const SParticipantId &Target, SImpactPhase2Output *pOutput, const char *pReason, bool IncludeFall, bool IncludeRecovery)
	{
		for(size_t i = 0; i < m_vPendingEpisodes.size();)
		{
			SImpactPendingEpisode &Episode = m_vPendingEpisodes[i];
			const bool IsFallEpisode = Episode.m_Type == EImpactEventType::FALL_CONFIRMED;
			if(Episode.m_Target != Target || (IsFallEpisode && !IncludeFall) || (!IsFallEpisode && !IncludeRecovery))
			{
				++i;
				continue;
			}
			// A pending Fall is resolved only by an allowed harmful/recovery outcome.
			// Later reset, disconnect, teammate-recovery, or death handling may close
			// the retained context, but they must never erase or rewrite an already-
			// counted origin Fall.
			if(IsFallConfirmed(Episode))
			{
				++i;
				continue;
			}
			SImpactPhase2Debug Debug;
			FillDebugEpisode(Debug, Episode);
			Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
			Debug.m_EventType = Episode.m_Type;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_FrozenBefore = true;
			Debug.m_HookEvidence = Episode.m_HookEvidence;
			Debug.m_HammerEvidence = Episode.m_HammerEvidence;
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed != EImpactRecoveryNeed::UNKNOWN ?
				Episode.m_InitialRecoveryNeed : Episode.m_RecoveryNeed;
			Debug.m_HookFirstTick = Episode.m_HookFirstTick;
			Debug.m_HookLastTick = Episode.m_HookLastTick;
			Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
			Debug.m_EpisodeState = Episode.m_State;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			Debug.m_HarmCause = Episode.m_HarmCause;
			Debug.m_HarmCausalConfidence = Episode.m_HarmCausalConfidence;
			Debug.m_DebugLabel = Episode.m_Type == EImpactEventType::FALL_CONFIRMED ?
				(IsFallConfirmed(Episode) ? "FALL_CLEANUP" : "FALL_NOT_EMITTED") : "SAVE_REJECTED";
			Debug.m_Reason = pReason ? pReason : "pending episode cancelled";
			AddDebug(pOutput, Debug);
			m_vPendingEpisodes.erase(m_vPendingEpisodes.begin() + static_cast<ptrdiff_t>(i));
		}
	}

	void CImpactPhase2::ResetObserved()
	{
		m_aObserved.fill({});
		m_aFinishSignals.fill(false);
		m_aFinishTimeKnown.fill(false);
		m_aFinishTimeMillis.fill(-1);
		m_vPendingEpisodes.clear();
		m_RunId = 0;
		m_TeamId = 0;
		m_NextEpisodeId = 1;
		m_NextRecoveryProofId = 1;
		m_NextInteractionId = 1;
		m_LastMessageId = 0;
		m_FinishRequested = false;
		m_ClutchEpisodeActive = false;
		m_ClutchEpisodeConsumed = false;
		m_ClutchAwaitingNewCrisis = false;
		m_ClutchActor = {};
		m_ClutchArmedTick = -1;
		m_ClutchEpisodeId = 0;
		m_ClutchProvisionalObserved = false;
		m_ClutchProvisionalActor = {};
		m_ClutchProvisionalTick = -1;
		m_LastClutchEvaluationKey.clear();
	}

	void CImpactPhase2::Reset()
	{
		ResetObserved();
	}

	void CImpactPhase2::ResetTransientForBoundary()
	{
		// Keep the run/team identity and episode sequence, but discard all
		// pre-reset observations. The first post-reset snapshot is a fresh
		// authoritative baseline, never a synthetic movement segment from the old
		// lifecycle.
		m_aObserved.fill({});
		m_aFinishSignals.fill(false);
		m_aFinishTimeKnown.fill(false);
		m_aFinishTimeMillis.fill(-1);
		m_vPendingEpisodes.clear();
		m_FinishRequested = false;
		m_ClutchEpisodeActive = false;
		m_ClutchEpisodeConsumed = false;
		m_ClutchAwaitingNewCrisis = false;
		m_ClutchActor = {};
		m_ClutchArmedTick = -1;
		m_ClutchEpisodeId = 0;
		m_ClutchProvisionalObserved = false;
		m_ClutchProvisionalActor = {};
		m_ClutchProvisionalTick = -1;
		m_LastClutchEvaluationKey.clear();
	}

	bool CImpactPhase2::ProcessResetBoundaryMessages(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayMessage> &vMessages)
	{
		bool HasBoundary = false;
		std::vector<SImpactGameplayMessage> vSorted;
		for(const SImpactGameplayMessage &Message : vMessages)
		{
			if(Message.m_Confirmed && (Message.m_Type == EImpactGameplayMessageType::TEAM_RESET ||
				Message.m_Type == EImpactGameplayMessageType::TEAM_WIPE) &&
				(Message.m_TeamId == 0 || Message.m_TeamId == TeamId))
				vSorted.push_back(Message);
		}
		std::sort(vSorted.begin(), vSorted.end(), [](const SImpactGameplayMessage &Left, const SImpactGameplayMessage &Right) {
			if(Left.m_ServerTick != Right.m_ServerTick)
				return Left.m_ServerTick < Right.m_ServerTick;
			return Left.m_MessageId < Right.m_MessageId;
		});
		for(const SImpactGameplayMessage &Message : vSorted)
		{
			if(Message.m_MessageId != 0 && Message.m_MessageId <= m_LastMessageId)
				continue;
			if(Message.m_MessageId != 0)
				m_LastMessageId = std::max(m_LastMessageId, Message.m_MessageId);
			const int64_t Tick = Message.m_ServerTick >= 0 ? Message.m_ServerTick : ServerTick;
			for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
			{
				SImpactPhase2Debug Debug;
				FillDebugEpisode(Debug, Episode);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_EventType = Episode.m_Type;
				Debug.m_EpisodeId = Episode.m_EpisodeId;
				Debug.m_Target = Episode.m_Target;
				Debug.m_PotentialSaver = Episode.m_Actor;
				Debug.m_StartTick = Episode.m_StartTick;
				Debug.m_RestorationTick = Episode.m_RestoreTick;
				Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
				Debug.m_FrozenBefore = true;
				Debug.m_HookEvidence = Episode.m_HookEvidence;
				Debug.m_HammerEvidence = Episode.m_HammerEvidence;
				Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
				Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed != EImpactRecoveryNeed::UNKNOWN ?
					Episode.m_InitialRecoveryNeed : Episode.m_RecoveryNeed;
				Debug.m_HookFirstTick = Episode.m_HookFirstTick;
				Debug.m_HookLastTick = Episode.m_HookLastTick;
				Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
				Debug.m_EpisodeState = Episode.m_State;
				Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
				Debug.m_HarmCause = Episode.m_HarmCause;
				Debug.m_HarmCausalConfidence = Episode.m_HarmCausalConfidence;
				Debug.m_FallAlreadyConfirmed = IsFallConfirmed(Episode);
				Debug.m_ConfirmedFall = IsFallConfirmed(Episode);
				Debug.m_DebugLabel = Episode.m_Type == EImpactEventType::FALL_CONFIRMED ?
					(IsFallConfirmed(Episode) ? "FALL_CLEANUP" : "FALL_NOT_EMITTED") : "SAVE_REJECTED";
				Debug.m_Reason = IsFallConfirmed(Episode) ?
					"team reset/wipe closed context; already-confirmed Fall remains immutable" :
					"team reset/wipe before episode confirmation";
				AddDebug(pOutput, Debug);
			}
			m_vPendingEpisodes.clear();
			ResetTransientForBoundary();
			SImpactEvent Event = MakeEvent(Message.m_Type == EImpactGameplayMessageType::TEAM_RESET || Message.m_First >= 0 ?
				EImpactEventType::TEAM_RESET_CONFIRMED : EImpactEventType::WIPE_CONFIRMED, TeamId, {}, {}, Tick, Tick,
				Message.m_First >= 0 ? "server-confirmed team reset" : "server-confirmed team wipe");
			if(Event.m_Type == EImpactEventType::WIPE_CONFIRMED)
			{
				Event.m_RawDelta.m_Wipes = 1;
				Event.m_vTags.push_back("WIPE");
			}
			else
				Event.m_vTags.push_back("TEAM_RESET");
			if(pOutput)
				pOutput->m_vEvents.push_back(std::move(Event));
			HasBoundary = true;
		}
		return HasBoundary;
	}

	int64_t CImpactPhase2::ConfirmationTicks(int TickSpeed)
	{
		const int64_t Speed = std::max(1, TickSpeed);
		return (7 * Speed + 9) / 10;
	}

	void CImpactPhase2::TrackJumpResetEpisodes(const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		for(const SImpactGameplayFrame &Target : vFrames)
		{
			const int ClientId = Target.m_Participant.m_ClientId;
			if(!IsValidClientId(ClientId) || !Target.m_Participant.IsValid())
				continue;

			SObservedState &Observed = m_aObserved[ClientId];
			const bool SameIdentity = Observed.m_Valid && SameParticipant(Observed.m_Frame, Target);
			const bool HasPreviousFrame = SameIdentity && Observed.m_Frame.m_CurrentCharacterKnown;
			const bool CurrentHarmfulEpisode = Target.m_HarmCause == EImpactHarmCause::SELF_CAUSED ||
				(Target.m_HarmfulOutcomeCandidateKnown && Target.m_HarmfulOutcomeCandidate);
			auto ClearJumpResetEvidence = [&Observed]() {
				Observed.m_JumpResetShotSeen = false;
				Observed.m_JumpResetShotTick = -1;
				Observed.m_JumpResetStateBefore = EImpactJumpState::UNKNOWN;
				Observed.m_JumpResetActive = false;
				Observed.m_JumpResetFreezeStart = -1;
			};

			if(!SameIdentity)
				ClearJumpResetEvidence();

			// Jump Reset is a personal, eligible-participant classification. Missing
			// current/identity/team data is not evidence for either a reset or a Fall;
			// discard the partial candidate conservatively so it cannot cross a gap.
			if(!Target.m_Confirmed || !Target.m_Present || !Target.m_CurrentCharacterKnown || !Target.m_Active ||
				!Target.m_Eligible || !Target.m_AliveKnown || !Target.m_Alive || !Target.m_FreezeKnown ||
				!Target.m_SoloKnown || Target.m_Solo || !IsValidTeam(Target.m_TeamId) || Target.m_TeamId != m_TeamId)
			{
				ClearJumpResetEvidence();
				continue;
			}

			const bool FreezeEntry = Target.m_Frozen && HasPreviousFrame && Observed.m_Frame.m_FreezeKnown &&
				!Observed.m_Frame.m_Frozen;
			// A new authoritative freeze entry is a new reset episode. Likewise, a
			// changed authoritative freeze start cannot inherit the old episode's shot.
			if(Observed.m_JumpResetActive && FreezeEntry)
				ClearJumpResetEvidence();
			else if(Observed.m_JumpResetActive && Target.m_Frozen && Target.m_FreezeStart >= 0 &&
				Observed.m_JumpResetFreezeStart >= 0 && Target.m_FreezeStart != Observed.m_JumpResetFreezeStart)
				ClearJumpResetEvidence();

			// A known usable jump before the freeze disproves the no-jump
			// prerequisite. UNKNOWN does not disprove it and is never promoted to
			// unavailable.
			if(!Observed.m_JumpResetActive && Target.m_JumpState == EImpactJumpState::AVAILABLE)
				ClearJumpResetEvidence();

			// A shot may only start a reset episode before a harmful/fall episode is
			// already tracked. This is intentionally an episode-state check, not a
			// recent-shot timer: a panic shot after loss of control cannot acquire
			// reset meaning retroactively.
			if(!Observed.m_JumpResetActive && (CurrentHarmfulEpisode ||
				Target.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED))
			{
				Observed.m_JumpResetShotSeen = false;
				Observed.m_JumpResetShotTick = -1;
				Observed.m_JumpResetStateBefore = EImpactJumpState::UNKNOWN;
			}

			// The pending shot must be followed by the freeze entry in the next
			// reliable observation. If a confirmed non-frozen observation arrives
			// first, the reset context has ended; no arbitrary time window is needed.
			const bool CurrentDownwardShot = !Target.m_Frozen && Target.m_JumpState == EImpactJumpState::UNAVAILABLE &&
				Target.m_DownwardShotKnown && Target.m_DownwardShot;
			if(!Observed.m_JumpResetActive && Observed.m_JumpResetShotSeen && !Target.m_Frozen && !CurrentDownwardShot)
				ClearJumpResetEvidence();

			// AttackTick edge, aim and ownership have already been normalized by the
			// live adapter. No time, distance, velocity or observation-count threshold
			// is introduced here.
			if(!Observed.m_JumpResetActive && CurrentDownwardShot && !CurrentHarmfulEpisode &&
				Target.m_HarmCause != EImpactHarmCause::TEAMMATE_CAUSED)
			{
				Observed.m_JumpResetShotSeen = true;
				Observed.m_JumpResetShotTick = Target.m_ServerTick;
				Observed.m_JumpResetStateBefore = Target.m_JumpState;
			}

			if(!Observed.m_JumpResetActive && FreezeEntry && Observed.m_JumpResetShotSeen &&
				!CurrentHarmfulEpisode && Target.m_HarmCause != EImpactHarmCause::TEAMMATE_CAUSED &&
				Observed.m_JumpResetStateBefore == EImpactJumpState::UNAVAILABLE &&
				Target.m_SelfInitiatedFreezeKnown && Target.m_SelfInitiatedFreeze)
			{
				Observed.m_JumpResetActive = true;
				Observed.m_JumpResetFreezeStart = Target.m_FreezeStart;
			}
			else if(!Observed.m_JumpResetActive && FreezeEntry && Observed.m_JumpResetShotSeen)
			{
				// A freeze entry without an authoritative self-initiation proof is
				// deliberately unresolved. Do not carry its shot into a later
				// restoration or freeze episode as guessed intent.
				ClearJumpResetEvidence();
			}

			// The server-authoritative jump state becoming AVAILABLE after a known
			// frozen interval is the observable reset outcome. This is separate context
			// only: it neither infers a Fall nor suppresses the raw Fall decision for
			// the same participant/episode.
			if(Observed.m_JumpResetActive && !Target.m_Frozen && Target.m_JumpState == EImpactJumpState::AVAILABLE)
			{
				const int64_t StartTick = Observed.m_JumpResetShotTick >= 0 ? Observed.m_JumpResetShotTick :
					(Target.m_FreezeStart >= 0 ? Target.m_FreezeStart : Target.m_ServerTick);
				const char *pReason = "authoritative no-jump self-downward-shot freeze reset restored jump";
				SImpactEvent Event = MakeEvent(EImpactEventType::JUMP_RESET_CONFIRMED, m_TeamId,
					Target.m_Participant, Target.m_Participant, StartTick, Target.m_ServerTick, pReason);
				Event.m_BeforeContext = "server-confirmed jump unavailable before self downward shot and freeze";
				Event.m_AfterContext = "server-confirmed jump available after recovery";
				Event.m_vTags.push_back("JUMP_RESET");
				if(pOutput)
				{
					SImpactPhase2Debug Debug;
					FillDebugFrame(Debug, Target);
					Debug.m_DebugLabel = "JUMP_RESET_CONFIRMED";
					Debug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
					Debug.m_EventType = Event.m_Type;
					Debug.m_Target = Target.m_Participant;
					Debug.m_PotentialSaver = Target.m_Participant;
					Debug.m_StartTick = StartTick;
					Debug.m_RestorationTick = Target.m_ServerTick;
					Debug.m_RecoveryNeed = Target.m_RecoveryNeed;
					Debug.m_CausalConfidence = ECausalConfidence::CONFIRMED;
					Debug.m_Reason = pReason;
					AddDebug(pOutput, Debug);
				}
				pOutput->m_vEvents.push_back(std::move(Event));
				ClearJumpResetEvidence();
			}
		}
	}

	void CImpactPhase2::TrackSimpleProblemEpisodes(std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		std::array<bool, MAX_CLIENTS> aEvaluated{};
		for(SObservedState &Observed : m_aObserved)
			Observed.m_ProblemEpisodeStartedThisFrame = false;

		auto FindFallEpisode = [&](const SParticipantId &Target) -> SImpactPendingEpisode * {
			for(SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_Target == Target && Episode.m_TeamId == m_TeamId)
					return &Episode;
			return nullptr;
		};
		auto AddProblemTrace = [&](const SImpactGameplayFrame &Target, uint64_t EpisodeId, EImpactPhase2DebugStage Stage,
			bool ConfirmedFall, int64_t ProblemStartTick, const char *pReason) {
			if(pOutput == nullptr)
				return;
			SImpactPhase2Debug Debug;
			FillDebugFrame(Debug, Target);
			Debug.m_DebugLabel = Stage == EImpactPhase2DebugStage::CONFIRMED ? "FALL_CONFIRM" : "FALL_TRACE";
			Debug.m_Stage = Stage;
			Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
			Debug.m_EpisodeId = EpisodeId;
			Debug.m_Target = Target.m_Participant;
			Debug.m_TeamId = Target.m_TeamId;
			Debug.m_StartTick = ProblemStartTick >= 0 ? ProblemStartTick : Target.m_ServerTick;
			Debug.m_FrozenBefore = Target.m_Frozen;
			Debug.m_FrozenNow = Target.m_Frozen;
			Debug.m_ConfirmedFall = ConfirmedFall;
			Debug.m_FallAlreadyConfirmed = ConfirmedFall;
			Debug.m_TeammateCause = false;
			Debug.m_Reason = pReason ? pReason : "active problem episode";
			AddDebug(pOutput, Debug);
		};

		for(const SImpactGameplayFrame &Target : vFrames)
		{
			const int ClientId = Target.m_Participant.m_ClientId;
			if(!IsValidPhase2Target(Target, m_TeamId) || !IsValidClientId(ClientId) || aEvaluated[ClientId])
				continue;
			aEvaluated[ClientId] = true;

			SObservedState &Observed = m_aObserved[ClientId];
			const bool SameIdentity = Observed.m_Valid && SameParticipant(Observed.m_Frame, Target);
			const SImpactGameplayFrame *pPrevious = SameIdentity && Observed.m_Frame.m_CurrentCharacterKnown ?
				&Observed.m_Frame : nullptr;
			// An unknown character snapshot is not a new physical episode. Keep the
			// last confirmed frozen frame available for freeze-lifecycle comparison,
			// while position/trajectory predicates continue to use pPrevious only.
			const SImpactGameplayFrame *pFreezePrevious = pPrevious;
			if(pFreezePrevious == nullptr && SameIdentity && Observed.m_HasLastKnownFreeze)
				pFreezePrevious = &Observed.m_LastKnownFreezeFrame;
			const SImpactGameplayFrame *pBeforePrevious = Observed.m_HasPreviousFrame ? &Observed.m_PreviousFrame : nullptr;
			const bool EnteredProblem = Target.m_Frozen &&
				(pFreezePrevious == nullptr || !pFreezePrevious->m_FreezeKnown || !pFreezePrevious->m_Frozen);
			const bool NewProblemEpisode = Target.m_Frozen &&
				(!SameIdentity || Observed.m_ProblemStartTick < 0 || !Observed.m_HasLastKnownFreeze ||
					!Observed.m_LastKnownFreezeFrame.m_Frozen);
			// A target may leave a freeze and re-enter it before the existing 35-tick
			// confirmation boundary closes. If that first unfrozen boundary had no
			// attributable Save recovery, the episode remains open and the retained
			// last-known frozen frame intentionally makes NewProblemEpisode false. Do
			// not let the old restoration latch then suppress the next real rescue:
			// re-arm only this restoration boundary at the new authoritative freeze
			// entry. A pending Save recovery has its own refreeze/rearm path in
			// ResolvePending and is left untouched here.
			const bool HasPendingSaveRecovery = std::any_of(m_vPendingEpisodes.begin(), m_vPendingEpisodes.end(),
				[&Target, this](const SImpactPendingEpisode &Episode) {
					return Episode.m_Type == EImpactEventType::SAVE_CONFIRMED &&
						Episode.m_Target == Target.m_Participant && Episode.m_TeamId == m_TeamId;
				});
			if(EnteredProblem && Observed.m_RestorationObserved && !HasPendingSaveRecovery)
			{
				Observed.m_RestorationObserved = false;
				Observed.m_RestorationTick = -1;
			}
			// The current frame is the only baseline that can be used for a new
			// problem. An older safe platform is not carried over after the target
			// has left it and later enters a different frozen state.
			const bool CurrentAutonomousSafe = CanSelfRecover(Target, pPrevious);
			// A stable platform predicate is useful physical context, but it is not
			// by itself a positive autonomous outcome. In particular, the native
			// shadow evaluator can have inspected the same supported frames and
			// returned UNRESOLVED at its boundary. Do not promote that unresolved
			// observation into an episode-wide SELF_SAFE latch.
			const bool CurrentPositiveAutonomousSafe = CurrentAutonomousSafe &&
				HasPositiveAutonomousSelfSafeFact(Target);
			const bool FreezeRefreshObserved = HasFreezeRefreshBetweenFrames(Target, pFreezePrevious);
			const bool HasCurrentTargetSpecificAction = std::any_of(vFrames.begin(), vFrames.end(),
				[&Target, ClientId, this](const SImpactGameplayFrame &Actor) {
					if(!IsValidPhase2Target(Actor, m_TeamId) || Actor.m_Participant == Target.m_Participant)
						return false;
					return HasHookEvidence(Actor, ClientId) ||
						(Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence);
				});
			if(NewProblemEpisode)
			{
				Observed.m_ProblemStartTick = Target.m_FreezeStart >= 0 ? Target.m_FreezeStart : Target.m_ServerTick;
				Observed.m_ProblemEpisodeStartedThisFrame = true;
				AddEngagementProof(vFrames, Target.m_Participant, Target.m_ServerTick, "PHASE2_PROBLEM_EPISODE", pOutput);
				Observed.m_ProblemOriginTeammateCaused = false;
				Observed.m_AutonomousSelfRecoveryProven = false;
				Observed.m_AutonomousSelfRecoveryEpisodeId = 0;
				Observed.m_AutonomousSelfRecoverySourceTick = -1;
				Observed.m_AutonomousSelfRecoveryNativeProof = false;
				Observed.m_NativeAutonomousResultObserved = false;
				Observed.m_NativeAutonomousResultSelfSafe = false;
				Observed.m_NativeAutonomousResultTick = -1;
				Observed.m_AutonomousHarmfulEvidenceProven = false;
				Observed.m_SelfSafeInvalidatedTick = -1;
				Observed.m_PostInvalidationHarmfulEvidenceTick = -1;
				Observed.m_ExternalInteractionOutcomeUnavailable = false;
				Observed.m_vFrozenInteractions.clear();
				Observed.m_EpisodeRecoveryNeed = Target.m_RecoveryNeed;
				Observed.m_RestorationObserved = false;
				Observed.m_RestorationTick = -1;
			}
			if(Target.m_Frozen && Observed.m_vFrozenInteractions.empty() &&
				((Target.m_AutonomousRecoveryKnown && !Target.m_AutonomousRecoveryPossible) ||
					(Target.m_HarmfulOutcomeCandidateKnown && Target.m_HarmfulOutcomeCandidate)))
			{
				Observed.m_AutonomousHarmfulEvidenceProven = true;
			}
			if(Target.m_Frozen && HasCurrentTargetSpecificAction && !CurrentPositiveAutonomousSafe &&
				Target.m_SupportKnown && Target.m_Supported && Target.m_SafeCollisionPositionKnown &&
				Target.m_SafeCollisionPosition &&
				(pPrevious == nullptr || !HasSafePlatformContact(*pPrevious)))
			{
				Observed.m_ExternalInteractionOutcomeUnavailable = true;
			}

			if(!Target.m_Frozen)
			{
				// The later unfrozen state is an outcome of the episode, not a
				// pre-action autonomous-safe baseline. Do not latch it as if the
				// target had been safe before a teammate interaction; that would turn
				// post-action SAFE into self-recovery and suppress the independent
				// Save decision.
				continue;
			}
			if(Observed.m_ProblemStartTick < 0)
				continue;
			// Teammate causation is episode context, not a terminal victim-Fall
			// veto. Keep processing the same physical problem so later canonical
			// harmful evidence (including an authoritative freeze refresh) can
			// confirm the victim Fall. Save tracking remains independent below.

			SImpactPendingEpisode *pEpisode = FindFallEpisode(Target.m_Participant);
			if(pEpisode == nullptr)
			{
				if(m_vPendingEpisodes.size() >= MAX_PENDING_EPISODES)
					continue;
				SImpactPendingEpisode Episode;
				Episode.m_EpisodeId = m_NextEpisodeId++;
				Episode.m_Type = EImpactEventType::FALL_CONFIRMED;
				Episode.m_Actor = Target.m_Participant;
				Episode.m_Target = Target.m_Participant;
				Episode.m_TeamId = Target.m_TeamId;
				Episode.m_StartTick = Observed.m_ProblemStartTick;
				Episode.m_State = EImpactEpisodeState::NONE;
				Episode.m_TargetAlive = Target.m_Alive;
				Episode.m_TargetWasEligible = Target.m_Eligible;
				// Preserve origin context as passive episode metadata. These fields are
				// diagnostics/persistence only; raw Fall is still decided exclusively by
				// the current physical problem and never by RecoveryNeed or HarmCause.
				Episode.m_RecoveryNeed = Target.m_RecoveryNeed;
				Episode.m_InitialRecoveryNeed = Target.m_RecoveryNeed;
				Episode.m_HarmCause = Target.m_HarmCause;
				Episode.m_HarmCausalConfidence = Target.m_HarmCausalConfidence;
				Episode.m_OriginSelfOriginEvidence = Target.m_HarmCause == EImpactHarmCause::SELF_CAUSED;
				Episode.m_OriginExternalCauseEvidence = Target.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
				Episode.m_BeforeContext = "authoritative problem episode";
				Episode.m_DebugReason = "entry frame creates an active problem episode";
				m_vPendingEpisodes.push_back(std::move(Episode));
				pEpisode = &m_vPendingEpisodes.back();
				AddProblemTrace(Target, pEpisode->m_EpisodeId, EImpactPhase2DebugStage::CANDIDATE, false,
					Observed.m_ProblemStartTick,
					EnteredProblem ? "problem entry; no Fall on the entry frame" : "active problem episode created");
			}

			if(IsFallConfirmed(*pEpisode))
				continue;

			// A supported-frame proof is only valid while the authoritative freeze
			// deadline remains the same. A later refresh at the same apparent platform
			// is positive evidence that the location is an active freeze source, not a
			// natural-expiry platform. Invalidate a weak platform-only episode proof
			// even if an interaction has already been observed; only a positive native
			// autonomous proof is immutable here.
			if(FreezeRefreshObserved && !CurrentAutonomousSafe && Observed.m_AutonomousSelfRecoveryProven &&
				!Observed.m_AutonomousSelfRecoveryNativeProof)
			{
				Observed.m_AutonomousSelfRecoveryProven = false;
				Observed.m_AutonomousSelfRecoveryEpisodeId = 0;
				Observed.m_AutonomousSelfRecoverySourceTick = -1;
				Observed.m_AutonomousSelfRecoveryNativeProof = false;
				Observed.m_NativeAutonomousResultObserved = false;
				Observed.m_NativeAutonomousResultSelfSafe = false;
				Observed.m_NativeAutonomousResultTick = -1;
			}

			// Only an explicit positive autonomous fact may latch self-recovery. The
			// stable-platform predicate is physical context for the current frame, but
			// supported frames with a native UNRESOLVED result must not become an
			// episode-wide SELF_SAFE latch. Once a native proof exists for this exact
			// episode, a later hook cannot turn the original self-safe episode into a
			// Fall or make the unnecessary action a Save.
			const bool PreActionSelfSafe = Observed.m_vFrozenInteractions.empty() && HasCurrentTargetSpecificAction &&
				WasSelfRecoverableImmediatelyBeforeInteraction(Target, pPrevious, pBeforePrevious);
			const bool PreActionPositiveAutonomousSafe = PreActionSelfSafe && pPrevious != nullptr &&
				HasPositiveAutonomousSelfSafeFact(*pPrevious);
			if(!Observed.m_AutonomousSelfRecoveryProven &&
				((!HasCurrentTargetSpecificAction && Observed.m_vFrozenInteractions.empty() && CurrentPositiveAutonomousSafe) ||
					PreActionPositiveAutonomousSafe))
			{
				Observed.m_AutonomousSelfRecoveryProven = true;
				Observed.m_AutonomousSelfRecoveryEpisodeId = pEpisode->m_EpisodeId;
				Observed.m_AutonomousSelfRecoverySourceTick =
					PreActionSelfSafe && pPrevious != nullptr ? pPrevious->m_ServerTick : Target.m_ServerTick;
				// This separate marker is used only by Save attribution. The existing
				// Fall-side proof may still retain the older platform predicate, but a
				// Save veto requires a positive native/autonomous fact rather than the
				// platform predicate alone.
				Observed.m_AutonomousSelfRecoveryNativeProof =
					(!HasCurrentTargetSpecificAction && HasPositiveAutonomousSelfSafeFact(Target)) ||
					(PreActionSelfSafe && pPrevious != nullptr && HasPositiveAutonomousSelfSafeFact(*pPrevious));
				if(pOutput != nullptr)
				{
					SImpactPhase2Debug Debug;
					FillDebugFrame(Debug, Target);
					Debug.m_DebugLabel = "SELF_SAFE_PROVEN";
					Debug.m_EpisodeId = pEpisode->m_EpisodeId;
					Debug.m_ProblemEpisodeId = pEpisode->m_EpisodeId;
					Debug.m_Target = Target.m_Participant;
					Debug.m_TeamId = Target.m_TeamId;
					Debug.m_SelfSafeEvidenceTick = Observed.m_AutonomousSelfRecoverySourceTick;
					Debug.m_AutonomousSelfRecoveryProven = true;
					Debug.m_AutonomousRecoveryEvaluated = Target.m_AutonomousRecoveryEvaluated;
					Debug.m_AutonomousRecoveryKnown = Target.m_AutonomousRecoveryKnown;
					Debug.m_AutonomousRecoveryPossible = Target.m_AutonomousRecoveryPossible;
					Debug.m_NativePreActionResult = "SELF_SAFE";
					Debug.m_Reason = "positive autonomous recovery proof latched for current participant generation and problem episode";
					AddDebug(pOutput, Debug);
				}
			}
			// RECOVERED_SELF is a result for the physical state that was observed at
			// that point in this episode, not a permanent immunity. If the target has
			// subsequently left that autonomous-safe state before any rescue action,
			// resume observing the same problem episode. This is the structural
			// distinction between "was safe earlier" and "is safe immediately before
			// the current rescue"; no elapsed-tick expiry is involved.
			if(pEpisode->m_State == EImpactEpisodeState::RECOVERED_SELF &&
				!CurrentAutonomousSafe && Observed.m_vFrozenInteractions.empty())
			{
				const int64_t InvalidatedSelfSafeEvidenceTick = Observed.m_AutonomousSelfRecoverySourceTick >= 0 ?
					Observed.m_AutonomousSelfRecoverySourceTick : Observed.m_NativeAutonomousResultTick;
				const EImpactEpisodeState PreviousEpisodeState = pEpisode->m_State;
				pEpisode->m_State = EImpactEpisodeState::NONE;
				pEpisode->m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
				pEpisode->m_DebugReason = "autonomous-safe state ended; problem episode remains active";
				// The episode result above is state-specific, not a permanent veto.
				// Clear the episode-scoped proof at the same boundary as RECOVERED_SELF;
				// otherwise its stale latch would continue suppressing an independent
				// harmful Fall after the target became non-self-safe again.
				if(Observed.m_AutonomousSelfRecoveryEpisodeId == pEpisode->m_EpisodeId)
				{
					Observed.m_AutonomousSelfRecoveryProven = false;
					Observed.m_AutonomousSelfRecoveryEpisodeId = 0;
					Observed.m_AutonomousSelfRecoverySourceTick = -1;
					Observed.m_AutonomousSelfRecoveryNativeProof = false;
				}
				// Record the exact state boundary. Clearing the SELF_SAFE latch must
				// not let ordinary frozen/unsafe snapshot context immediately become a
				// Fall; only a later positive harmful result can reopen that decision.
				Observed.m_SelfSafeInvalidatedTick = Target.m_ServerTick;
				Observed.m_PostInvalidationHarmfulEvidenceTick = -1;
				if(Observed.m_NativeAutonomousResultSelfSafe &&
					Observed.m_NativeAutonomousResultTick >= Observed.m_ProblemStartTick &&
					Observed.m_NativeAutonomousResultTick <= Target.m_ServerTick)
				{
					Observed.m_NativeAutonomousResultObserved = false;
					Observed.m_NativeAutonomousResultSelfSafe = false;
					Observed.m_NativeAutonomousResultTick = -1;
				}
				if(pOutput != nullptr)
				{
					SImpactPhase2Debug Debug;
					FillDebugFrame(Debug, Target);
					Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
					Debug.m_DebugLabel = "SELF_SAFE_INVALIDATED";
					Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
					Debug.m_EpisodeId = pEpisode->m_EpisodeId;
					Debug.m_ProblemEpisodeId = pEpisode->m_EpisodeId;
					Debug.m_Target = Target.m_Participant;
					Debug.m_TeamId = Target.m_TeamId;
					Debug.m_StartTick = Observed.m_ProblemStartTick;
					Debug.m_ServerTick = Target.m_ServerTick;
					Debug.m_PhysicalOutcome = EvaluatePhysicalOutcome(Target);
					Debug.m_AutonomousRecoveryKnown = Target.m_AutonomousRecoveryKnown;
					Debug.m_AutonomousRecoveryPossible = Target.m_AutonomousRecoveryPossible;
					Debug.m_AutonomousRecoveryEvaluated = Target.m_AutonomousRecoveryEvaluated;
					Debug.m_AutonomousSelfRecoveryProven = false;
					Debug.m_SelfSafeEvidenceTick = InvalidatedSelfSafeEvidenceTick;
					Debug.m_EpisodeState = EImpactEpisodeState::NONE;
					Debug.m_EpisodeStateBefore = PreviousEpisodeState;
					Debug.m_EpisodeStateAfter = EImpactEpisodeState::NONE;
					Debug.m_PreActionSafeKnown = true;
					Debug.m_PreActionSafe = true;
					Debug.m_PreActionSafeSource = "SAME_EPISODE_STATE_EXPIRED";
					Debug.m_NativePreActionResult = "SELF_SAFE_EXPIRED";
					Debug.m_Reason = "SELF_SAFE proof invalidated; Fall remains neutral until fresh positive autonomous harmful evidence";
					AddDebug(pOutput, Debug);
				}
			}
			const bool EpisodeAutonomousSelfRecoveryProven = Observed.m_AutonomousSelfRecoveryProven &&
				Observed.m_AutonomousSelfRecoveryEpisodeId == pEpisode->m_EpisodeId;

			// A pre-freeze teammate cause is only accepted when the actor's
			// target-specific hook/hammer evidence coincides with an observed target
			// movement change. Later rescue interaction is deliberately not examined
			// in this origin decision.
			bool TeammateCaused = Target.m_HarmCause == EImpactHarmCause::TEAMMATE_CAUSED;
			if(EnteredProblem && pPrevious != nullptr)
			{
				for(const SImpactGameplayFrame &Actor : vFrames)
				{
					if(!IsValidPhase2Target(Actor, m_TeamId) || Actor.m_Participant == Target.m_Participant ||
						Actor.m_TeamId != Target.m_TeamId)
						continue;
					const bool CurrentHook = HasHookEvidence(Actor, ClientId);
					const SImpactGameplayFrame *pPreviousActor = nullptr;
					if(IsValidClientId(Actor.m_Participant.m_ClientId))
					{
						const SObservedState &ActorObserved = m_aObserved[Actor.m_Participant.m_ClientId];
						if(ActorObserved.m_Valid && SameParticipant(ActorObserved.m_Frame, Actor))
							pPreviousActor = &ActorObserved.m_Frame;
					}
					const bool PreviousHook = pPreviousActor != nullptr && HasHookEvidence(*pPreviousActor, ClientId);
					const bool CurrentHammer = Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence;
					if((CurrentHook || PreviousHook || CurrentHammer) &&
						HasObservedMovementChange(*pPrevious, Target))
					{
						TeammateCaused = true;
						break;
					}
				}
			}
			if(TeammateCaused)
			{
				if(!Observed.m_ProblemOriginTeammateCaused)
				{
					Observed.m_ProblemOriginTeammateCaused = true;
					pEpisode->m_OriginExternalCauseEvidence = true;
					pEpisode->m_HarmCause = EImpactHarmCause::TEAMMATE_CAUSED;
					pEpisode->m_HarmCausalConfidence = ECausalConfidence::CONFIRMED;
					pEpisode->m_DebugReason = "teammate-caused context; Fall remains independently resolvable";
				}
			}

			// A positive autonomous fact belongs to the target only when no
			// teammate interaction has been recorded for this problem episode. A
			// post-rescue SAFE/current platform must never rewrite the origin.
			// TrackSimpleProblemEpisodes runs before the interaction recorder. A hook
			// visible in this same authoritative batch may already have changed the
			// target, so the current safe position cannot be called pre-action safe
			// until we have excluded a target-specific action from this frame.
			const bool AlreadySafeWithoutTeammate = Observed.m_vFrozenInteractions.empty() &&
				!HasCurrentTargetSpecificAction && CurrentPositiveAutonomousSafe;
			if(AlreadySafeWithoutTeammate)
			{
				// Preserve the positive autonomous fact for the episode boundary. A
				// later generic SAFE/unfrozen frame must not be used to manufacture this
				// fact, and teammate interaction is explicitly excluded above.
				if(pEpisode->m_State != EImpactEpisodeState::RECOVERED_SELF)
				{
					pEpisode->m_State = EImpactEpisodeState::RECOVERED_SELF;
					pEpisode->m_DebugReason = "NO_FALL_SELF_SAFE";
					AddProblemTrace(Target, pEpisode->m_EpisodeId, EImpactPhase2DebugStage::CANCELLED, false,
						Observed.m_ProblemStartTick,
						"NO_FALL_SELF_SAFE");
				}
				continue;
			}

			// Entry is observation only. On the next authoritative frame for this same
			// target, the active problem is evaluated independently of Save/rescue. A
			// positive autonomous fact keeps the episode at Fall 0; otherwise a current
			// authoritative helplessness fact establishes the raw Fall. This is
			// target-routed and does not read PhysicalOutcome, RecoveryNeed, or any Save
			// record. The entry frame itself never reaches this branch.
			const bool SubsequentProblemFrame = !EnteredProblem && pFreezePrevious != nullptr &&
				pFreezePrevious->m_Frozen && Target.m_Frozen;
			// FreezeStart/FreezeEnd are authoritative lifecycle facts. Once they
			// advance within the same frozen episode, an old/stale position sample or
			// an unresolved shadow result must not erase the existing active-freeze
			// proof. A positive autonomous proof remains the only veto.
			const bool FreezeLifecycleOutlivedPhysicalFrame =
				(pPrevious == nullptr && pFreezePrevious != nullptr) || Target.m_AuthoritativePhysicalStateStale;
			const bool RefreshProvesActiveFreezeSource =
				!NewProblemEpisode && FreezeRefreshObserved && Target.m_Frozen &&
				((Target.m_SupportKnown && Target.m_Supported && Target.m_SafeCollisionPositionKnown &&
					Target.m_SafeCollisionPosition) || FreezeLifecycleOutlivedPhysicalFrame);
			const bool TargetSpecificInteractionObserved = HasCurrentTargetSpecificAction || !Observed.m_vFrozenInteractions.empty();
			const bool PreHelperAutonomousHarmfulProof = Observed.m_AutonomousHarmfulEvidenceProven ||
				(Observed.m_vFrozenInteractions.empty() && pPrevious != nullptr &&
					pPrevious->m_AutonomousRecoveryKnown && !pPrevious->m_AutonomousRecoveryPossible);
			const bool CurrentPositiveHarmfulEvidence =
				(Target.m_AutonomousRecoveryKnown && !Target.m_AutonomousRecoveryPossible) ||
				(Target.m_HarmfulOutcomeCandidateKnown && Target.m_HarmfulOutcomeCandidate) ||
				(!Target.m_DeepFrozen && Target.m_Frozen && Target.m_FreezeEnd > 0 &&
					Target.m_ServerTick >= Target.m_FreezeEnd);
			// A separate positive result from the same authoritative frame is valid
			// evidence too; SELF_SAFE invalidation itself never sets this predicate.
			if(Observed.m_SelfSafeInvalidatedTick >= Observed.m_ProblemStartTick &&
				Target.m_ServerTick >= Observed.m_SelfSafeInvalidatedTick && CurrentPositiveHarmfulEvidence &&
				Observed.m_PostInvalidationHarmfulEvidenceTick < 0)
			{
				Observed.m_PostInvalidationHarmfulEvidenceTick = Target.m_ServerTick;
			}
			const bool UnresolvedAutonomousEvaluationSupersededByInteraction =
				TargetSpecificInteractionObserved &&
				(Observed.m_NativeAutonomousResultObserved || Observed.m_ExternalInteractionOutcomeUnavailable) &&
				!PreHelperAutonomousHarmfulProof && !CurrentPositiveHarmfulEvidence;
			const bool CurrentHelplessEvidence = HasCurrentHelplessPhysicalEvidence(Target);
			const bool PostInteractionFreezeRefreshEvidence = TargetSpecificInteractionObserved &&
				FreezeRefreshObserved && CurrentHelplessEvidence;
			const bool CurrentProblemHasFallEvidence = RefreshProvesActiveFreezeSource || PostInteractionFreezeRefreshEvidence ||
				(CurrentHelplessEvidence && !UnresolvedAutonomousEvaluationSupersededByInteraction);
			const bool FreshHarmfulProofAfterInvalidation = Observed.m_SelfSafeInvalidatedTick < Observed.m_ProblemStartTick ||
				Observed.m_PostInvalidationHarmfulEvidenceTick >= Observed.m_SelfSafeInvalidatedTick;
			const bool FallCandidateWithoutSelfSafeVeto = SubsequentProblemFrame && CurrentProblemHasFallEvidence &&
				!CurrentAutonomousSafe && FreshHarmfulProofAfterInvalidation;
			if(FallCandidateWithoutSelfSafeVeto && EpisodeAutonomousSelfRecoveryProven)
			{
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "FALL_NOT_EMITTED";
				Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
				Debug.m_EpisodeId = pEpisode->m_EpisodeId;
				Debug.m_Target = Target.m_Participant;
				Debug.m_TeamId = Target.m_TeamId;
				Debug.m_StartTick = Observed.m_ProblemStartTick;
				Debug.m_ServerTick = Target.m_ServerTick;
				Debug.m_EpisodeState = pEpisode->m_State;
				Debug.m_PhysicalOutcome = EImpactPhysicalOutcome::HARMFUL;
				Debug.m_AutonomousRecoveryKnown = Target.m_AutonomousRecoveryKnown;
				Debug.m_AutonomousRecoveryPossible = Target.m_AutonomousRecoveryPossible;
				Debug.m_AutonomousRecoveryEvaluated = Target.m_AutonomousRecoveryEvaluated;
				Debug.m_AutonomousSelfRecoveryProven = true;
				Debug.m_SelfSafeEvidenceTick = Observed.m_AutonomousSelfRecoverySourceTick;
				Debug.m_FallSuppressed = true;
				Debug.m_PreActionSafeKnown = true;
				Debug.m_PreActionSafe = true;
				Debug.m_PreActionSafeSource = "CURRENT_PROBLEM_EPISODE_LATCH";
				Debug.m_NativePreActionResult = "SELF_SAFE";
				for(const SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
				{
					if(Interaction.m_Target != Target.m_Participant || !Interaction.m_PhysicalChangeObserved)
						continue;
					if(Debug.m_FirstPhysicalChangeTick < 0 ||
						Interaction.m_FirstPhysicalChangeTick < Debug.m_FirstPhysicalChangeTick)
					{
						Debug.m_FirstPhysicalChangeTick = Interaction.m_FirstPhysicalChangeTick;
						Debug.m_PotentialSaver = Interaction.m_Actor;
					}
				}
			Debug.m_Reason = "harmful Fall evidence was blocked by current problem-episode SELF_SAFE latch";
			AddDebug(pOutput, Debug);
			}
			if(FallCandidateWithoutSelfSafeVeto && !EpisodeAutonomousSelfRecoveryProven)
			{
				pEpisode->m_PhysicalOutcome = EImpactPhysicalOutcome::HARMFUL;
				pEpisode->m_CausalConfidence = ECausalConfidence::CONFIRMED;
				const char *pFallReason = RefreshProvesActiveFreezeSource ?
					"FALL: authoritative freeze refresh proves active freeze source" :
					"FALL: subsequent frozen problem has no autonomous recovery fact";
				pEpisode->m_DebugReason = pFallReason;
				ConfirmFallEpisode(pEpisode->m_EpisodeId, Target.m_ServerTick, m_TeamId, pOutput,
					pFallReason, true, true, &Target);
			}
		}
	}

	void CImpactPhase2::RecordSimpleInteractions(int TickSpeed, std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		(void)TickSpeed;
		auto IsValidActor = [&](const SImpactGameplayFrame &Frame) {
			return Frame.m_Confirmed && Frame.m_Present && Frame.m_CurrentCharacterKnown && Frame.m_Active &&
				Frame.m_Eligible && Frame.m_AliveKnown && Frame.m_Alive && Frame.m_Participant.IsValid() &&
				Frame.m_TeamId == m_TeamId && Frame.m_SoloKnown && !Frame.m_Solo &&
				IsValidClientId(Frame.m_Participant.m_ClientId);
		};
		auto HasSafeRecoveryFact = [](const SImpactGameplayFrame &Frame) {
			return HasObservedRecoveryFact(Frame);
		};

		for(const SImpactGameplayFrame &Target : vFrames)
		{
			if(!IsValidPhase2Target(Target, m_TeamId))
				continue;
			SObservedState &Observed = m_aObserved[Target.m_Participant.m_ClientId];
			if(Observed.m_ProblemStartTick < 0)
				continue;
			// DetectSimpleRestorations owns the first unfrozen boundary. Once that
			// boundary has been observed, later unfrozen frames are confirmation
			// history, not new target-specific interactions or new restoration causes.
			if(!Target.m_Frozen && Observed.m_RestorationObserved)
				continue;
			const bool SameIdentity = Observed.m_Valid && SameParticipant(Observed.m_Frame, Target);
			const SImpactGameplayFrame *pPrevious = SameIdentity && Observed.m_Frame.m_CurrentCharacterKnown ?
				&Observed.m_Frame : nullptr;
			const SImpactGameplayFrame *pBeforePrevious = Observed.m_HasPreviousFrame ? &Observed.m_PreviousFrame : nullptr;
			bool InteractionChanged = false;

			// Preserve the final physical delta before closing a released span, but
			// defer assigning its safe-transition proof until active actor-specific
			// evidence for this frame has been processed below.
			for(SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
			{
				const SImpactGameplayFrame *pActor = FindParticipant(vFrames, Interaction.m_Actor);
				const bool HookActive = pActor != nullptr && HasHookEvidence(*pActor, Target.m_Participant.m_ClientId);
				if(!HookActive && Interaction.m_ActionActive)
				{
					const bool WasActionActive = Interaction.m_ActionActive;
					ObserveInteractionPhysicalEvidence(Interaction, Target, true, -1, false);
					if(!Interaction.m_vHookSpans.empty())
						CloseHookSpan(Observed.m_vFrozenInteractions, Interaction.m_Actor, Interaction.m_Target, Target.m_ServerTick);
					else
					{
						Interaction.m_ActionActive = false;
						Interaction.m_TrackPostActionOutcome = true;
					}
					InteractionChanged |= WasActionActive && !Interaction.m_ActionActive;
				}
			}

			for(const SImpactGameplayFrame &Actor : vFrames)
			{
				// An interaction after restoration belongs to a future problem, not to
				// the recovery that already restored this target. Same-tick actions are
				// still handled by DetectSimpleRestorations below.
				if(!Target.m_Frozen)
					break;
				if(!IsValidActor(Actor) || Actor.m_Participant == Target.m_Participant)
					continue;
				const bool HookEvidence = HasHookEvidence(Actor, Target.m_Participant.m_ClientId);
				const bool HammerEvidence = Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence;
				if(!HookEvidence && !HammerEvidence)
				{
					if(Actor.m_HookKnown)
						CloseHookSpan(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant, Target.m_ServerTick);
					continue;
				}

				SCausalInteraction *pExisting = FindInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant);
				const bool FirstTargetAction = Observed.m_vFrozenInteractions.empty();
				const bool NewInteraction = pExisting == nullptr;
				// Save attribution is intentionally stricter than the legacy Fall-side
				// platform predicate. A platform observed before the action is useful
				// causality metadata, but it is not proof that natural unfreeze would
				// succeed there. Only an episode-scoped positive native result (or an
				// explicit positive autonomous fact on the clean pre-action frame) may
				// veto a Save. Native UNRESOLVED and platform-only evidence stay false.
				const bool PreActionNativeSelfSafe =
					Observed.m_AutonomousSelfRecoveryNativeProof ||
					(Observed.m_NativeAutonomousResultObserved && Observed.m_NativeAutonomousResultSelfSafe) ||
					(pPrevious != nullptr && HasPositiveAutonomousSelfSafeFact(*pPrevious));
				const bool PreActionAutonomousSafe = FirstTargetAction && PreActionNativeSelfSafe;
				if(FirstTargetAction && PreActionAutonomousSafe && !Observed.m_AutonomousSelfRecoveryProven)
				{
					for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
					{
						if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
							Episode.m_Target == Target.m_Participant && Episode.m_TeamId == m_TeamId &&
							Episode.m_StartTick == Observed.m_ProblemStartTick && !IsFallConfirmed(Episode))
						{
							Observed.m_AutonomousSelfRecoveryProven = true;
							Observed.m_AutonomousSelfRecoveryEpisodeId = Episode.m_EpisodeId;
							Observed.m_AutonomousSelfRecoverySourceTick = pPrevious != nullptr ? pPrevious->m_ServerTick : Target.m_ServerTick;
							if(pOutput != nullptr)
							{
								SImpactPhase2Debug Debug;
								FillDebugFrame(Debug, Target);
								Debug.m_DebugLabel = "SELF_SAFE_PROVEN";
								Debug.m_EpisodeId = Episode.m_EpisodeId;
								Debug.m_ProblemEpisodeId = Episode.m_EpisodeId;
								Debug.m_Target = Target.m_Participant;
								Debug.m_TeamId = Target.m_TeamId;
								Debug.m_SelfSafeEvidenceTick = Observed.m_AutonomousSelfRecoverySourceTick;
								Debug.m_AutonomousSelfRecoveryProven = true;
								Debug.m_NativePreActionResult = "SELF_SAFE";
								Debug.m_Reason = "positive pre-action autonomous recovery proof latched for current problem episode";
								AddDebug(pOutput, Debug);
							}
							break;
						}
					}
				}
				AddInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant,
					HammerEvidence, HookEvidence,
					HookEvidence && Actor.m_MeaningfulHookSupportKnown && Actor.m_MeaningfulHookSupport,
					Actor.m_HammerTargetEvidenceKnown, PreActionAutonomousSafe, Target.m_ServerTick, Target);
				SCausalInteraction *pInteraction = FindInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant);
				if(pInteraction == nullptr)
					continue;
				if(NewInteraction)
				{
					AddEngagementProof(vFrames, Actor.m_Participant, Target.m_ServerTick, "PHASE2_TARGET_INTERACTION", pOutput);
					AddEngagementProof(vFrames, Target.m_Participant, Target.m_ServerTick, "PHASE2_TARGET_INTERACTION", pOutput);
				}
				if(NewInteraction && pInteraction->m_InteractionId == 0)
					pInteraction->m_InteractionId = m_NextInteractionId++;

				// AddInteraction starts from the current frame because that is where
				// the action is observed. Replace only the first action's baseline
				// with the immediately preceding authoritative target frame; later
				// rehooks must not rewrite it.
				if(NewInteraction)
				{
					if(Target.m_PreActionAutonomousSelfSafeTick >= Observed.m_ProblemStartTick &&
						Target.m_PreActionAutonomousSelfSafeTick < Target.m_ServerTick)
						pInteraction->m_PreActionAutonomousSelfSafeTick = Target.m_PreActionAutonomousSelfSafeTick;
					pInteraction->m_PreActionAutonomousSafe = PreActionAutonomousSafe;
					pInteraction->m_PreActionNativeEvaluated = Observed.m_NativeAutonomousResultObserved;
					pInteraction->m_PreActionNativeSelfSafe = PreActionNativeSelfSafe;
					pInteraction->m_PreActionPlatformPreexisted = pPrevious != nullptr && HasSafePlatformContact(*pPrevious);
					pInteraction->m_PreActionFreezeRefreshObserved =
						HasFreezeRefreshBetweenFrames(pPrevious != nullptr ? *pPrevious : Target, pBeforePrevious);
					// An interaction observed on the same frame that established a
					// teammate-caused freeze belongs to the origin explanation, not to
					// a later rescue. Preserve it for diagnostics, but never let it
					// become the Save actor for the resulting restoration.
					if(Observed.m_ProblemEpisodeStartedThisFrame && Observed.m_ProblemOriginTeammateCaused)
						pInteraction->m_RelevantToThisFreezeEpisode = false;
					pInteraction->m_PreActionSafeCollisionPositionKnown = PreActionAutonomousSafe &&
						pPrevious != nullptr && pPrevious->m_SafeCollisionPositionKnown;
					pInteraction->m_PreActionSafeCollisionPosition = pInteraction->m_PreActionSafeCollisionPositionKnown &&
						pPrevious->m_SafeCollisionPosition;
					pInteraction->m_PreActionOutcome = pPrevious != nullptr ?
						EvaluatePhysicalOutcome(*pPrevious) : EImpactPhysicalOutcome::UNKNOWN;
					pInteraction->m_PreActionProblemStateKnown = pPrevious != nullptr ?
						(pPrevious->m_Frozen || Target.m_Frozen) : Target.m_Frozen;
					pInteraction->m_PreActionFreezeEnd = pPrevious != nullptr ? pPrevious->m_FreezeEnd : Target.m_FreezeEnd;
					pInteraction->m_PreActionDeepFrozen = pPrevious != nullptr ? pPrevious->m_DeepFrozen : Target.m_DeepFrozen;
					pInteraction->m_PreActionDynamicSupportKnown = pPrevious != nullptr && pPrevious->m_DynamicSupportKnown;
					pInteraction->m_PreActionDynamicSupport = pPrevious != nullptr && pPrevious->m_DynamicSupport;
					pInteraction->m_PreActionTeleportDestinationKnown = pPrevious != nullptr &&
						pPrevious->m_TeleportDestinationKnown;
					pInteraction->m_PreActionTeleportDestinationSafe = pPrevious != nullptr &&
						pPrevious->m_TeleportDestinationSafe;
					// If the first target-specific action and the restoration are observed
					// in the same authoritative frame, the immediately preceding frozen
					// frame is the physical action baseline. AddInteraction initially uses
					// the current frame, which would otherwise make this valid recovery look
					// as if no displacement happened at all.
					if(!Target.m_Frozen && pPrevious != nullptr && pPrevious->m_Frozen)
					{
						pInteraction->m_ActionStartTick = pPrevious->m_ServerTick;
						pInteraction->m_ActionStartPositionKnown = pPrevious->m_PositionKnown;
						pInteraction->m_ActionStartX = pPrevious->m_X;
						pInteraction->m_ActionStartY = pPrevious->m_Y;
						pInteraction->m_ActionStartVelocityKnown = pPrevious->m_VelocityKnown;
						pInteraction->m_ActionStartVelX = pPrevious->m_VelX;
						pInteraction->m_ActionStartVelY = pPrevious->m_VelY;
					}
					InteractionChanged = true;
				}
				ObserveInteractionPhysicalEvidence(*pInteraction, Target, true);
				const bool WasCausallyLinked = pInteraction->m_CausalSafeTransitionObserved;
				if(pInteraction->m_PhysicalChangeObserved && !pInteraction->m_PreActionAutonomousSafe &&
					HasSafeRecoveryFact(Target))
				{
					pInteraction->m_CausalContributionObserved = true;
					if(!pInteraction->m_CausalSafeTransitionObserved)
					{
						pInteraction->m_CausalSafeTransitionObserved = true;
						pInteraction->m_SafeTransitionTick = Target.m_ServerTick;
					}
					pInteraction->m_PostActionOutcome = EImpactPhysicalOutcome::SAFE;
					InteractionChanged |= !WasCausallyLinked;
				}
			}

			const bool InteractionAlreadyProvedSafeTransition = std::any_of(
				Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(), [&Observed, &Target](const SCausalInteraction &Interaction) {
					return Interaction.m_CausalSafeTransitionObserved &&
						Interaction.m_SafeTransitionTick >= Observed.m_ProblemStartTick &&
						Interaction.m_SafeTransitionTick <= Target.m_ServerTick;
				});
			for(SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
			{
				if(!Interaction.m_TrackPostActionOutcome || Interaction.m_PreActionAutonomousSafe)
					continue;
				const bool WasCausallyLinked = Interaction.m_CausalSafeTransitionObserved;
				// Once an interaction has positively linked a safe transition in this
				// problem episode, do not let a different interaction acquire the same
				// shared outcome merely because it also changed the target earlier.
				// Keep its historical evidence, but don't make it a competing decisive
				// actor for this later restoration.
				const bool AnotherInteractionAlreadyProvedThisSafeTransition = InteractionAlreadyProvedSafeTransition &&
					!Interaction.m_CausalSafeTransitionObserved;
				if(!AnotherInteractionAlreadyProvedThisSafeTransition)
					ObserveInteractionPhysicalEvidence(Interaction, Target, true);
				if(Interaction.m_PhysicalChangeObserved && HasSafeRecoveryFact(Target) &&
					!AnotherInteractionAlreadyProvedThisSafeTransition)
				{
					Interaction.m_CausalContributionObserved = true;
					if(!Interaction.m_CausalSafeTransitionObserved)
					{
						Interaction.m_CausalSafeTransitionObserved = true;
						Interaction.m_SafeTransitionTick = Target.m_ServerTick;
					}
					Interaction.m_PostActionOutcome = EImpactPhysicalOutcome::SAFE;
					InteractionChanged |= !WasCausallyLinked;
				}
			}

			if(InteractionChanged && pOutput != nullptr && !Observed.m_vFrozenInteractions.empty())
			{
				const SCausalInteraction &Interaction = *std::max_element(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
					[](const SCausalInteraction &Left, const SCausalInteraction &Right) { return Left.m_LastTick < Right.m_LastTick; });
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				{
					if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
						Episode.m_Target == Target.m_Participant && Episode.m_TeamId == m_TeamId &&
						Episode.m_StartTick == Observed.m_ProblemStartTick)
					{
						Debug.m_EpisodeId = Episode.m_EpisodeId;
						Debug.m_ProblemEpisodeId = Episode.m_EpisodeId;
						break;
					}
				}
				// The interaction record is target-scoped, while hook state is an
				// actor fact. Keep the diagnostic target fields from the target frame,
				// but source hook fields from the authoritative actor frame so the
				// recovery trace cannot report the target as its own actor.
				const SImpactGameplayFrame *pActor = FindParticipant(vFrames, Interaction.m_Actor);
				if(pActor != nullptr)
				{
					Debug.m_HookKnown = pActor->m_HookKnown;
					Debug.m_HookedPlayer = pActor->m_HookedPlayer;
					Debug.m_HookState = pActor->m_HookState;
					Debug.m_HammerTargetEvidenceKnown = pActor->m_HammerTargetEvidenceKnown;
				}
				Debug.m_DebugLabel = "SAVE_INTERACTION";
				Debug.m_InteractionId = Interaction.m_InteractionId;
				Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
				Debug.m_EventType = EImpactEventType::SAVE_CONFIRMED;
				Debug.m_Target = Target.m_Participant;
				Debug.m_PotentialSaver = Interaction.m_Actor;
				Debug.m_ActorSnapshotAvailable = pActor != nullptr && pActor->m_Confirmed && pActor->m_Present && pActor->m_CurrentCharacterKnown;
				Debug.m_ActorTargetInteractionObservable = Debug.m_ActorSnapshotAvailable &&
					(Interaction.m_HookEvidence || Interaction.m_HammerEvidence);
				Debug.m_TeamId = m_TeamId;
				Debug.m_CandidateCreated = false;
				Debug.m_RecoveryCreated = false;
				Debug.m_StartTick = Interaction.m_FirstTick;
				Debug.m_ActionTick = Interaction.m_ActionStartTick;
				Debug.m_PreActionSafeKnown = Interaction.m_PreActionAutonomousSafe;
				Debug.m_PreActionSafe = Interaction.m_PreActionAutonomousSafe;
				Debug.m_PreActionSafeSource = Interaction.m_PreActionAutonomousSafe ? "NATIVE_SELF_SAFE" : "NONE";
				Debug.m_NativePreActionResult = Interaction.m_PreActionNativeEvaluated ?
					(Interaction.m_PreActionNativeSelfSafe ? "SELF_SAFE" : "UNRESOLVED") : "NOT_EVALUATED";
				Debug.m_PlatformPreexistedBeforeAction = Interaction.m_PreActionPlatformPreexisted;
				Debug.m_FreezeRefreshObservedBeforeAction = Interaction.m_PreActionFreezeRefreshObserved;
				Debug.m_PreActionOutcome = Interaction.m_PreActionOutcome;
				Debug.m_PostActionOutcome = Interaction.m_PostActionOutcome;
				Debug.m_HookEvidence = Interaction.m_HookEvidence;
				Debug.m_HammerEvidence = Interaction.m_HammerEvidence;
				Debug.m_TargetSpecificEvidence = Interaction.m_TargetSpecificEvidence;
				Debug.m_PhysicalChangeEvidence = Interaction.m_PhysicalChangeObserved;
				Debug.m_DecisivePhysicalEvidence = Interaction.m_CausalSafeTransitionObserved;
				Debug.m_HookFirstTick = Interaction.m_HookFirstTick;
				Debug.m_HookLastTick = Interaction.m_HookLastTick;
				Debug.m_Reason = Interaction.m_CausalContributionObserved ?
					"target-specific action changed target; awaiting restoration" : "target-specific interaction recorded";
				AddDebug(pOutput, Debug);
			}
		}
	}

	void CImpactPhase2::DetectSimpleRestorations(int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		for(const SImpactGameplayFrame &Target : vFrames)
		{
			if(!IsValidPhase2Target(Target, m_TeamId) || Target.m_Frozen)
				continue;
			SObservedState &Observed = m_aObserved[Target.m_Participant.m_ClientId];
			if(Observed.m_ProblemStartTick < 0)
				continue;
			const bool SameIdentity = Observed.m_Valid && SameParticipant(Observed.m_Frame, Target);
			const bool PreviousFrozen = SameIdentity && Observed.m_Frame.m_FreezeKnown && Observed.m_Frame.m_Frozen;
			// The first authoritative unfrozen frame after the current problem is the
			// only restoration boundary. Once it has been consumed, later unfrozen
			// frames are confirmation history and must not create another interaction
			// or another recovery attempt for the same boundary.
			const bool FirstRestoration = (PreviousFrozen || Observed.m_HasLastKnownFreeze) &&
				!Observed.m_RestorationObserved;
			if(!FirstRestoration)
				continue;

			// Observe the final physical segment before closing active spans. The
			// interaction history is episode-scoped, so a hook released one snapshot
			// earlier can still be the cause of this restoration.
			for(SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
			{
				if(Interaction.m_TrackPostActionOutcome || Interaction.m_ActionActive)
					ObserveInteractionPhysicalEvidence(Interaction, Target, true);
				if(Interaction.m_PhysicalChangeObserved && !Interaction.m_PreActionAutonomousSafe)
				{
					Interaction.m_CausalContributionObserved = true;
					const bool HasOtherPriorCausalTransition = std::any_of(
						Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(), [&Interaction, &Target](const SCausalInteraction &Other) {
							return &Other != &Interaction && Other.m_CausalSafeTransitionObserved &&
								Other.m_SafeTransitionTick >= 0 && Other.m_SafeTransitionTick < Target.m_ServerTick;
						});
					if(!Interaction.m_CausalSafeTransitionObserved && !HasOtherPriorCausalTransition)
					{
						Interaction.m_CausalSafeTransitionObserved = true;
						Interaction.m_SafeTransitionTick = Target.m_ServerTick;
					}
					Interaction.m_PostActionOutcome = EImpactPhysicalOutcome::SAFE;
				}
			}

			// A target-specific action can be observed on the same authoritative
			// unfreeze frame. It still needs an actual displacement/velocity change
			// relative to the frozen baseline, but it must not be skipped merely
			// because no earlier interaction span was materialized.
			for(const SImpactGameplayFrame &Actor : vFrames)
			{
				if(!Actor.m_Confirmed || !Actor.m_Present || !Actor.m_CurrentCharacterKnown || !Actor.m_Active ||
					!Actor.m_Eligible || !Actor.m_AliveKnown || !Actor.m_Alive || !Actor.m_Participant.IsValid() ||
					Actor.m_Participant == Target.m_Participant || Actor.m_TeamId != m_TeamId ||
					!Actor.m_SoloKnown || Actor.m_Solo || !IsValidClientId(Actor.m_Participant.m_ClientId))
					continue;
				const bool HookEvidence = HasHookEvidence(Actor, Target.m_Participant.m_ClientId);
				const bool HammerEvidence = Actor.m_HammerTargetEvidenceKnown && Actor.m_HammerTargetEvidence;
				if(!HookEvidence && !HammerEvidence)
					continue;
				SCausalInteraction *pExisting = FindInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant);
				const bool NewInteraction = pExisting == nullptr;
				const bool PreActionNativeSelfSafe =
					Observed.m_AutonomousSelfRecoveryNativeProof ||
					(Observed.m_NativeAutonomousResultObserved && Observed.m_NativeAutonomousResultSelfSafe) ||
					(Observed.m_HasLastKnownFreeze && HasPositiveAutonomousSelfSafeFact(Observed.m_LastKnownFreezeFrame));
				const bool PreActionAutonomousSafe = NewInteraction && PreActionNativeSelfSafe;
				AddInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant,
					HammerEvidence, HookEvidence,
					HookEvidence && Actor.m_MeaningfulHookSupportKnown && Actor.m_MeaningfulHookSupport,
					Actor.m_HammerTargetEvidenceKnown, PreActionAutonomousSafe, Target.m_ServerTick, Target);
				SCausalInteraction *pInteraction = FindInteraction(Observed.m_vFrozenInteractions, Actor.m_Participant, Target.m_Participant);
				if(pInteraction == nullptr || !NewInteraction)
					continue;
				if(Target.m_PreActionAutonomousSelfSafeTick >= Observed.m_ProblemStartTick &&
					Target.m_PreActionAutonomousSelfSafeTick < Target.m_ServerTick)
					pInteraction->m_PreActionAutonomousSelfSafeTick = Target.m_PreActionAutonomousSelfSafeTick;
				pInteraction->m_PreActionAutonomousSafe = PreActionAutonomousSafe;
				pInteraction->m_PreActionNativeEvaluated = Observed.m_NativeAutonomousResultObserved;
				pInteraction->m_PreActionNativeSelfSafe = PreActionNativeSelfSafe;
				pInteraction->m_PreActionPlatformPreexisted = Observed.m_HasLastKnownFreeze &&
					HasSafePlatformContact(Observed.m_LastKnownFreezeFrame);
				pInteraction->m_PreActionFreezeRefreshObserved =
					(Observed.m_HasPreviousFrame &&
						HasFreezeRefreshBetweenFrames(Observed.m_LastKnownFreezeFrame, &Observed.m_PreviousFrame));
				pInteraction->m_PreActionSafeCollisionPositionKnown = PreActionAutonomousSafe &&
					Observed.m_LastKnownFreezeFrame.m_SafeCollisionPositionKnown;
				pInteraction->m_PreActionSafeCollisionPosition = pInteraction->m_PreActionSafeCollisionPositionKnown &&
					Observed.m_LastKnownFreezeFrame.m_SafeCollisionPosition;
				pInteraction->m_PreActionOutcome = Observed.m_LastKnownFreezeFrame.m_PhysicalOutcome;
				pInteraction->m_PreActionProblemStateKnown = true;
				if(Observed.m_LastKnownFreezeFrame.m_ServerTick >= 0 &&
					Observed.m_LastKnownFreezeFrame.m_ServerTick < Target.m_ServerTick)
				{
					pInteraction->m_ActionStartTick = Observed.m_LastKnownFreezeFrame.m_ServerTick;
					pInteraction->m_ActionStartPositionKnown = Observed.m_LastKnownFreezeFrame.m_PositionKnown;
					pInteraction->m_ActionStartX = Observed.m_LastKnownFreezeFrame.m_X;
					pInteraction->m_ActionStartY = Observed.m_LastKnownFreezeFrame.m_Y;
					pInteraction->m_ActionStartVelocityKnown = Observed.m_LastKnownFreezeFrame.m_VelocityKnown;
					pInteraction->m_ActionStartVelX = Observed.m_LastKnownFreezeFrame.m_VelX;
					pInteraction->m_ActionStartVelY = Observed.m_LastKnownFreezeFrame.m_VelY;
				}
				// AddInteraction starts at the current (unfrozen) frame. For an action
				// first observed on this same restoration snapshot, compare that frame
				// with the immediately preceding frozen target state so the causal
				// physical change is not lost to the ordering of the batch.
				ObserveInteractionPhysicalEvidence(*pInteraction, Target, true);
			}

			// This is a positive restoration observation, not a self-safe Fall result.
			// It only prevents the same unfrozen boundary from being processed again;
			// Save attribution still uses the immutable pre-action interaction history.
			Observed.m_RestorationObserved = true;
			Observed.m_RestorationTick = Target.m_ServerTick;
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				Debug.m_DebugLabel = "RESTORATION_OBSERVED";
				Debug.m_Target = Target.m_Participant;
				Debug.m_TeamId = Target.m_TeamId;
				Debug.m_RestorationTick = Target.m_ServerTick;
				Debug.m_ProblemEpisodeId = 0;
				for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				{
					if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_Target == Target.m_Participant &&
						Episode.m_TeamId == m_TeamId && Episode.m_StartTick == Observed.m_ProblemStartTick)
					{
						Debug.m_EpisodeId = Episode.m_EpisodeId;
						Debug.m_ProblemEpisodeId = Episode.m_EpisodeId;
						Debug.m_EpisodeState = Episode.m_State;
						break;
					}
				}
				if(!Observed.m_vFrozenInteractions.empty())
				{
					const auto It = std::max_element(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
						[](const SCausalInteraction &Left, const SCausalInteraction &Right) { return Left.m_LastTick < Right.m_LastTick; });
					Debug.m_PotentialSaver = It->m_Actor;
					Debug.m_InteractionId = It->m_InteractionId;
					Debug.m_HookEvidence = It->m_HookEvidence;
					Debug.m_HammerEvidence = It->m_HammerEvidence;
					Debug.m_PhysicalChangeEvidence = It->m_PhysicalChangeObserved;
					Debug.m_DecisivePhysicalEvidence = It->m_CausalSafeTransitionObserved;
				}
				Debug.m_Reason = "first authoritative unfrozen frame observed for current problem episode";
				AddDebug(pOutput, Debug);
			}
			if(Observed.m_vFrozenInteractions.empty())
				continue;

			// A positive SELF_SAFE result can be observed after a target-specific
			// interaction has already been recorded. In that ordering, the
			// interaction's immutable pre-action fields remain UNKNOWN, while
			// TrackFrames retains the positive native result on Observed. Bind that
			// result to this exact participant generation, team and current problem
			// episode before allowing the restoration to materialize a Save.
			const SImpactPendingEpisode *pCurrentProblemEpisode = nullptr;
			for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
			{
				if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
					Episode.m_Target == Target.m_Participant && Episode.m_TeamId == m_TeamId &&
					Episode.m_StartTick == Observed.m_ProblemStartTick)
				{
					pCurrentProblemEpisode = &Episode;
					break;
				}
			}
		const bool SameProblemIdentity = SameIdentity && pCurrentProblemEpisode != nullptr &&
			Observed.m_Frame.m_TeamId == Target.m_TeamId && Target.m_TeamId == m_TeamId &&
			Observed.m_HasLastKnownFreeze &&
			SameParticipant(Observed.m_LastKnownFreezeFrame, Target);
		const int64_t ProblemStartTick = pCurrentProblemEpisode != nullptr ? pCurrentProblemEpisode->m_StartTick : -1;
		const int64_t RestorationTick = Target.m_ServerTick;
		const auto EvidenceBelongsToCurrentProblem = [ProblemStartTick, RestorationTick](int64_t EvidenceTick) {
			return ProblemStartTick >= 0 && EvidenceTick >= ProblemStartTick &&
				(RestorationTick < 0 || EvidenceTick <= RestorationTick);
		};
		int64_t SelfSafeEvidenceTick = -1;
		if(SameProblemIdentity && Observed.m_AutonomousSelfRecoveryProven &&
			Observed.m_AutonomousSelfRecoveryNativeProof &&
			Observed.m_AutonomousSelfRecoveryEpisodeId == pCurrentProblemEpisode->m_EpisodeId &&
			EvidenceBelongsToCurrentProblem(Observed.m_AutonomousSelfRecoverySourceTick))
			SelfSafeEvidenceTick = Observed.m_AutonomousSelfRecoverySourceTick;
		if(SameProblemIdentity && Observed.m_NativeAutonomousResultObserved &&
			Observed.m_NativeAutonomousResultSelfSafe &&
			EvidenceBelongsToCurrentProblem(Observed.m_NativeAutonomousResultTick) &&
			(SelfSafeEvidenceTick < 0 || Observed.m_NativeAutonomousResultTick < SelfSafeEvidenceTick))
			SelfSafeEvidenceTick = Observed.m_NativeAutonomousResultTick;
		int64_t FirstPhysicalChangeTick = -1;
		SParticipantId FirstPhysicalChangeActor;
		for(const SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
		{
			if(Interaction.m_Target != Target.m_Participant || !Interaction.m_RelevantToThisFreezeEpisode ||
				!Interaction.m_TargetSpecificEvidence || !Interaction.m_PhysicalChangeObserved ||
				!EvidenceBelongsToCurrentProblem(Interaction.m_FirstTick) ||
				!EvidenceBelongsToCurrentProblem(Interaction.m_FirstPhysicalChangeTick))
				continue;
			if(FirstPhysicalChangeTick < 0 || Interaction.m_FirstPhysicalChangeTick < FirstPhysicalChangeTick)
			{
				FirstPhysicalChangeTick = Interaction.m_FirstPhysicalChangeTick;
				FirstPhysicalChangeActor = Interaction.m_Actor;
			}
		}
		// SELF_SAFE is a Save veto only when it was already established before any
		// observed teammate-caused physical change. A positive simulation that comes
		// after the helper has already moved the target describes the post-action
		// state and must not erase a real rescue. Strict ordering avoids inventing
		// intra-tick causality when both facts share one snapshot.
		const bool CurrentEpisodeWasAlreadySelfSafe = SameProblemIdentity && SelfSafeEvidenceTick >= 0 &&
			FirstPhysicalChangeTick > SelfSafeEvidenceTick;
		if(CurrentEpisodeWasAlreadySelfSafe)
		{
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "SAVE_REJECTED";
				Debug.m_EventType = EImpactEventType::SAVE_CONFIRMED;
				Debug.m_EpisodeId = pCurrentProblemEpisode->m_EpisodeId;
				Debug.m_ProblemEpisodeId = pCurrentProblemEpisode->m_EpisodeId;
				Debug.m_Target = Target.m_Participant;
				Debug.m_TeamId = m_TeamId;
				Debug.m_StartTick = ProblemStartTick;
				Debug.m_RestorationTick = RestorationTick;
				Debug.m_PotentialSaver = FirstPhysicalChangeActor;
				Debug.m_SelfSafeEvidenceTick = SelfSafeEvidenceTick;
				Debug.m_FirstPhysicalChangeTick = FirstPhysicalChangeTick;
				Debug.m_AutonomousSelfRecoveryProven = true;
				Debug.m_PreActionSafeKnown = true;
				Debug.m_PreActionSafe = true;
				Debug.m_PreActionSafeSource = "CURRENT_EPISODE_SELF_SAFE";
				Debug.m_NativePreActionResult = "SELF_SAFE";
				Debug.m_Reason = "positive SELF_SAFE evidence predates teammate physical change in this participant generation and problem episode";
				AddDebug(pOutput, Debug);
			}
			continue;
		}

			const char *pRejectReason = nullptr;
			const bool RecoveryMaterialized = MaterializeRecovery(Observed.m_vFrozenInteractions, 0,
				Target.m_Participant, m_TeamId, Observed.m_ProblemStartTick, Target.m_ServerTick, TickSpeed,
				vFrames, Target, true, SelfSafeEvidenceTick, pOutput, &pRejectReason);
			if(RecoveryMaterialized && pOutput != nullptr)
			{
				const SImpactPendingEpisode *pRecovery = nullptr;
				for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				{
					if(Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_Target == Target.m_Participant &&
						Episode.m_TeamId == m_TeamId && Episode.m_StartTick >= Observed.m_ProblemStartTick)
					{
						pRecovery = &Episode;
						break;
					}
				}
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				Debug.m_DebugLabel = "SAVE_RESTORE";
				Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
				Debug.m_EventType = EImpactEventType::SAVE_CONFIRMED;
				Debug.m_EpisodeId = pRecovery != nullptr ? pRecovery->m_EpisodeId : 0;
				Debug.m_ProblemEpisodeId = pCurrentProblemEpisode != nullptr ? pCurrentProblemEpisode->m_EpisodeId : 0;
				Debug.m_RelatedFallEpisodeId = pRecovery != nullptr ? pRecovery->m_RelatedFallEpisodeId : 0;
				Debug.m_RelatedRecoveryEpisodeId = pRecovery != nullptr ? pRecovery->m_EpisodeId : 0;
				Debug.m_Target = Target.m_Participant;
				Debug.m_PotentialSaver = pRecovery != nullptr ? pRecovery->m_Actor : SParticipantId{};
				Debug.m_TeamId = m_TeamId;
				Debug.m_SelfSafeEvidenceTick = SelfSafeEvidenceTick;
				Debug.m_CandidateCreated = true;
				Debug.m_RecoveryCreated = true;
				for(const SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
				{
					if(Interaction.m_Target == Target.m_Participant && Interaction.m_Actor == Debug.m_PotentialSaver &&
						Interaction.m_PhysicalChangeObserved)
					{
						Debug.m_FirstPhysicalChangeTick = Interaction.m_FirstPhysicalChangeTick;
						break;
					}
				}
				if(pRecovery != nullptr)
				{
					Debug.m_FallAlreadyConfirmed = pRecovery->m_RelatedFallAlreadyConfirmed;
					Debug.m_ConfirmedFall = Debug.m_FallAlreadyConfirmed;
					const auto FirstInteraction = std::min_element(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
						[](const SCausalInteraction &A, const SCausalInteraction &B) { return A.m_FirstTick < B.m_FirstTick; });
					Debug.m_PreActionSafeKnown = FirstInteraction != Observed.m_vFrozenInteractions.end() &&
						FirstInteraction->m_PreActionAutonomousSafe;
					if(FirstInteraction != Observed.m_vFrozenInteractions.end())
					{
						Debug.m_PreActionSafeSource = FirstInteraction->m_PreActionAutonomousSafe ? "NATIVE_SELF_SAFE" : "NONE";
						Debug.m_NativePreActionResult = FirstInteraction->m_PreActionNativeEvaluated ?
							(FirstInteraction->m_PreActionNativeSelfSafe ? "SELF_SAFE" : "UNRESOLVED") : "NOT_EVALUATED";
						Debug.m_PlatformPreexistedBeforeAction = FirstInteraction->m_PreActionPlatformPreexisted;
						Debug.m_FreezeRefreshObservedBeforeAction = FirstInteraction->m_PreActionFreezeRefreshObserved;
					}
				}
				Debug.m_PreActionSafe = false;
				Debug.m_PhysicalChangeEvidence = true;
				Debug.m_DecisivePhysicalEvidence = true;
				Debug.m_RestorationTick = Target.m_ServerTick;
				Debug.m_Reason = "target left problem after attributable teammate action";
				AddDebug(pOutput, Debug);
			}
			else if(!RecoveryMaterialized && pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				FillDebugFrame(Debug, Target);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "SAVE_REJECTED";
				Debug.m_EventType = EImpactEventType::SAVE_CONFIRMED;
				Debug.m_EpisodeId = pCurrentProblemEpisode != nullptr ? pCurrentProblemEpisode->m_EpisodeId : 0;
				Debug.m_ProblemEpisodeId = pCurrentProblemEpisode != nullptr ? pCurrentProblemEpisode->m_EpisodeId : 0;
				Debug.m_Target = Target.m_Participant;
				Debug.m_TeamId = m_TeamId;
				Debug.m_StartTick = Observed.m_ProblemStartTick;
				Debug.m_RestorationTick = RestorationTick;
				Debug.m_SelfSafeEvidenceTick = SelfSafeEvidenceTick;
				Debug.m_AutonomousSelfRecoveryProven = Observed.m_AutonomousSelfRecoveryProven;
				Debug.m_PreActionSafeKnown = SelfSafeEvidenceTick >= 0;
				Debug.m_PreActionSafe = SelfSafeEvidenceTick >= 0;
				Debug.m_PreActionSafeSource = SelfSafeEvidenceTick >= 0 ? "CURRENT_EPISODE_NATIVE" : "NONE";
				Debug.m_NativePreActionResult = SelfSafeEvidenceTick >= 0 ? "SELF_SAFE" :
					(Observed.m_NativeAutonomousResultObserved ? "UNRESOLVED" : "NOT_EVALUATED");
				for(const SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
				{
					if(Interaction.m_Target != Target.m_Participant || !Interaction.m_RelevantToThisFreezeEpisode ||
						!Interaction.m_TargetSpecificEvidence)
						continue;
					if(!Debug.m_PotentialSaver.IsValid() ||
						(Interaction.m_PhysicalChangeObserved &&
							(Debug.m_FirstPhysicalChangeTick < 0 || Interaction.m_FirstPhysicalChangeTick < Debug.m_FirstPhysicalChangeTick)))
					{
						Debug.m_PotentialSaver = Interaction.m_Actor;
						Debug.m_ActionTick = Interaction.m_FirstTick;
						if(Interaction.m_PhysicalChangeObserved)
							Debug.m_FirstPhysicalChangeTick = Interaction.m_FirstPhysicalChangeTick;
					}
					Debug.m_HookEvidence |= Interaction.m_HookEvidence;
					Debug.m_HammerEvidence |= Interaction.m_HammerEvidence;
					if(Interaction.m_PreActionAutonomousSelfSafeTick >= Observed.m_ProblemStartTick &&
						(Debug.m_SelfSafeEvidenceTick < 0 || Interaction.m_PreActionAutonomousSelfSafeTick < Debug.m_SelfSafeEvidenceTick))
						Debug.m_SelfSafeEvidenceTick = Interaction.m_PreActionAutonomousSelfSafeTick;
				}
				Debug.m_Reason = pRejectReason != nullptr ? pRejectReason : "recovery candidate rejected without a reason";
				AddDebug(pOutput, Debug);
			}
		}
	}

	void CImpactPhase2::TrackFrames(int64_t ServerTick, const std::vector<SImpactGameplayFrame> &vFrames)
	{
		std::array<bool, MAX_CLIENTS> aSeen{};
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			const int ClientId = Frame.m_Participant.m_ClientId;
			if(!IsValidClientId(ClientId) || !Frame.m_Participant.IsValid() || aSeen[ClientId])
				continue;
			aSeen[ClientId] = true;
			SObservedState &Observed = m_aObserved[ClientId];
			const bool SameIdentity = Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame);
			if(SameIdentity && Observed.m_Frame.m_CurrentCharacterKnown)
			{
				Observed.m_PreviousFrame = Observed.m_Frame;
				Observed.m_HasPreviousFrame = true;
			}
			else
			{
				Observed.m_PreviousFrame = {};
				Observed.m_HasPreviousFrame = false;
			}
			Observed.m_PreviousAliveUnfrozenSince = SameIdentity ? Observed.m_AliveUnfrozenSince : -1;
			if(Observed.m_ProblemEpisodeStartedThisFrame)
			{
				Observed.m_NativeAutonomousResultObserved = false;
				Observed.m_NativeAutonomousResultSelfSafe = false;
				Observed.m_NativeAutonomousResultTick = -1;
			}
			if(!SameIdentity)
			{
				// RecordFrozenInteractions runs before TrackFrames and may have just
				// initialized the first frozen episode for this identity/generation.
				// Preserve that exact episode baseline (and its first-frame hook spans)
				// instead of resetting ProblemStartTick and making the next snapshot look
				// like another new freeze.
				const bool FrozenEpisodeStartedThisFrame = Observed.m_ProblemEpisodeStartedThisFrame &&
					Frame.m_FreezeKnown && Frame.m_Frozen;
				// A reused client slot is a new participant generation. Do not allow
				// pending harm/recovery from the old generation to resolve against the
				// replacement, even if the numeric client id is identical.
				if(Observed.m_Valid && Observed.m_Frame.m_Participant.IsValid() &&
					Observed.m_Frame.m_Participant != Frame.m_Participant)
				{
					m_vPendingEpisodes.erase(std::remove_if(m_vPendingEpisodes.begin(), m_vPendingEpisodes.end(),
						[&Frame](const SImpactPendingEpisode &Episode) {
							return Episode.m_Target.m_ClientId == Frame.m_Participant.m_ClientId &&
								Episode.m_Target != Frame.m_Participant;
						}), m_vPendingEpisodes.end());
				}
				Observed.m_AliveUnfrozenSince = -1;
				Observed.m_HasLastKnownCharacter = false;
				Observed.m_HasLastKnownFreeze = false;
				if(!FrozenEpisodeStartedThisFrame)
				{
					Observed.m_ProblemStartTick = -1;
					Observed.m_ProblemEpisodeStartedThisFrame = false;
					Observed.m_ProblemOriginTeammateCaused = false;
					Observed.m_SelfSafeInvalidatedTick = -1;
					Observed.m_PostInvalidationHarmfulEvidenceTick = -1;
				}
				Observed.m_EpisodeRecoveryNeed = Frame.m_FreezeKnown && Frame.m_Frozen ? Frame.m_RecoveryNeed : EImpactRecoveryNeed::UNKNOWN;
				// RecordFrozenInteractions runs before TrackFrames. If this is the first
				// authoritative frozen frame for a new identity, it may already have
				// recorded the episode's safe-platform evidence. Preserve that result;
				// non-frozen identity changes still start with a clean safe-state marker.
				if(!(Frame.m_FreezeKnown && Frame.m_Frozen))
				{
					Observed.m_RestorationObserved = false;
					Observed.m_RestorationTick = -1;
					Observed.m_AutonomousSelfRecoveryProven = false;
					Observed.m_AutonomousSelfRecoveryEpisodeId = 0;
					Observed.m_AutonomousSelfRecoverySourceTick = -1;
					Observed.m_AutonomousSelfRecoveryNativeProof = false;
					Observed.m_NativeAutonomousResultObserved = false;
					Observed.m_NativeAutonomousResultSelfSafe = false;
					Observed.m_NativeAutonomousResultTick = -1;
				}
				// RecordFrozenInteractions has already discarded stale evidence and
				// recorded evidence from this first confirmed frozen frame. Preserve
				// that new episode history when the slot itself was just observed.
				if(Frame.m_FreezeKnown && !Frame.m_Frozen)
					Observed.m_vFrozenInteractions.clear();
				m_aFinishSignals[ClientId] = false;
			}
			// A SpecChar is emitted by DDNet only while the participant's actual
			// CCharacter is paused outside the world. Pause(true) resets/releases
			// hooks; Pause(false) resumes with zero velocity. This ends old actor
			// trajectory attribution without ending the frozen problem/Fall episode.
			// A generic missing Character snapshot deliberately does not do this.
			if(SameIdentity && Frame.m_PhysicallyPausedCharacter)
				Observed.m_vFrozenInteractions.clear();
			// Current positive autonomous evidence is evaluated from the accepted
			// frame. The only historical value retained here is the episode-scoped
			// proof recorded before a first teammate action; it is cleared at identity
			// and stable episode boundaries, never carried across a new physical episode.
			if(IsContinuouslyAliveUnfrozen(Frame))
			{
				if(!SameIdentity || !IsContinuouslyAliveUnfrozen(Observed.m_Frame) ||
					Observed.m_Frame.m_TeamId != Frame.m_TeamId || Frame.m_ServerTick < Observed.m_Frame.m_ServerTick)
					Observed.m_AliveUnfrozenSince = Frame.m_ServerTick >= 0 ? Frame.m_ServerTick : ServerTick;
			}
			else if(Frame.m_AliveKnown && Frame.m_FreezeKnown)
				Observed.m_AliveUnfrozenSince = -1;
			// Keep the context that describes the harmful side of this recovery
			// episode. Do not overwrite it with the final supported/unfrozen frame:
			// that would make a teammate displacement recovery look self-safe when
			// restoration is resolved later or after a sparse snapshot.
			if(Frame.m_FreezeKnown && Frame.m_CurrentCharacterKnown && Frame.m_Frozen)
			{
				Observed.m_LastKnownFreezeFrame = Frame;
				Observed.m_HasLastKnownFreeze = true;
			}
			if(Frame.m_Confirmed && Frame.m_Present && Frame.m_CurrentCharacterKnown)
			{
				Observed.m_LastKnownCharacterFrame = Frame;
				Observed.m_HasLastKnownCharacter = true;
			}
			if(Frame.m_AutonomousRecoveryEvaluated)
			{
				Observed.m_NativeAutonomousResultObserved = true;
				Observed.m_NativeAutonomousResultSelfSafe = HasPositiveAutonomousSelfSafeFact(Frame);
				Observed.m_NativeAutonomousResultTick = Frame.m_ServerTick;
			}
			Observed.m_Frame = Frame;
			Observed.m_Valid = true;
		}

		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(aSeen[ClientId] || !m_aObserved[ClientId].m_Valid)
				continue;
			// A missing frame is an UNKNOWN snapshot, not a disconnect. Retain the
			// last authoritative identity/character/freeze context so a sparse remote
			// update can still resolve the same episode. Explicit DISCONNECT and reset
			// messages clear this state through their lifecycle paths.
			m_aObserved[ClientId].m_PreviousAliveUnfrozenSince = m_aObserved[ClientId].m_AliveUnfrozenSince;
			m_aObserved[ClientId].m_AliveUnfrozenSince = -1;
		}
	}

	bool CImpactPhase2::MaterializeRecovery(const std::vector<SCausalInteraction> &vInteractions, uint64_t RelatedFallEpisodeId,
		const SParticipantId &Target, int TeamId, int64_t ProblemStartTick, int64_t ServerTick, int TickSpeed,
		const std::vector<SImpactGameplayFrame> &vContextFrames, const SImpactGameplayFrame &TargetFrame,
		bool RestorationObserved, int64_t SelfSafeEvidenceTick, SImpactPhase2Output *pOutput, const char **ppRejectReason)
	{
		auto SetRejectReason = [ppRejectReason](const char *pReason) {
			if(ppRejectReason != nullptr && *ppRejectReason == nullptr)
				*ppRejectReason = pReason;
		};
		if(!Target.IsValid() || !IsValidTeam(TeamId) || ProblemStartTick < 0)
		{
			SetRejectReason("invalid recovery context");
			return false;
		}
		if(!RestorationObserved || TargetFrame.m_Frozen)
		{
			// Interaction history is collected while the target is frozen. It is not
			// a recovery record until an authoritative unfrozen boundary is observed.
			SetRejectReason("recovery requires an observed unfrozen restoration");
			return false;
		}

		struct SCandidate
		{
			const SCausalInteraction *m_pInteraction = nullptr;
			int64_t m_Tick = -1;
		};
		std::vector<SCandidate> vCandidates;
		std::vector<SParticipantId> vContributors;
		for(const SCausalInteraction &Interaction : vInteractions)
		{
			if(Interaction.m_Target != Target || !Interaction.m_Actor.IsValid() ||
				!Interaction.m_RelevantToThisFreezeEpisode ||
				!Interaction.m_TargetSpecificEvidence || Interaction.m_PreActionAutonomousSafe ||
				Interaction.m_FirstTick < ProblemStartTick || Interaction.m_FirstTick > ServerTick)
				continue;
			// A positive native no-interaction proof can be later than an earlier
			// rescuer's causal movement. Apply it per actor: it excludes only the
			// interaction whose first physical delta happens after the proof, rather
			// than letting a later actor inherit someone else's trajectory.
			int64_t ActorSafeProofTick = SelfSafeEvidenceTick;
			if(Interaction.m_PreActionAutonomousSelfSafeTick >= ProblemStartTick &&
				(ActorSafeProofTick < ProblemStartTick || Interaction.m_PreActionAutonomousSelfSafeTick < ActorSafeProofTick))
				ActorSafeProofTick = Interaction.m_PreActionAutonomousSelfSafeTick;
			if(ActorSafeProofTick >= ProblemStartTick && Interaction.m_FirstPhysicalChangeTick > ActorSafeProofTick)
			{
				SetRejectReason("native autonomous trajectory was already safe before this actor's physical change");
				continue;
			}
			if(std::find(vContributors.begin(), vContributors.end(), Interaction.m_Actor) == vContributors.end())
				vContributors.push_back(Interaction.m_Actor);
			if(!Interaction.m_PhysicalChangeObserved)
				continue;
			// Restoration is only attributable after the interaction has observed a
			// real safe boundary.  A bare later unfreeze is not enough: it can be the
			// ordinary natural expiry of a target that the interaction never changed.
			// Raw Save uses the current authoritative recovery fact directly. The
			// legacy transition bit is compatibility/debug metadata only and must not
			// remain a hidden permission gate for an ordinary rescue.
			const bool SafeBoundaryObserved = RestorationObserved || HasObservedRecoveryFact(TargetFrame);
			if(!SafeBoundaryObserved)
				continue;
			// A target-specific action must be linked to the observed recovery. A
			// movement delta observed while frozen is the strongest link. For hooks,
			// the authoritative hook span itself remains causal episode history even
			// when the sparse stream exposes the displacement only on the later
			// restoration frame; the hook is not required to still be active at the
			// restoration tick. The interaction's first tick and physical-change
			// evidence above keep this limited to the current episode and prevent a
			// post-unfreeze action from being retroactively attributed.
			// Hammer-only history does not get that fallback, so ordinary movement
			// after natural expiry cannot turn a stale hammer attack into a Save.
			const bool ReleasedOrActiveHookRecoveryLink = RestorationObserved &&
				Interaction.m_HookEvidence && Interaction.m_HookFirstTick >= ProblemStartTick &&
				Interaction.m_PhysicalChangeObserved;
			const bool RecoveryPhysicalLink = Interaction.m_PhysicalChangeWhileFrozen ||
				ReleasedOrActiveHookRecoveryLink;
			if(!RecoveryPhysicalLink)
			{
				SetRejectReason("physical change was observed only after recovery");
				continue;
			}
			const int64_t SafeTick = Interaction.m_CausalSafeTransitionObserved && Interaction.m_SafeTransitionTick >= 0 ?
				Interaction.m_SafeTransitionTick : ServerTick;
			vCandidates.push_back({&Interaction, SafeTick});
		}
		if(vCandidates.empty())
		{
			SetRejectReason(vContributors.empty() ? "no target-specific interaction" :
				"target-specific action has no observed physical recovery");
			return false;
		}
		// At a restoration boundary, an older released interaction may already
		// contain physical movement without having created the safe outcome. Prefer
		// an action that is still active at the boundary; if no action is active,
		// use the latest observed physical contribution. This is causal ordering,
		// not a last-touch/proximity heuristic: a later actor wins only when the
		// authoritative target movement attributable to that actor is the one that
		// reaches the recovery boundary.
		std::vector<SCandidate> vDecisiveCandidates = vCandidates;
		if(RestorationObserved)
		{
			// A positive actor-specific safe transition observed before the final
			// restoration is stronger than the shared displacement visible on the
			// restoration frame. Refreeze re-arms the transition bit, so an earlier
			// failed attempt cannot carry this proof into a later recovery. This keeps
			// a setup actor from becoming tied with the actor whose later action
			// independently produced the successful recovery boundary.
			const bool HavePriorCausalSafeTransition = std::any_of(vCandidates.begin(), vCandidates.end(), [ProblemStartTick, ServerTick](const SCandidate &Candidate) {
				const SCausalInteraction &Interaction = *Candidate.m_pInteraction;
				return Interaction.m_CausalSafeTransitionObserved &&
					Interaction.m_SafeTransitionTick >= ProblemStartTick &&
					Interaction.m_SafeTransitionTick < ServerTick;
			});
			if(HavePriorCausalSafeTransition)
			{
				vDecisiveCandidates.erase(std::remove_if(vDecisiveCandidates.begin(), vDecisiveCandidates.end(), [ProblemStartTick, ServerTick](const SCandidate &Candidate) {
					const SCausalInteraction &Interaction = *Candidate.m_pInteraction;
					return !Interaction.m_CausalSafeTransitionObserved ||
						Interaction.m_SafeTransitionTick < ProblemStartTick ||
						Interaction.m_SafeTransitionTick >= ServerTick;
				}), vDecisiveCandidates.end());
			}
			else
			{
				const bool HaveActiveCandidate = std::any_of(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Candidate) {
					return Candidate.m_pInteraction->m_ActionActive;
				});
				if(HaveActiveCandidate)
				{
					vDecisiveCandidates.erase(std::remove_if(vDecisiveCandidates.begin(), vDecisiveCandidates.end(), [](const SCandidate &Candidate) {
						return !Candidate.m_pInteraction->m_ActionActive;
					}), vDecisiveCandidates.end());
				}
				else
				{
					const int64_t LatestPhysicalChangeTick = std::max_element(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
						return Left.m_pInteraction->m_LastPhysicalChangeTick < Right.m_pInteraction->m_LastPhysicalChangeTick;
					})->m_pInteraction->m_LastPhysicalChangeTick;
					vDecisiveCandidates.erase(std::remove_if(vDecisiveCandidates.begin(), vDecisiveCandidates.end(),
						[LatestPhysicalChangeTick](const SCandidate &Candidate) {
							return Candidate.m_pInteraction->m_LastPhysicalChangeTick != LatestPhysicalChangeTick;
						}), vDecisiveCandidates.end());
				}
			}
		}
		if(vDecisiveCandidates.empty())
			vDecisiveCandidates = vCandidates;
		const int64_t EarliestSafeTick = std::min_element(vCandidates.begin(), vCandidates.end(),
			[](const SCandidate &Left, const SCandidate &Right) { return Left.m_Tick < Right.m_Tick; })->m_Tick;
		std::vector<SParticipantId> vDecisiveActors;
		const SCausalInteraction *pEarliestInteraction = nullptr;
		for(const SCandidate &Candidate : vCandidates)
		{
			if(Candidate.m_Tick == EarliestSafeTick && pEarliestInteraction == nullptr)
				pEarliestInteraction = Candidate.m_pInteraction;
		}
		const SCausalInteraction *pDecisiveInteraction = nullptr;
		for(const SCandidate &Candidate : vDecisiveCandidates)
		{
			if(pDecisiveInteraction == nullptr)
				pDecisiveInteraction = Candidate.m_pInteraction;
			if(std::find(vDecisiveActors.begin(), vDecisiveActors.end(), Candidate.m_pInteraction->m_Actor) == vDecisiveActors.end())
				vDecisiveActors.push_back(Candidate.m_pInteraction->m_Actor);
		}
		const bool UniqueActor = vDecisiveActors.size() == 1;
		const SParticipantId DecisiveActor = UniqueActor ? vDecisiveActors.front() : SParticipantId{};
		const int64_t ActionTick = pEarliestInteraction != nullptr && pEarliestInteraction->m_ActionStartTick >= 0 ?
			pEarliestInteraction->m_ActionStartTick :
			(pEarliestInteraction != nullptr && pEarliestInteraction->m_FirstTick >= 0 ?
				pEarliestInteraction->m_FirstTick : EarliestSafeTick);
		uint64_t AssociatedFallEpisodeId = RelatedFallEpisodeId;
		if(AssociatedFallEpisodeId == 0)
		{
			for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
			{
				if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_Target == Target &&
					Episode.m_TeamId == TeamId && Episode.m_StartTick == ProblemStartTick)
				{
					AssociatedFallEpisodeId = Episode.m_EpisodeId;
					break;
				}
			}
		}

		size_t RecoveryIndex = m_vPendingEpisodes.size();
		for(size_t Index = 0; Index < m_vPendingEpisodes.size(); ++Index)
		{
			const SImpactPendingEpisode &Episode = m_vPendingEpisodes[Index];
			if(Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_Target == Target &&
				Episode.m_TeamId == TeamId && Episode.m_StartTick >= ProblemStartTick)
			{
				RecoveryIndex = Index;
				break;
			}
		}
		bool Changed = false;
		bool RecoveryCreated = false;
		if(RecoveryIndex >= m_vPendingEpisodes.size())
		{
			if(m_vPendingEpisodes.size() >= MAX_PENDING_EPISODES)
			{
				SetRejectReason("active episode limit");
				return false;
			}
			SImpactPendingEpisode Recovery;
			Recovery.m_EpisodeId = m_NextEpisodeId++;
			Recovery.m_Type = EImpactEventType::SAVE_CONFIRMED;
			Recovery.m_Actor = DecisiveActor;
			Recovery.m_Target = Target;
			Recovery.m_TeamId = TeamId;
			Recovery.m_StartTick = ActionTick;
			Recovery.m_RestoreTick = RestorationObserved && !TargetFrame.m_Frozen ? ServerTick : -1;
			Recovery.m_RequiredTicks = ConfirmationTicks(TickSpeed);
			Recovery.m_State = EImpactEpisodeState::NONE;
			Recovery.m_CausalConfidence = UniqueActor ? ECausalConfidence::CONFIRMED : ECausalConfidence::AMBIGUOUS;
			Recovery.m_OwnershipStatus = UniqueActor ? EOwnershipStatus::ACTOR : EOwnershipStatus::AMBIGUOUS;
			Recovery.m_TargetAlive = TargetFrame.m_Alive;
			Recovery.m_TargetUnfrozen = !TargetFrame.m_Frozen;
			Recovery.m_TargetWasEligible = TargetFrame.m_Eligible;
			Recovery.m_PhysicalChangeEvidence = true;
			Recovery.m_DecisivePhysicalEvidence = true;
			Recovery.m_RelatedFallEpisodeId = AssociatedFallEpisodeId;
			Recovery.m_BeforeContext = "target was in the current problem before teammate action";
			Recovery.m_AfterContext = "target-specific action created a safe recovery";
			Recovery.m_DebugReason = "recovery record created; awaiting stable 0.7 second survival";
			for(const SCausalInteraction &Interaction : vInteractions)
			{
				if(Interaction.m_Target != Target || !Interaction.m_TargetSpecificEvidence)
					continue;
				Recovery.m_HookEvidence |= Interaction.m_HookEvidence;
				Recovery.m_HammerEvidence |= Interaction.m_HammerEvidence;
				Recovery.m_HookControlContinuity |= Interaction.m_HookControlContinuity;
				if(Interaction.m_HookFirstTick >= 0 &&
					(Recovery.m_HookFirstTick < 0 || Interaction.m_HookFirstTick < Recovery.m_HookFirstTick))
					Recovery.m_HookFirstTick = Interaction.m_HookFirstTick;
				Recovery.m_HookLastTick = std::max(Recovery.m_HookLastTick, Interaction.m_HookLastTick);
			}
			for(const SParticipantId &Contributor : vContributors)
				if(!UniqueActor || Contributor != DecisiveActor)
					AddHelper(Recovery.m_vHelpers, Contributor);
			m_vPendingEpisodes.push_back(std::move(Recovery));
			RecoveryIndex = m_vPendingEpisodes.size() - 1;
			Changed = true;
			RecoveryCreated = true;
		}
		SImpactPendingEpisode &Recovery = m_vPendingEpisodes[RecoveryIndex];
		if(Recovery.m_RelatedFallEpisodeId != 0)
		{
			for(const SImpactPendingEpisode &FallEpisode : m_vPendingEpisodes)
			{
				if(FallEpisode.m_EpisodeId == Recovery.m_RelatedFallEpisodeId &&
					FallEpisode.m_Type == EImpactEventType::FALL_CONFIRMED)
				{
					Recovery.m_RelatedFallAlreadyConfirmed = IsFallConfirmed(FallEpisode);
					break;
				}
			}
		}
		const EOwnershipStatus NewOwnership = UniqueActor ? EOwnershipStatus::ACTOR : EOwnershipStatus::AMBIGUOUS;
		// Once a unique decisive actor has been recorded, later touches cannot steal
		// the recovery. If ownership was ambiguous, a later uniquely causal action
		// may resolve it without changing the Fall state.
		if(Recovery.m_OwnershipStatus != EOwnershipStatus::ACTOR && UniqueActor)
		{
			Recovery.m_Actor = DecisiveActor;
			Recovery.m_OwnershipStatus = NewOwnership;
			Recovery.m_CausalConfidence = ECausalConfidence::CONFIRMED;
			Changed = true;
		}
		if(Recovery.m_RestoreTick < 0 && RestorationObserved && !TargetFrame.m_Frozen)
		{
			Recovery.m_RestoreTick = ServerTick;
			Recovery.m_TargetUnfrozen = true;
			Recovery.m_TargetAlive = TargetFrame.m_Alive;
			Changed = true;
		}
		Recovery.m_PhysicalChangeEvidence = true;
		Recovery.m_DecisivePhysicalEvidence = true;
		for(const SParticipantId &Contributor : vContributors)
			if(!UniqueActor || Contributor != DecisiveActor)
				AddHelper(Recovery.m_vHelpers, Contributor);
		if(Recovery.m_OwnershipStatus == EOwnershipStatus::ACTOR && Recovery.m_Actor.IsValid())
			Recovery.m_vHelpers.erase(std::remove(Recovery.m_vHelpers.begin(), Recovery.m_vHelpers.end(), Recovery.m_Actor), Recovery.m_vHelpers.end());
		const bool HadClutchCandidate = Recovery.m_ClutchCandidate;
		MaybeMarkClutchCandidate(Recovery, vContextFrames, pOutput);
		Changed |= !HadClutchCandidate && Recovery.m_ClutchCandidate;
		if(pOutput != nullptr && RecoveryCreated)
		{
			SImpactPhase2Debug Debug;
			Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
			Debug.m_DebugLabel = "CLUTCH_PENDING_ATTACH";
			Debug.m_EventType = Recovery.m_Type;
			Debug.m_EpisodeId = Recovery.m_EpisodeId;
			Debug.m_ClutchEpisodeId = Recovery.m_ClutchEpisodeId != 0 ? Recovery.m_ClutchEpisodeId : m_ClutchEpisodeId;
			Debug.m_Target = Recovery.m_Target;
			Debug.m_PotentialSaver = Recovery.m_Actor;
			Debug.m_TeamId = Recovery.m_TeamId;
			Debug.m_ServerTick = ServerTick;
			Debug.m_ClutchArmedTick = Recovery.m_ClutchArmedTick;
			Debug.m_StartTick = Recovery.m_StartTick;
			Debug.m_RestorationTick = Recovery.m_RestoreTick;
			Debug.m_CandidateCreated = true;
			Debug.m_Reason = "canonical_save_candidate=YES clutch_episode_armed=" + std::to_string(m_ClutchEpisodeActive ? 1 : 0) +
				" clutch_actor=" + std::to_string(m_ClutchActor.m_ClientId) + ":" + std::to_string(m_ClutchActor.m_SlotGeneration) +
				" actor_match=" + std::to_string(Recovery.m_Actor == m_ClutchActor ? 1 : 0) +
				" clutch_candidate=" + std::to_string(Recovery.m_ClutchCandidate ? 1 : 0) +
				" episode_consumed=" + std::to_string(m_ClutchEpisodeConsumed ? 1 : 0) +
				" ownership=" + OwnershipStatusName(Recovery.m_OwnershipStatus) +
				" target_exclusion=APPLIED";
			AddDebug(pOutput, Debug);
		}
		for(const SCausalInteraction &Interaction : vInteractions)
		{
			if(Interaction.m_Target != Target || !Interaction.m_TargetSpecificEvidence)
				continue;
			Recovery.m_HookEvidence |= Interaction.m_HookEvidence;
			Recovery.m_HammerEvidence |= Interaction.m_HammerEvidence;
			Recovery.m_HookControlContinuity |= Interaction.m_HookControlContinuity;
			if(Interaction.m_HookFirstTick >= 0 &&
				(Recovery.m_HookFirstTick < 0 || Interaction.m_HookFirstTick < Recovery.m_HookFirstTick))
				Recovery.m_HookFirstTick = Interaction.m_HookFirstTick;
			Recovery.m_HookLastTick = std::max(Recovery.m_HookLastTick, Interaction.m_HookLastTick);
		}
		// The old persisted proof slot is retained only as a compact compatibility
		// record for history/debug readers. The new raw Save resolver never consults
		// it as permission, and the Fall resolver never consults it at all.
		SImpactCausalRecoveryProof &CompatibilityProof = Recovery.m_CausalRecoveryProof;
		if(!CompatibilityProof.m_Valid)
		{
			CompatibilityProof = {};
			CompatibilityProof.m_Valid = true;
			CompatibilityProof.m_ProofId = m_NextRecoveryProofId++;
		}
		CompatibilityProof.m_FallEpisodeId = Recovery.m_RelatedFallEpisodeId;
		CompatibilityProof.m_RecoveryEpisodeId = Recovery.m_EpisodeId;
		CompatibilityProof.m_Target = Recovery.m_Target;
		CompatibilityProof.m_DecisiveActor = Recovery.m_Actor;
		CompatibilityProof.m_TeamId = Recovery.m_TeamId;
		CompatibilityProof.m_HarmStartTick = ProblemStartTick;
		CompatibilityProof.m_ActionTick = ActionTick;
		CompatibilityProof.m_FirstProvenTick = EarliestSafeTick;
		CompatibilityProof.m_RestoreTick = Recovery.m_RestoreTick;
		CompatibilityProof.m_HookFirstTick = Recovery.m_HookFirstTick;
		CompatibilityProof.m_HookLastTick = Recovery.m_HookLastTick;
		CompatibilityProof.m_PreActionOutcome = pEarliestInteraction != nullptr ?
			pEarliestInteraction->m_PreActionOutcome : EImpactPhysicalOutcome::UNKNOWN;
		CompatibilityProof.m_PostActionOutcome = EImpactPhysicalOutcome::SAFE;
		CompatibilityProof.m_TargetAlreadySafeBeforeAction = false;
		CompatibilityProof.m_TargetSpecificEvidence = true;
		CompatibilityProof.m_PhysicalChangeEvidence = true;
		CompatibilityProof.m_SafeOutcomeEvidence = true;
		CompatibilityProof.m_HookEvidence = Recovery.m_HookEvidence;
		CompatibilityProof.m_HammerEvidence = Recovery.m_HammerEvidence;
		CompatibilityProof.m_OwnershipStatus = Recovery.m_OwnershipStatus;
		CompatibilityProof.m_Confidence = Recovery.m_CausalConfidence;
		CompatibilityProof.m_vContributors = vContributors;
		CompatibilityProof.m_Reason = "compatibility recovery record; raw Save uses episode facts directly";
		if(Recovery.m_RelatedFallEpisodeId != 0)
		{
			for(SImpactPendingEpisode &FallEpisode : m_vPendingEpisodes)
			{
				if(FallEpisode.m_EpisodeId == Recovery.m_RelatedFallEpisodeId &&
					FallEpisode.m_Type == EImpactEventType::FALL_CONFIRMED)
				{
					if(FallEpisode.m_RelatedRecoveryEpisodeId != Recovery.m_EpisodeId)
					{
						FallEpisode.m_RelatedRecoveryEpisodeId = Recovery.m_EpisodeId;
						Changed = true;
					}
					break;
				}
			}
		}
		if(pOutput != nullptr && Changed)
		{
			SImpactPhase2Debug Debug;
			FillDebugFrame(Debug, TargetFrame);
			Debug.m_DebugLabel = "SAVE_PENDING";
			Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
			Debug.m_EventType = EImpactEventType::SAVE_CONFIRMED;
			Debug.m_EpisodeId = Recovery.m_EpisodeId;
			Debug.m_RelatedRecoveryEpisodeId = Recovery.m_EpisodeId;
			Debug.m_Target = Recovery.m_Target;
			Debug.m_PotentialSaver = Recovery.m_Actor;
			Debug.m_TeamId = Recovery.m_TeamId;
			Debug.m_StartTick = Recovery.m_StartTick;
			Debug.m_RestorationTick = Recovery.m_RestoreTick;
			Debug.m_HookEvidence = Recovery.m_HookEvidence;
			Debug.m_HammerEvidence = Recovery.m_HammerEvidence;
			Debug.m_TargetSpecificEvidence = true;
			Debug.m_PhysicalChangeEvidence = true;
			Debug.m_DecisivePhysicalEvidence = true;
			Debug.m_RecoveryCreated = true;
			Debug.m_CandidateCreated = true;
			Debug.m_PreActionSafeKnown = pEarliestInteraction != nullptr &&
				pEarliestInteraction->m_PreActionAutonomousSafe;
			Debug.m_PreActionSafe = Debug.m_PreActionSafeKnown;
			Debug.m_PreActionSafeSource = Debug.m_PreActionSafe ? "NATIVE_SELF_SAFE" : "NONE";
			Debug.m_NativePreActionResult = pEarliestInteraction != nullptr && pEarliestInteraction->m_PreActionNativeEvaluated ?
				(pEarliestInteraction->m_PreActionNativeSelfSafe ? "SELF_SAFE" : "UNRESOLVED") : "NOT_EVALUATED";
			Debug.m_PlatformPreexistedBeforeAction = pEarliestInteraction != nullptr &&
				pEarliestInteraction->m_PreActionPlatformPreexisted;
			Debug.m_FreezeRefreshObservedBeforeAction = pEarliestInteraction != nullptr &&
				pEarliestInteraction->m_PreActionFreezeRefreshObserved;
			Debug.m_Reason = "target-specific physical change created safe recovery; Save awaits 0.7 second survival";
			AddDebug(pOutput, Debug);
		}
		return Changed;
	}

	bool CImpactPhase2::HasPreHelperRescueFallCandidate(const SImpactPendingEpisode &Recovery,
		const SImpactPendingEpisode &FallEpisode, const std::vector<SCausalInteraction> &vInteractions) const
	{
		// This is a narrow bridge for the case where the canonical recovery record
		// was created before the independent Fall resolver had a decisive result.
		// It is not a Save-to-Fall rule: every condition below must describe the
		// same bound target/actor/problem episode and an already observed causal
		// recovery. In particular, an interaction without a pre-action native
		// evaluation is intentionally insufficient; that preserves the existing
		// ambiguous-helper Save-without-Fall behavior.
		if(Recovery.m_Type != EImpactEventType::SAVE_CONFIRMED ||
			FallEpisode.m_Type != EImpactEventType::FALL_CONFIRMED ||
			Recovery.m_RelatedFallEpisodeId != FallEpisode.m_EpisodeId ||
			Recovery.m_Target != FallEpisode.m_Target || Recovery.m_TeamId != FallEpisode.m_TeamId ||
			Recovery.m_OwnershipStatus != EOwnershipStatus::ACTOR || !Recovery.m_Actor.IsValid() ||
			!Recovery.m_PhysicalChangeEvidence || !Recovery.m_DecisivePhysicalEvidence ||
			IsFallConfirmed(FallEpisode))
			return false;

		// A positive pre-action SELF_SAFE interaction in this same episode vetoes
		// this causal bridge. Later movement cannot turn an already self-recoverable
		// target into a Fall merely because another recovery record was eventually
		// materialized.
		for(const SCausalInteraction &Interaction : vInteractions)
		{
			if(Interaction.m_Target == Recovery.m_Target && Interaction.m_RelevantToThisFreezeEpisode &&
				Interaction.m_FirstTick >= FallEpisode.m_StartTick &&
				(Recovery.m_StartTick < 0 || Interaction.m_FirstTick <= Recovery.m_StartTick) &&
				(Interaction.m_PreActionAutonomousSafe || Interaction.m_PreActionNativeSelfSafe))
				return false;
		}

		for(const SCausalInteraction &Interaction : vInteractions)
		{
			if(Interaction.m_Actor != Recovery.m_Actor || Interaction.m_Target != Recovery.m_Target ||
				!Interaction.m_RelevantToThisFreezeEpisode || !Interaction.m_TargetSpecificEvidence ||
				Interaction.m_FirstTick < FallEpisode.m_StartTick ||
				(Recovery.m_StartTick >= 0 && Interaction.m_FirstTick > Recovery.m_StartTick) ||
				!Interaction.m_PreActionProblemStateKnown || !Interaction.m_PreActionNativeEvaluated ||
				Interaction.m_PreActionAutonomousSafe || Interaction.m_PreActionNativeSelfSafe ||
				Interaction.m_PreActionOutcome == EImpactPhysicalOutcome::SAFE ||
				!Interaction.m_PhysicalChangeObserved || !Interaction.m_CausalContributionObserved)
				continue;
			return true;
		}
		return false;
	}

	bool CImpactPhase2::ConfirmFallEpisode(uint64_t EpisodeId, int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const char *pReason, bool KeepEpisode, bool FrozenNow, const SImpactGameplayFrame *pPhysicalFrame)
	{
		if(EpisodeId == 0 || pOutput == nullptr)
			return false;
		for(size_t i = 0; i < m_vPendingEpisodes.size(); ++i)
		{
			SImpactPendingEpisode &Episode = m_vPendingEpisodes[i];
			if(Episode.m_EpisodeId != EpisodeId || Episode.m_Type != EImpactEventType::FALL_CONFIRMED)
				continue;
			const bool WasAlreadyConfirmed = IsFallConfirmed(Episode);
			if(WasAlreadyConfirmed)
			{
				SImpactPhase2Debug Debug;
				FillDebugEpisode(Debug, Episode);
				Debug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
				Debug.m_DebugLabel = "FALL_DUPLICATE_SUPPRESSED";
				Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
				Debug.m_ServerTick = ServerTick;
						Debug.m_EpisodeId = Episode.m_EpisodeId;
						Debug.m_ProblemEpisodeId = Episode.m_EpisodeId;
				Debug.m_Target = Episode.m_Target;
				Debug.m_EpisodeState = EImpactEpisodeState::CONFIRMED_FALL;
				Debug.m_ConfirmedFall = false;
				Debug.m_FallAlreadyConfirmed = true;
				Debug.m_Reason = "Fall already confirmed; duplicate suppressed";
				AddDebug(pOutput, Debug);
				// A duplicate corroborating signal must never consume the retained
				// confirmed-origin context. Recovery binding may still need this object
				// after the same tick, and confirmed Fall is immutable independently of
				// the caller's transient cleanup preference.
				return true;
			}

			SImpactEvent Event = MakeEvent(EImpactEventType::FALL_CONFIRMED, TeamId, Episode.m_Target, Episode.m_Target,
				Episode.m_StartTick, ServerTick, pReason ? pReason : "server-confirmed frozen problem without autonomous recovery");
			Event.m_EpisodeState = EImpactEpisodeState::CONFIRMED_FALL;
			Event.m_CausalConfidence = ECausalConfidence::CONFIRMED;
			Event.m_OwnershipStatus = EOwnershipStatus::NONE;
			Event.m_BeforeContext = Episode.m_BeforeContext;
			Event.m_AfterContext = "server-confirmed frozen problem without autonomous recovery";
			Event.m_vTags.push_back("FALL");
			Event.m_RawDelta.m_Falls = 1;
			const EImpactEpisodeState StateBeforeConfirmation = Episode.m_State;
			SImpactPhase2Debug CandidateDebug;
			FillDebugEpisode(CandidateDebug, Episode);
			CandidateDebug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
			CandidateDebug.m_DebugLabel = "FALL_CANDIDATE_CREATED";
			CandidateDebug.m_EventType = EImpactEventType::FALL_CONFIRMED;
			CandidateDebug.m_EpisodeId = Episode.m_EpisodeId;
			CandidateDebug.m_ProblemEpisodeId = Episode.m_EpisodeId;
			CandidateDebug.m_Target = Episode.m_Target;
			CandidateDebug.m_TeamId = Episode.m_TeamId;
			CandidateDebug.m_ServerTick = ServerTick;
			CandidateDebug.m_EpisodeStateBefore = StateBeforeConfirmation;
			CandidateDebug.m_EpisodeStateAfter = EImpactEpisodeState::CONFIRMED_FALL;
			CandidateDebug.m_PhysicalOutcome = Episode.m_PhysicalOutcome;
			CandidateDebug.m_RawDelta.m_Falls = 1;
			CandidateDebug.m_Reason = Event.m_CausalReason;
			AddDebug(pOutput, CandidateDebug);
			pOutput->m_vEvents.push_back(std::move(Event));
			Episode.m_FallAlreadyConfirmed = true;
			Episode.m_State = EImpactEpisodeState::CONFIRMED_FALL;
			for(SImpactPendingEpisode &RecoveryEpisode : m_vPendingEpisodes)
			{
				if(RecoveryEpisode.m_Type == EImpactEventType::SAVE_CONFIRMED &&
					RecoveryEpisode.m_RelatedFallEpisodeId == Episode.m_EpisodeId)
					RecoveryEpisode.m_RelatedFallAlreadyConfirmed = true;
			}

			SImpactPhase2Debug Debug;
			FillDebugEpisode(Debug, Episode);
			if(pPhysicalFrame != nullptr)
			{
				Debug.m_FreezeKnown = pPhysicalFrame->m_FreezeKnown;
				Debug.m_FrozenNow = pPhysicalFrame->m_Frozen;
				Debug.m_FreezeStart = pPhysicalFrame->m_FreezeStart;
				Debug.m_FreezeEnd = pPhysicalFrame->m_FreezeEnd;
				Debug.m_PositionKnown = pPhysicalFrame->m_PositionKnown;
				Debug.m_X = pPhysicalFrame->m_X;
				Debug.m_Y = pPhysicalFrame->m_Y;
				Debug.m_VelocityKnown = pPhysicalFrame->m_VelocityKnown;
				Debug.m_VelX = pPhysicalFrame->m_VelX;
				Debug.m_VelY = pPhysicalFrame->m_VelY;
				Debug.m_SupportKnown = pPhysicalFrame->m_SupportKnown;
				Debug.m_Supported = pPhysicalFrame->m_Supported;
				Debug.m_SafeCollisionPositionKnown = pPhysicalFrame->m_SafeCollisionPositionKnown;
				Debug.m_SafeCollisionPosition = pPhysicalFrame->m_SafeCollisionPosition;
				Debug.m_OrdinaryFreezeTileKnown = pPhysicalFrame->m_OrdinaryFreezeTileKnown;
				Debug.m_OrdinaryFreezeTile = pPhysicalFrame->m_OrdinaryFreezeTile;
			}
			Debug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
			Debug.m_DebugLabel = "FALL_CONFIRM";
			Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
			Debug.m_ServerTick = ServerTick;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_ProblemEpisodeId = Episode.m_EpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_TeamId = Episode.m_TeamId;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_FrozenBefore = true;
			Debug.m_FrozenNow = FrozenNow;
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_PhysicalOutcome = Episode.m_PhysicalOutcome;
			Debug.m_HookEvidence = Episode.m_HookEvidence;
			Debug.m_HammerEvidence = Episode.m_HammerEvidence;
			Debug.m_HookFirstTick = Episode.m_HookFirstTick;
			Debug.m_HookLastTick = Episode.m_HookLastTick;
			Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
			Debug.m_EpisodeState = EImpactEpisodeState::CONFIRMED_FALL;
			Debug.m_EpisodeStateBefore = StateBeforeConfirmation;
			Debug.m_EpisodeStateAfter = EImpactEpisodeState::CONFIRMED_FALL;
			Debug.m_OwnershipStatus = EOwnershipStatus::NONE;
			Debug.m_HarmCause = Episode.m_HarmCause;
			Debug.m_HarmCausalConfidence = Episode.m_HarmCausalConfidence;
			Debug.m_ConfirmedFall = true;
			Debug.m_FallAlreadyConfirmed = true;
			Debug.m_RawDelta.m_Falls = 1;
			Debug.m_CausalConfidence = Event.m_CausalConfidence;
			Debug.m_Reason = pReason ? pReason : "server-confirmed frozen problem without autonomous recovery";
			AddDebug(pOutput, Debug);

			if(!KeepEpisode)
				m_vPendingEpisodes.erase(m_vPendingEpisodes.begin() + i);
			return true;
		}
		return false;
	}

	void CImpactPhase2::CloseStableProblemEpisodes(int64_t ServerTick, int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		const int64_t RequiredTicks = ConfirmationTicks(TickSpeed);
		for(const SImpactGameplayFrame &Target : vFrames)
		{
			if(!Target.m_Confirmed || !Target.m_Present || !Target.m_CurrentCharacterKnown || !Target.m_Active ||
				!Target.m_Eligible || !Target.m_AliveKnown || !Target.m_Alive || !Target.m_FreezeKnown ||
				!Target.m_SoloKnown || Target.m_Solo || Target.m_TeamId != m_TeamId || !Target.m_Participant.IsValid() ||
				!IsValidClientId(Target.m_Participant.m_ClientId))
				continue;
			SObservedState &Observed = m_aObserved[Target.m_Participant.m_ClientId];
			if(!Observed.m_Valid || !SameParticipant(Observed.m_Frame, Target) || Observed.m_AliveUnfrozenSince < 0)
				continue;
			const int64_t ResolveTick = Target.m_ServerTick >= 0 ? Target.m_ServerTick : ServerTick;
			if(ResolveTick < Observed.m_AliveUnfrozenSince ||
				ResolveTick - Observed.m_AliveUnfrozenSince < RequiredTicks)
				continue;
			const bool ClosedBeforeNewFreeze = Target.m_Frozen && Observed.m_Frame.m_FreezeKnown && !Observed.m_Frame.m_Frozen;
			if(Target.m_Frozen && !ClosedBeforeNewFreeze)
				continue;

			bool PreserveObservedRescueContext = false;
			for(size_t i = 0; i < m_vPendingEpisodes.size();)
			{
				const SImpactPendingEpisode &Episode = m_vPendingEpisodes[i];
				if(Episode.m_Type != EImpactEventType::FALL_CONFIRMED || Episode.m_Target != Target.m_Participant ||
					Episode.m_TeamId != m_TeamId)
				{
					++i;
					continue;
				}
				SImpactPhase2Debug Debug;
				FillDebugEpisode(Debug, Episode);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				const bool FallConfirmed = IsFallConfirmed(Episode);
				const bool CausalRescuePending = !FallConfirmed && std::any_of(m_vPendingEpisodes.begin(), m_vPendingEpisodes.end(),
					[this, &Episode, &Observed](const SImpactPendingEpisode &Recovery) {
						return HasPreHelperRescueFallCandidate(Recovery, Episode, Observed.m_vFrozenInteractions);
					});
				if(CausalRescuePending)
				{
					// The target has a canonical recovery record, but its 35-tick Save
					// confirmation is not complete yet. Keep the same Fall episode and
					// pre-action interaction evidence alive; closing it here would turn a
					// valid fast rescue into Fall=0 before ResolvePending can finish the
					// independent Save confirmation.
					PreserveObservedRescueContext = true;
					++i;
					continue;
				}
				bool SafeBeforeAnyTargetAction = false;
				// An autonomous-safe latch is meaningful here only when no
				// target-specific action ever existed. Once an interaction is present,
				// its immutable first-action baseline is the only pre-action evidence;
				// the current/post-action latch must not relabel the recovery as self-safe.
				if(Observed.m_vFrozenInteractions.empty())
					SafeBeforeAnyTargetAction =
						(Observed.m_AutonomousSelfRecoveryProven &&
							Observed.m_AutonomousSelfRecoveryEpisodeId == Episode.m_EpisodeId) ||
						(Observed.m_HasLastKnownFreeze && CanSelfRecover(Observed.m_LastKnownFreezeFrame));
				if(!Observed.m_vFrozenInteractions.empty())
				{
					const auto FirstInteraction = std::min_element(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
						[](const SCausalInteraction &A, const SCausalInteraction &B) { return A.m_FirstTick < B.m_FirstTick; });
					if(FirstInteraction != Observed.m_vFrozenInteractions.end())
					{
						const int64_t FirstInteractionTick = FirstInteraction->m_FirstTick;
						SafeBeforeAnyTargetAction |= std::any_of(Observed.m_vFrozenInteractions.begin(), Observed.m_vFrozenInteractions.end(),
							[FirstInteractionTick](const SCausalInteraction &Interaction) {
							return Interaction.m_FirstTick == FirstInteractionTick &&
								(Interaction.m_PreActionAutonomousSafe ||
									(Interaction.m_PreActionSafeCollisionPositionKnown && Interaction.m_PreActionSafeCollisionPosition));
							});
					}
				}
				// A stable unfrozen boundary with no causal teammate recovery is an
				// observed self-recovery outcome. It is not inferred from UNKNOWN
				// PhysicalOutcome. External recovery is resolved independently at the
				// restoration boundary above, so the Fall cleanup path must not use a
				// Save/recovery object as a permission gate or fallback.
				const char *pNoFallReason = SafeBeforeAnyTargetAction ? "NO_FALL_SELF_SAFE" :
					"NO_FALL_SELF_RECOVERY";
				Debug.m_DebugLabel = FallConfirmed ? "FALL_EPISODE_CLOSED" : "FALL_NOT_EMITTED";
				Debug.m_EventType = EImpactEventType::FALL_CONFIRMED;
				Debug.m_EpisodeId = Episode.m_EpisodeId;
				Debug.m_ServerTick = ResolveTick;
				Debug.m_EmissionTick = ServerTick;
				Debug.m_Target = Episode.m_Target;
				Debug.m_ConfirmedTicks = ResolveTick - Observed.m_AliveUnfrozenSince;
				Debug.m_RequiredTicks = RequiredTicks;
				Debug.m_FallAlreadyConfirmed = IsFallConfirmed(Episode);
				Debug.m_ConfirmedFall = Debug.m_FallAlreadyConfirmed;
				Debug.m_EpisodeState = FallConfirmed ? EImpactEpisodeState::CONFIRMED_FALL : EImpactEpisodeState::CANCELLED;
				Debug.m_FallSuppressed = !FallConfirmed;
				// A stable boundary closes the active episode, but it does not turn
				// missing physical evidence into a SAFE outcome. SAFE remains a
				// positive-frame compatibility fact only; the raw Fall decision is
				// based on the explicit autonomous/team predicates above.
				Debug.m_Reason = IsFallConfirmed(Episode) ?
					"stable alive/present/unfrozen boundary closed problem episode; confirmed Fall unchanged" :
					pNoFallReason;
				AddDebug(pOutput, Debug);
				m_vPendingEpisodes.erase(m_vPendingEpisodes.begin() + static_cast<ptrdiff_t>(i));
			}
			if(PreserveObservedRescueContext)
				continue;

			// This boundary belongs to the physical problem lifecycle, not the Save
			// result. Only now is it safe to discard target-specific history and allow a
			// subsequent freeze to receive a new problem identity.
			Observed.m_vFrozenInteractions.clear();
			Observed.m_EpisodeRecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
			Observed.m_RestorationObserved = false;
			Observed.m_RestorationTick = -1;
			Observed.m_HasLastKnownFreeze = false;
			Observed.m_ProblemStartTick = -1;
			Observed.m_ProblemOriginTeammateCaused = false;
			Observed.m_SelfSafeInvalidatedTick = -1;
			Observed.m_PostInvalidationHarmfulEvidenceTick = -1;
			Observed.m_AutonomousSelfRecoveryProven = false;
			Observed.m_AutonomousSelfRecoveryEpisodeId = 0;
			Observed.m_AutonomousSelfRecoverySourceTick = -1;
			Observed.m_AutonomousSelfRecoveryNativeProof = false;
			Observed.m_NativeAutonomousResultObserved = false;
			Observed.m_NativeAutonomousResultSelfSafe = false;
			Observed.m_NativeAutonomousResultTick = -1;
		}
	}

	void CImpactPhase2::ResolvePending(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames)
	{
		auto RearmRecoveryAfterRefreeze = [&](const SImpactPendingEpisode &Episode, const SImpactGameplayFrame &Target) {
			if(!IsValidClientId(Episode.m_Target.m_ClientId))
				return;
			SObservedState &Observed = m_aObserved[Episode.m_Target.m_ClientId];
			if(!Observed.m_Valid || !SameParticipant(Observed.m_Frame, Target))
				return;
			// A sub-35-tick refreeze invalidates the previous SAFE boundary, not
			// the continuous problem episode or its interaction history. Re-arm
			// physical proof so a later rehook must establish a new observed
			// post-action change; the old candidate cannot be resurrected from its
			// stale transition or first-safe tick.
			Observed.m_RestorationObserved = false;
			Observed.m_RestorationTick = -1;
			for(SCausalInteraction &Interaction : Observed.m_vFrozenInteractions)
			{
				if(!Interaction.m_CausalSafeTransitionObserved)
					continue;
				Interaction.m_CausalSafeTransitionObserved = false;
				Interaction.m_SafeTransitionTick = -1;
				Interaction.m_PostActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
				// Reject only the failed confirmation attempt. The physical change that
				// already occurred while the target was frozen remains valid evidence for
				// this same continuous rescue/problem episode, so a later restoration can
				// start a fresh 35-tick confirmation without requiring an immortal hook
				// or a new proof object.
				if(Interaction.m_ActionActive)
				{
					Interaction.m_ActionStartTick = ServerTick;
					Interaction.m_ActionStartPositionKnown = Target.m_PositionKnown;
					Interaction.m_ActionStartX = Target.m_X;
					Interaction.m_ActionStartY = Target.m_Y;
					Interaction.m_ActionStartVelocityKnown = Target.m_VelocityKnown;
					Interaction.m_ActionStartVelX = Target.m_VelX;
					Interaction.m_ActionStartVelY = Target.m_VelY;
					Interaction.m_TrackPostActionOutcome = true;
				}
				else
				{
					Interaction.m_ActionStartTick = -1;
					Interaction.m_ActionStartPositionKnown = false;
					Interaction.m_ActionStartVelocityKnown = false;
					Interaction.m_TrackPostActionOutcome = false;
				}
			}
		};
			auto CloseConfirmedClutchEpisode = [&](const SImpactPendingEpisode &ConfirmedEpisode) {
				const uint64_t ClosedClutchEpisodeId = ConfirmedEpisode.m_ClutchEpisodeId != 0 ?
					ConfirmedEpisode.m_ClutchEpisodeId : m_ClutchEpisodeId;
				const bool PreserveNewerActiveEpisode = m_ClutchEpisodeActive && m_ClutchEpisodeId != 0 &&
					m_ClutchEpisodeId != ClosedClutchEpisodeId;
			if(ClosedClutchEpisodeId == 0 && !m_ClutchEpisodeActive && !m_ClutchEpisodeConsumed)
				return;

			// A successful Clutch is the authoritative end of this Clutch crisis. Do
			// not wait for a later roster/freeze transition to clear the state: the next
			// genuine problem episode must be able to arm immediately with a new id.
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "CLUTCH_EPISODE_CLEAR";
				Debug.m_EventType = ConfirmedEpisode.m_Type;
				Debug.m_EpisodeId = ConfirmedEpisode.m_EpisodeId;
				Debug.m_ClutchEpisodeId = ClosedClutchEpisodeId;
				Debug.m_Target = ConfirmedEpisode.m_Target;
				Debug.m_PotentialSaver = ConfirmedEpisode.m_Actor;
				Debug.m_TeamId = ConfirmedEpisode.m_TeamId;
				Debug.m_ServerTick = ServerTick;
				Debug.m_ClutchArmedTick = ConfirmedEpisode.m_ClutchArmedTick;
				Debug.m_StartTick = ConfirmedEpisode.m_StartTick;
				Debug.m_RestorationTick = ConfirmedEpisode.m_RestoreTick;
				Debug.m_CandidateCreated = false;
				Debug.m_Reason = "reason=CLUTCH_CONFIRMED closed_clutch_episode_id=" + std::to_string(ClosedClutchEpisodeId) +
					" save_episode_id=" + std::to_string(ConfirmedEpisode.m_EpisodeId) +
					" newer_episode_preserved=" + std::to_string(PreserveNewerActiveEpisode ? 1 : 0);
				AddDebug(pOutput, Debug);
			}

			// A second recovery record that was already carrying the old side-car id
			// belongs to the completed rescue, not to a future crisis. Clear only that
			// side-car metadata; the canonical Save records remain untouched.
			if(ClosedClutchEpisodeId != 0)
			{
				for(SImpactPendingEpisode &Pending : m_vPendingEpisodes)
				{
					if(Pending.m_EpisodeId == ConfirmedEpisode.m_EpisodeId ||
						Pending.m_ClutchEpisodeId != ClosedClutchEpisodeId)
						continue;
					Pending.m_ClutchCandidate = false;
					Pending.m_ClutchArmedTick = -1;
					Pending.m_ClutchEpisodeId = 0;
				}
			}
			// The 35-tick Save confirmation can finish after a separate new crisis
			// has already armed another Clutch episode. Closing the confirmed
			// Save's older side-car must not clear that newer episode or put it
			// behind the post-confirmation barrier.
			if(PreserveNewerActiveEpisode)
				return;
			// A distinct problem can start after restoration but before the prior
			// Save reaches its 35-tick confirmation. If it was still UNKNOWN at that
			// moment, it could not arm a newer Clutch episode before this close. Do
			// not require its one-frame start edge to happen again: the current
			// generation-scoped observed episode is sufficient once the old Save's
			// own restoration boundary proves it is newer.
			const bool HasProblemAfterConfirmedRescue = ConfirmedEpisode.m_RestoreTick >= 0 &&
				std::any_of(vFrames.begin(), vFrames.end(), [this, &ConfirmedEpisode, TeamId](const SImpactGameplayFrame &Frame) {
					if(!Frame.m_Confirmed || !Frame.m_Present || !Frame.m_CurrentCharacterKnown || !Frame.m_Active ||
						!Frame.m_Eligible || !Frame.m_AliveKnown || !Frame.m_Alive || !Frame.m_FreezeKnown || !Frame.m_Frozen ||
						!Frame.m_SoloKnown || Frame.m_Solo || Frame.m_TeamId != TeamId || !Frame.m_Participant.IsValid() ||
						!IsValidClientId(Frame.m_Participant.m_ClientId))
						return false;
					const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
					return Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame) &&
						Observed.m_ProblemStartTick > ConfirmedEpisode.m_RestoreTick;
				});

			m_ClutchEpisodeActive = false;
			// Keep the consumed bit set behind the barrier unless an already-started
			// physical problem is newer than this Save's restoration. This only
			// releases the barrier for a distinct current episode; old unresolved
			// problems remain consumed.
			m_ClutchEpisodeConsumed = !HasProblemAfterConfirmedRescue;
			m_ClutchAwaitingNewCrisis = !HasProblemAfterConfirmedRescue;
			m_ClutchActor = {};
			m_ClutchArmedTick = -1;
			m_ClutchEpisodeId = 0;
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
			m_LastClutchEvaluationKey.clear();
		};
		for(size_t i = 0; i < m_vPendingEpisodes.size();)
		{
			SImpactPendingEpisode &Episode = m_vPendingEpisodes[i];
			if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED)
			{
				// A Fall episode is retained independently of the recovery timer. It
				// may still be pending or already confirmed; the Save confirmation
				// timer must never emit a second Fall.
				++i;
				continue;
			}
			const SImpactGameplayFrame *pTarget = FindParticipant(vFrames, Episode.m_Target);
			const bool MissingConfirmationFrame = pTarget == nullptr || !pTarget->m_CurrentCharacterKnown ||
				!pTarget->m_AliveKnown || !pTarget->m_FreezeKnown || !pTarget->m_SoloKnown;
			if(MissingConfirmationFrame)
			{
				// A missing confirmation frame is not an alive/unfrozen transition.
				// Keep the active recovery record and reset its confirmation baseline;
				// unavailable time cannot be counted toward Save confirmation.
				if(Episode.m_ConfirmedRecoveryStartTick >= 0 && pOutput)
				{
					SImpactPhase2Debug Debug;
					if(pTarget != nullptr)
						FillDebugFrame(Debug, *pTarget);
					else
						FillDebugEpisode(Debug, Episode);
					Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
					Debug.m_DebugLabel = "SAVE_PENDING";
					Debug.m_EventType = Episode.m_Type;
					Debug.m_EpisodeId = Episode.m_EpisodeId;
					Debug.m_Target = Episode.m_Target;
					Debug.m_PotentialSaver = Episode.m_Actor;
					Debug.m_StartTick = Episode.m_StartTick;
					Debug.m_RestorationTick = Episode.m_RestoreTick;
					Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
					Debug.m_ConfirmationStartTick = Episode.m_ConfirmedRecoveryStartTick;
					Debug.m_ConfirmedTicks = Episode.m_ConfirmedRecoveryStartTick >= 0 ? ServerTick - Episode.m_ConfirmedRecoveryStartTick : -1;
					Debug.m_RequiredTicks = Episode.m_RequiredTicks;
					Debug.m_FrozenBefore = true;
					Debug.m_HookEvidence = Episode.m_HookEvidence;
					Debug.m_HammerEvidence = Episode.m_HammerEvidence;
					Debug.m_UnfreezeCause = Episode.m_UnfreezeCause;
					Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
					Debug.m_HookFirstTick = Episode.m_HookFirstTick;
					Debug.m_HookLastTick = Episode.m_HookLastTick;
					Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
					Debug.m_EpisodeState = Episode.m_State;
					Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
					Debug.m_UnknownInterruption = true;
					Debug.m_Reason = "authoritative confirmation frame unavailable";
					FillDebugPendingEpisodeContext(Debug, Episode);
					AddDebug(pOutput, Debug);
				}
				Episode.m_ConfirmedRecoveryStartTick = -1;
				++i;
				continue;
			}
			// A causal recovery proof may be bound while the target is still frozen.
			// That is the earliest Fall/Save-proof tick, but it is not yet a
			// restoration. Keep the candidate alive until an authoritative unfrozen
			// frame supplies the restoration boundary; in particular, do not reject it
			// merely because the normal confirmation predicate sees a frozen target.
			if(Episode.m_RestoreTick < 0)
			{
				if(!pTarget->m_Frozen && pTarget->m_Confirmed && pTarget->m_Present && pTarget->m_Active &&
					pTarget->m_Eligible && pTarget->m_Alive)
				{
					Episode.m_RestoreTick = ServerTick;
					Episode.m_TargetAlive = pTarget->m_Alive;
					Episode.m_TargetUnfrozen = true;
				}
				else
				{
					++i;
					continue;
				}
			}
			// A sparse authoritative stream can omit every unfrozen frame after the
			// restoration. If the required interval has already elapsed by the time
			// the next frame arrives, confirm that completed interval before treating
			// the current frame as a new freeze. The current frozen state is then the
			// next lifecycle, not evidence that the already completed Save failed.
			const bool ConfirmationDueBeforeNewFreeze = pTarget != nullptr && pTarget->m_Frozen &&
				pTarget->m_Confirmed && pTarget->m_Present && pTarget->m_Active && pTarget->m_Eligible &&
				pTarget->m_Alive && !pTarget->m_Solo && pTarget->m_TeamId == TeamId &&
				Episode.m_ConfirmedRecoveryStartTick >= 0 && ServerTick >= Episode.m_ConfirmedRecoveryStartTick &&
				ServerTick - Episode.m_ConfirmedRecoveryStartTick >= Episode.m_RequiredTicks;
			if(pTarget == nullptr || !pTarget->m_Confirmed || !pTarget->m_Present || !pTarget->m_Active ||
				!pTarget->m_Eligible || !pTarget->m_Alive || pTarget->m_Solo || pTarget->m_TeamId != TeamId ||
				(pTarget->m_Frozen && !ConfirmationDueBeforeNewFreeze))
			{
				SImpactPhase2Debug Debug;
				if(pTarget != nullptr)
					FillDebugFrame(Debug, *pTarget);
				else
					FillDebugEpisode(Debug, Episode);
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = Episode.m_Type == EImpactEventType::FALL_CONFIRMED ?
					(IsFallConfirmed(Episode) ? "FALL_CLEANUP" : "FALL_NOT_EMITTED") :
					(pTarget != nullptr && pTarget->m_Frozen && !ConfirmationDueBeforeNewFreeze ? "SAVE_REJECTED_REFREEZE" : "SAVE_REJECTED");
				Debug.m_EventType = Episode.m_Type;
				Debug.m_EpisodeId = Episode.m_EpisodeId;
				Debug.m_Target = Episode.m_Target;
				Debug.m_PotentialSaver = Episode.m_Actor;
				Debug.m_StartTick = Episode.m_StartTick;
				Debug.m_RestorationTick = Episode.m_RestoreTick;
				Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
				Debug.m_FrozenBefore = true;
				Debug.m_FrozenNow = pTarget != nullptr && pTarget->m_Frozen;
				Debug.m_HookEvidence = Episode.m_HookEvidence;
				Debug.m_HammerEvidence = Episode.m_HammerEvidence;
				Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
				Debug.m_HookFirstTick = Episode.m_HookFirstTick;
				Debug.m_HookLastTick = Episode.m_HookLastTick;
				Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
				Debug.m_EpisodeState = Episode.m_State;
				Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
				Debug.m_Reason = pTarget == nullptr ? "target left confirmed snapshot" :
					(pTarget->m_Frozen && !ConfirmationDueBeforeNewFreeze) ? "target became frozen again before confirmation" :
					!pTarget->m_Alive ? "target did not survive confirmation window" :
					"target failed confirmed recovery conditions";
				FillDebugPendingEpisodeContext(Debug, Episode);
				AddDebug(pOutput, Debug);
				if(Episode.m_Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_RestoreTick >= 0 &&
					pTarget != nullptr && pTarget->m_Frozen && !ConfirmationDueBeforeNewFreeze)
					RearmRecoveryAfterRefreeze(Episode, *pTarget);
				m_vPendingEpisodes.erase(m_vPendingEpisodes.begin() + i);
				continue;
			}
			if(ServerTick < Episode.m_RestoreTick)
			{
				Episode.m_TargetAlive = pTarget->m_Alive;
				Episode.m_TargetUnfrozen = !pTarget->m_Frozen;
				++i;
				continue;
			}
			if(Episode.m_ConfirmedRecoveryStartTick < 0 || ServerTick < Episode.m_ConfirmedRecoveryStartTick)
			{
				// The first known-good frame after an UNKNOWN gap is a new
				// continuous-confirmation baseline. Do not include the gap itself.
				Episode.m_ConfirmedRecoveryStartTick = ServerTick;
				Episode.m_TargetAlive = pTarget->m_Alive;
				Episode.m_TargetUnfrozen = !pTarget->m_Frozen;
				if(pOutput)
				{
					SImpactPhase2Debug Debug;
					FillDebugFrame(Debug, *pTarget);
					Debug.m_DebugLabel = "SAVE_PENDING";
					Debug.m_EventType = Episode.m_Type;
					Debug.m_EpisodeId = Episode.m_EpisodeId;
					Debug.m_Target = Episode.m_Target;
					Debug.m_PotentialSaver = Episode.m_Actor;
					Debug.m_StartTick = Episode.m_StartTick;
					Debug.m_RestorationTick = Episode.m_RestoreTick;
					Debug.m_ConfirmationStartTick = Episode.m_ConfirmedRecoveryStartTick;
					Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
					Debug.m_ConfirmedTicks = 0;
					Debug.m_RequiredTicks = Episode.m_RequiredTicks;
					Debug.m_FrozenBefore = true;
					Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
					Debug.m_UnfreezeCause = Episode.m_UnfreezeCause;
					Debug.m_HookEvidence = Episode.m_HookEvidence;
					Debug.m_HookFirstTick = Episode.m_HookFirstTick;
					Debug.m_HookLastTick = Episode.m_HookLastTick;
					Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
					Debug.m_CausalConfidence = Episode.m_CausalConfidence;
					Debug.m_EpisodeState = Episode.m_State;
				Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
				Debug.m_Reason = "confirmation baseline started";
				FillDebugPendingEpisodeContext(Debug, Episode);
				AddDebug(pOutput, Debug);
				}
				++i;
				continue;
			}
			if(ServerTick - Episode.m_ConfirmedRecoveryStartTick < Episode.m_RequiredTicks)
			{
				Episode.m_TargetAlive = pTarget->m_Alive;
				Episode.m_TargetUnfrozen = !pTarget->m_Frozen;
				++i;
				continue;
			}
			if(Episode.m_Type != EImpactEventType::ALED_CONFIRMED && Episode.m_OwnershipStatus != EOwnershipStatus::ACTOR)
			{
				if(pOutput)
				{
					SImpactPhase2Debug Debug;
					FillDebugFrame(Debug, *pTarget);
					Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
					Debug.m_DebugLabel = "SAVE_REJECTED";
					Debug.m_EventType = Episode.m_Type;
					Debug.m_EpisodeId = Episode.m_EpisodeId;
					Debug.m_Target = Episode.m_Target;
					Debug.m_PotentialSaver = {};
					Debug.m_StartTick = Episode.m_StartTick;
					Debug.m_RestorationTick = Episode.m_RestoreTick;
					Debug.m_ConfirmationStartTick = Episode.m_ConfirmedRecoveryStartTick;
					Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
					Debug.m_ConfirmedTicks = ServerTick - Episode.m_ConfirmedRecoveryStartTick;
					Debug.m_RequiredTicks = Episode.m_RequiredTicks;
					Debug.m_FrozenBefore = true;
					Debug.m_FrozenNow = false;
					Debug.m_HookEvidence = Episode.m_HookEvidence;
					Debug.m_HammerEvidence = Episode.m_HammerEvidence;
					Debug.m_TargetSpecificEvidence = Episode.m_HookEvidence || Episode.m_HammerEvidence;
					Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
					Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed;
					Debug.m_EpisodeState = Episode.m_State;
				Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
				Debug.m_Reason = "successful recovery has no uniquely provable decisive saver";
				FillDebugPendingEpisodeContext(Debug, Episode);
				AddDebug(pOutput, Debug);
				}
				m_vPendingEpisodes.erase(m_vPendingEpisodes.begin() + i);
				continue;
			}

			// UpDown is retained only as internal episode context for later analysis.
			// It is not a public recovery result and must never change the ordinary
			// Save event type or its raw counter semantics.
			const EImpactEventType Type = Episode.m_Type == EImpactEventType::ALED_CONFIRMED ?
				EImpactEventType::ALED_CONFIRMED : EImpactEventType::SAVE_CONFIRMED;
			if(Type == EImpactEventType::SAVE_CONFIRMED && Episode.m_RelatedFallEpisodeId != 0 &&
				IsValidClientId(Episode.m_Target.m_ClientId))
			{
				SObservedState &Observed = m_aObserved[Episode.m_Target.m_ClientId];
				if(Observed.m_Valid && SameParticipant(Observed.m_Frame, *pTarget) && Observed.m_Frame.m_TeamId == TeamId &&
					Observed.m_ProblemStartTick >= 0)
				{
					for(const SImpactPendingEpisode &FallEpisode : m_vPendingEpisodes)
					{
						if(FallEpisode.m_EpisodeId != Episode.m_RelatedFallEpisodeId ||
							FallEpisode.m_StartTick != Observed.m_ProblemStartTick)
							continue;
						if(HasPreHelperRescueFallCandidate(Episode, FallEpisode, Observed.m_vFrozenInteractions) &&
							ConfirmFallEpisode(FallEpisode.m_EpisodeId, ServerTick, TeamId, pOutput,
								"Fall: canonical confirmed external rescue of pre-helper dangerous frozen problem", true, false, pTarget))
						{
							Episode.m_RelatedFallAlreadyConfirmed = true;
						}
						break;
					}
				}
			}
			SImpactEvent Event = MakeEvent(Type, TeamId, Episode.m_Actor, Episode.m_Target, Episode.m_StartTick, ServerTick, Episode.m_DebugReason.c_str());
			Event.m_EpisodeState = EImpactEpisodeState::CONFIRMED_RECOVERY;
			Event.m_CausalConfidence = Episode.m_CausalConfidence;
		Event.m_OwnershipStatus = Episode.m_OwnershipStatus;
		Event.m_BeforeContext = Episode.m_BeforeContext;
		Event.m_AfterContext = Episode.m_AfterContext;
		Event.m_vHelpers = Episode.m_vHelpers;
		Event.m_RawDelta.m_Saves = 1;
		if(pOutput != nullptr)
		{
			SImpactPhase2Debug ClutchDebug;
			ClutchDebug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
			ClutchDebug.m_DebugLabel = "CLUTCH_SAVE_CONFIRM";
			ClutchDebug.m_EventType = Type;
			ClutchDebug.m_EpisodeId = Episode.m_EpisodeId;
			ClutchDebug.m_ClutchEpisodeId = Episode.m_ClutchEpisodeId;
			ClutchDebug.m_Target = Episode.m_Target;
			ClutchDebug.m_PotentialSaver = Episode.m_Actor;
			ClutchDebug.m_TeamId = Episode.m_TeamId;
			ClutchDebug.m_ServerTick = ServerTick;
			ClutchDebug.m_ClutchArmedTick = Episode.m_ClutchArmedTick;
			ClutchDebug.m_StartTick = Episode.m_StartTick;
			ClutchDebug.m_RestorationTick = Episode.m_RestoreTick;
			ClutchDebug.m_CandidateCreated = true;
			ClutchDebug.m_Reason = "save_confirmed=YES aled=" + std::to_string(Type == EImpactEventType::ALED_CONFIRMED ? 1 : 0) +
				" pending_clutch_candidate=" + std::to_string(Episode.m_ClutchCandidate ? 1 : 0) +
				" episode_armed=" + std::to_string(m_ClutchEpisodeActive ? 1 : 0) +
				" episode_consumed=" + std::to_string(m_ClutchEpisodeConsumed ? 1 : 0) +
				" actor_match=" + std::to_string(Episode.m_Actor == m_ClutchActor ? 1 : 0) +
				" crisis_proof_available=" + std::to_string(Episode.m_ClutchCandidate ? 1 : 0);
			AddDebug(pOutput, ClutchDebug);
		}
		if(Episode.m_ClutchCandidate && Episode.m_Actor.IsValid() && Episode.m_Actor == Event.m_Actor)
			Event.m_RawDelta.m_Clutches = 1;
		if(Type == EImpactEventType::ALED_CONFIRMED)
			{
				Event.m_vTags.push_back("ALED");
				Event.m_RawDelta.m_Aleds = 1;
			}
			else
				Event.m_vTags.push_back("SAVE");
			if(Episode.m_Type != EImpactEventType::ALED_CONFIRMED)
			{
				if(Episode.m_UnfreezeCause == EImpactUnfreezeCause::NATURAL)
					Event.m_vTags.push_back("NATURAL_UNFREEZE");
				else if(Episode.m_UnfreezeCause == EImpactUnfreezeCause::HAMMER)
					Event.m_vTags.push_back("HAMMER_CAUSED_UNFREEZE");
				else
					Event.m_vTags.push_back("UNKNOWN_UNFREEZE_CAUSE");
			}
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug ClutchDebug;
				ClutchDebug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
				ClutchDebug.m_DebugLabel = "CLUTCH_RAW_DELTA";
				ClutchDebug.m_EventType = Type;
				ClutchDebug.m_EpisodeId = Episode.m_EpisodeId;
				ClutchDebug.m_ClutchEpisodeId = Episode.m_ClutchEpisodeId;
				ClutchDebug.m_Target = Episode.m_Target;
				ClutchDebug.m_PotentialSaver = Episode.m_Actor;
				ClutchDebug.m_TeamId = Episode.m_TeamId;
				ClutchDebug.m_ServerTick = ServerTick;
				ClutchDebug.m_ClutchArmedTick = Episode.m_ClutchArmedTick;
				ClutchDebug.m_StartTick = Episode.m_StartTick;
				ClutchDebug.m_RestorationTick = Episode.m_RestoreTick;
				ClutchDebug.m_RawDelta = Event.m_RawDelta;
				ClutchDebug.m_Reason = "save_delta=" + std::to_string(Event.m_RawDelta.m_Saves) +
					" aled_delta=" + std::to_string(Event.m_RawDelta.m_Aleds) +
					" clutch_delta=" + std::to_string(Event.m_RawDelta.m_Clutches) +
					" actor=" + std::to_string(Event.m_Actor.m_ClientId) + ":" + std::to_string(Event.m_Actor.m_SlotGeneration) +
					" target=" + std::to_string(Event.m_Target.m_ClientId) + ":" + std::to_string(Event.m_Target.m_SlotGeneration);
				AddDebug(pOutput, ClutchDebug);
			}
			if(Event.m_RawDelta.m_Clutches > 0)
				CloseConfirmedClutchEpisode(Episode);
			SImpactPhase2Debug Debug;
			FillDebugFrame(Debug, *pTarget);
			Debug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
			Debug.m_DebugLabel = "SAVE_CONFIRMED";
			Debug.m_EventType = Type;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_ProblemEpisodeId = Episode.m_RelatedFallEpisodeId;
			Debug.m_RelatedFallEpisodeId = Episode.m_RelatedFallEpisodeId;
			Debug.m_RelatedRecoveryEpisodeId = Episode.m_EpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_ConfirmationStartTick = Episode.m_ConfirmedRecoveryStartTick;
			Debug.m_ConfirmedTicks = ServerTick - Episode.m_ConfirmedRecoveryStartTick;
			Debug.m_RequiredTicks = Episode.m_RequiredTicks;
			Debug.m_FrozenBefore = true;
			Debug.m_FrozenNow = false;
			Debug.m_FreezeKnown = pTarget->m_FreezeKnown;
			Debug.m_HookEvidence = Episode.m_HookEvidence;
			Debug.m_HammerEvidence = Episode.m_HammerEvidence;
			Debug.m_UnfreezeCause = Episode.m_UnfreezeCause;
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_InitialRecoveryNeed = Episode.m_InitialRecoveryNeed;
			Debug.m_HookFirstTick = Episode.m_HookFirstTick;
			Debug.m_HookLastTick = Episode.m_HookLastTick;
			Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
			Debug.m_TargetSpecificEvidence = Episode.m_HookEvidence || Episode.m_HammerEvidence;
			Debug.m_CausalConfidence = Episode.m_CausalConfidence;
			Debug.m_EpisodeState = Event.m_EpisodeState;
			Debug.m_RawDelta = Event.m_RawDelta;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			FillDebugPendingEpisodeContext(Debug, Episode);
			Debug.m_Stage = EImpactPhase2DebugStage::CONFIRMED;
			Debug.m_DebugLabel = "SAVE_CONFIRMED";
			Debug.m_EventType = Type;
			Debug.m_ConfirmationStartTick = Episode.m_ConfirmedRecoveryStartTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_ConfirmedTicks = ServerTick - Episode.m_ConfirmedRecoveryStartTick;
			Debug.m_RequiredTicks = Episode.m_RequiredTicks;
			Debug.m_FrozenBefore = true;
			Debug.m_FrozenNow = false;
			Debug.m_FreezeKnown = pTarget->m_FreezeKnown;
			Debug.m_UnfreezeCause = Episode.m_UnfreezeCause;
			Debug.m_CausalConfidence = Episode.m_CausalConfidence;
			Debug.m_EpisodeState = Event.m_EpisodeState;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			for(const SImpactPendingEpisode &FallEpisode : m_vPendingEpisodes)
			{
				if(FallEpisode.m_EpisodeId == Episode.m_RelatedFallEpisodeId &&
					FallEpisode.m_Type == EImpactEventType::FALL_CONFIRMED)
				{
					Debug.m_FallAlreadyConfirmed = IsFallConfirmed(FallEpisode);
					Debug.m_ConfirmedFall = Debug.m_FallAlreadyConfirmed;
					break;
				}
			}
			Debug.m_Reason = Event.m_CausalReason;
			AddDebug(pOutput, Debug);

			const uint64_t SaveEpisodeId = Episode.m_EpisodeId;
			const auto SaveIt = std::find_if(m_vPendingEpisodes.begin(), m_vPendingEpisodes.end(), [SaveEpisodeId](const SImpactPendingEpisode &Pending) {
				return Pending.m_EpisodeId == SaveEpisodeId;
			});
			if(SaveIt != m_vPendingEpisodes.end())
				m_vPendingEpisodes.erase(SaveIt);
			else
				++i;
			// Keep the recovery event last in this output batch. Besides making the
			// event order deterministic, this preserves the existing consumer
			// convention that the just-confirmed Save is the final recovery event for
			// the tick.
			pOutput->m_vEvents.push_back(std::move(Event));
		}
	}

	void CImpactPhase2::ProcessExplicitSignal(const SImpactGameplayMessage &Message, int TickSpeed, int TeamId,
		const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		if(!Message.m_Confirmed || !IsValidTeam(TeamId))
			return;
		const int64_t Tick = Message.m_ServerTick;
		SParticipantId Actor = Message.m_Actor;
		SParticipantId Target = Message.m_Target;
		if(Message.m_Type == EImpactGameplayMessageType::CONFIRMED_ALED)
		{
			// ALED is a canonical recovery candidate, not an immediate raw counter.
			// There is no live producer for this signal yet; if an authoritative
			// producer is added later, it must still pass the same 0.7 second
			// success confirmation as every other Save-based mechanic.
			if(!Message.m_CausalConfirmed || !Actor.IsValid() || !Target.IsValid() || Actor == Target ||
				m_vPendingEpisodes.size() >= MAX_PENDING_EPISODES)
				return;
			SImpactPendingEpisode Episode;
			Episode.m_EpisodeId = m_NextEpisodeId++;
			Episode.m_Type = EImpactEventType::ALED_CONFIRMED;
			Episode.m_Actor = Actor;
			Episode.m_Target = Target;
			Episode.m_TeamId = TeamId;
			Episode.m_StartTick = Tick;
			Episode.m_RestoreTick = Tick;
				Episode.m_RequiredTicks = ConfirmationTicks(TickSpeed);
			Episode.m_State = EImpactEpisodeState::NONE;
			Episode.m_CausalConfidence = Message.m_CausalConfidence == ECausalConfidence::INSUFFICIENT ?
				ECausalConfidence::CONFIRMED : Message.m_CausalConfidence;
			Episode.m_OwnershipStatus = EOwnershipStatus::ACTOR;
			Episode.m_TargetAlive = true;
			Episode.m_TargetUnfrozen = true;
			Episode.m_RecoveryNeed = EImpactRecoveryNeed::EXTERNAL_REQUIRED;
			Episode.m_ConfirmedRecoveryStartTick = Tick;
			Episode.m_TargetWasEligible = true;
			Episode.m_BeforeContext = "canonical ALED recovery candidate";
			Episode.m_AfterContext = "canonical ALED restored target";
			Episode.m_DebugReason = "canonical ALED candidate awaiting 0.7 second confirmation";
			MaybeMarkClutchCandidate(Episode, vFrames, pOutput);
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug ClutchDebug;
				ClutchDebug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
				ClutchDebug.m_DebugLabel = "CLUTCH_PENDING_ATTACH";
				ClutchDebug.m_EventType = Episode.m_Type;
				ClutchDebug.m_EpisodeId = Episode.m_EpisodeId;
				ClutchDebug.m_ClutchEpisodeId = Episode.m_ClutchEpisodeId != 0 ? Episode.m_ClutchEpisodeId : m_ClutchEpisodeId;
				ClutchDebug.m_Target = Episode.m_Target;
				ClutchDebug.m_PotentialSaver = Episode.m_Actor;
				ClutchDebug.m_TeamId = Episode.m_TeamId;
				ClutchDebug.m_ServerTick = Tick;
				ClutchDebug.m_ClutchArmedTick = Episode.m_ClutchArmedTick;
				ClutchDebug.m_StartTick = Episode.m_StartTick;
				ClutchDebug.m_RestorationTick = Episode.m_RestoreTick;
				ClutchDebug.m_CandidateCreated = true;
				ClutchDebug.m_Reason = "canonical_save_candidate=YES aled=YES clutch_episode_armed=" + std::to_string(m_ClutchEpisodeActive ? 1 : 0) +
					" clutch_actor=" + std::to_string(m_ClutchActor.m_ClientId) + ":" + std::to_string(m_ClutchActor.m_SlotGeneration) +
					" actor_match=" + std::to_string(Episode.m_Actor == m_ClutchActor ? 1 : 0) +
					" clutch_candidate=" + std::to_string(Episode.m_ClutchCandidate ? 1 : 0) +
					" episode_consumed=" + std::to_string(m_ClutchEpisodeConsumed ? 1 : 0) +
					" ownership=" + OwnershipStatusName(Episode.m_OwnershipStatus) + " target_exclusion=APPLIED";
				AddDebug(pOutput, ClutchDebug);
			}
			SImpactPhase2Debug Debug;
			Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
			Debug.m_EventType = Episode.m_Type;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_TeamId = Episode.m_TeamId;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_EpisodeState = Episode.m_State;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			Debug.m_Reason = Episode.m_DebugReason;
			AddDebug(pOutput, Debug);
			m_vPendingEpisodes.push_back(std::move(Episode));
			return;
		}
		EImpactEventType Type = EImpactEventType::TEAM_HARM_CONFIRMED;
		SRawStats Raw;
		const char *pTag = nullptr;
		switch(Message.m_Type)
		{
		case EImpactGameplayMessageType::CONFIRMED_HARM:
			if(!Message.m_CausalConfirmed || Message.m_CausalConfidence != ECausalConfidence::CONFIRMED)
				return;
			if(Message.m_IntentionalReset)
			{
				if(Target.IsValid())
					CancelPending(Target, pOutput, "intentional reset before recovery confirmation", false);
				Type = EImpactEventType::JUMP_RESET_CONFIRMED;
				pTag = "JUMP_RESET";
			}
			else if(Actor.IsValid() && Target.IsValid() && Actor != Target)
			{
				// An explicit actor/target harm signal proves teammate-caused harm.
				// It must never be converted into a victim Fall. Keep the causal fact
				// as a neutral team-harm event and close any recovery candidate for the
				// target; no personal penalty is inferred here.
				// Teammate-caused origin closes only the independent Fall context. A
				// Save candidate already proven by another target-specific action must
				// survive this signal and continue through its own restoration path.
				// The explicit harm packet is context only. It does not contain the
				// pre-freeze trajectory required by the independent Fall decision and
				// therefore must not cancel an unconfirmed raw Fall. It also must not
				// touch an independent Save recovery record.
				CancelPending(Target, pOutput, "confirmed teammate-caused harm context", false, false);
				Type = EImpactEventType::TEAM_HARM_CONFIRMED;
				pTag = "TEAM_HARM";
			}
			else
			{
				// An own-harm signal is origin metadata only. It cannot manufacture a
				// Fall from Message.m_PhysicalOutcome: death/tele/expiry evidence is not
				// the canonical self-recovery-impossible decision. TrackHarmfulEpisodes
				// remains the sole producer of ordinary Fall events after it has observed
				// the complete problem/cause/self-recovery chain.
				return;
			}
			break;
		case EImpactGameplayMessageType::CONFIRMED_ALED:
			return;
		case EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH:
			if(!Message.m_CausalConfirmed || !Message.m_Feasible || !Message.m_OutcomeWorsened || Message.m_ThirdPartyInterference)
				return;
			Type = EImpactEventType::MISSED_CATCH_CONFIRMED;
			Raw.m_MissedCatches = 1;
			pTag = "MISSED_CATCH";
			break;
		case EImpactGameplayMessageType::CONFIRMED_PREVENTED_PROGRESS_LOSS:
			if(!Message.m_CausalConfirmed)
				return;
			Type = EImpactEventType::PREVENTED_PROGRESS_LOSS_CONFIRMED;
			Raw.m_PreventedProgressLoss = 1;
			pTag = "FALL_INTERCEPTION";
			break;
		case EImpactGameplayMessageType::CONFIRMED_RECOVERY_DIFFICULTY_INCREASE:
			if(!Message.m_CausalConfirmed)
				return;
			Type = EImpactEventType::RECOVERY_DIFFICULTY_INCREASE_CONFIRMED;
			Raw.m_RecoveryDifficultyIncreases = 1;
			pTag = "RECOVERY_DIFFICULTY";
			break;
		case EImpactGameplayMessageType::CONFIRMED_JUMP_RESET:
			if(!Message.m_CausalConfirmed)
				return;
			Type = EImpactEventType::JUMP_RESET_CONFIRMED;
			pTag = "JUMP_RESET";
			break;
		default:
			return;
		}
		SImpactEvent Event = MakeEvent(Type, TeamId, Actor, Target, Tick, Tick, Message.m_Reason.c_str());
		Event.m_CausalConfidence = Message.m_CausalConfidence == ECausalConfidence::INSUFFICIENT ? ECausalConfidence::CONFIRMED : Message.m_CausalConfidence;
		Event.m_OwnershipStatus = Actor.IsValid() ? EOwnershipStatus::ACTOR : EOwnershipStatus::NONE;
		Event.m_RawDelta = Raw;
		if(pTag)
			Event.m_vTags.push_back(pTag);
		pOutput->m_vEvents.push_back(std::move(Event));
	}

	void CImpactPhase2::ProcessMessages(int64_t ServerTick, int TickSpeed, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames, const std::vector<SImpactGameplayMessage> &vMessages, bool SkipResetMessages)
	{
		std::vector<SImpactGameplayMessage> vSorted = vMessages;
		std::sort(vSorted.begin(), vSorted.end(), [](const SImpactGameplayMessage &Left, const SImpactGameplayMessage &Right) {
			if(Left.m_ServerTick != Right.m_ServerTick)
				return Left.m_ServerTick < Right.m_ServerTick;
			return Left.m_MessageId < Right.m_MessageId;
		});
		for(const SImpactGameplayMessage &Message : vSorted)
		{
			if(!Message.m_Confirmed || (Message.m_MessageId != 0 && Message.m_MessageId <= m_LastMessageId))
				continue;
			if(Message.m_MessageId != 0)
				m_LastMessageId = std::max(m_LastMessageId, Message.m_MessageId);
			if(SkipResetMessages && (Message.m_Type == EImpactGameplayMessageType::TEAM_RESET ||
				Message.m_Type == EImpactGameplayMessageType::TEAM_WIPE))
				continue;
			const int64_t Tick = Message.m_ServerTick >= 0 ? Message.m_ServerTick : ServerTick;
			switch(Message.m_Type)
			{
			case EImpactGameplayMessageType::KILL:
			{
				const int VictimId = Message.m_Victim;
				if(!IsValidClientId(VictimId))
				{
					AddDeathDebug(pOutput, Message, {}, false, false, "invalid victim client id");
					break;
				}
				const SObservedState &VictimObserved = m_aObserved[VictimId];
				if(!VictimObserved.m_Valid)
				{
					AddDeathDebug(pOutput, Message, {}, false, false, "victim participant state not observed");
					break;
				}
				const SParticipantId Victim = VictimObserved.m_Frame.m_Participant;
				const SImpactGameplayFrame *pVictimFrame = FindClient(vFrames, VictimId);
				const bool HistoricalEligibility = VictimObserved.m_HasLastKnownCharacter &&
					SameParticipant(VictimObserved.m_LastKnownCharacterFrame, VictimObserved.m_Frame) &&
					VictimObserved.m_LastKnownCharacterFrame.m_Eligible;
				const bool EligibleVictim = Victim.IsValid() && (VictimObserved.m_Frame.m_Eligible || HistoricalEligibility);
				if(!EligibleVictim)
				{
					AddDeathDebug(pOutput, Message, Victim, VictimObserved.m_HasLastKnownCharacter, false, "victim was not an eligible participant");
					break;
				}
				if(Message.m_Weapon == WEAPON_SELF || Message.m_Weapon == WEAPON_GAME)
				{
					AddDeathDebug(pOutput, Message, Victim, VictimObserved.m_HasLastKnownCharacter, false,
						Message.m_Weapon == WEAPON_SELF ? "manual/self kill ignored" : "game/reset kill ignored");
					CancelPending(Victim, pOutput, "server-confirmed self/reset harm before recovery confirmation");
					break;
				}
				// A live death is commonly delivered in the same snapshot in which
				// the server removes CNetObj_Character. PlayerInfo still keeps the
				// participant in vFrames, but that frame intentionally has unknown
				// character/solo state. Use the last confirmed character context for
				// validating the server outcome, never as a current state transition.
				const SImpactGameplayFrame *pVictimContext = pVictimFrame;
				if(pVictimContext == nullptr || !pVictimContext->m_CurrentCharacterKnown ||
					!pVictimContext->m_AliveKnown || !pVictimContext->m_SoloKnown)
				{
					pVictimContext = VictimObserved.m_HasLastKnownCharacter ?
						&VictimObserved.m_LastKnownCharacterFrame : nullptr;
				}
				if(pVictimContext == nullptr || !pVictimContext->m_SoloKnown || pVictimContext->m_Solo || pVictimContext->m_TeamId != TeamId)
				{
					AddDeathDebug(pOutput, Message, Victim, pVictimContext != nullptr, false, "no confirmed victim character context");
					if(g_Config.m_Debug)
						log_debug("amf-impact", "kill outcome rejected victim=%d reason='no confirmed victim character context'", VictimId);
					break;
				}
				bool AlreadyConfirmedFall = false;
				for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				{
					if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED && Episode.m_Target == Victim)
					{
						AlreadyConfirmedFall = IsFallConfirmed(Episode);
						break;
					}
				}
				// Killer/weapon attribution is a lifecycle signal only. It is not a
				// physical pre-freeze trajectory observation and therefore cannot suppress
				// or manufacture the independent raw Fall decision.
				AddDeathDebug(pOutput, Message, Victim, true, AlreadyConfirmedFall,
					AlreadyConfirmedFall ? "death closed an already-confirmed Fall context" :
					"death closed a pending episode; killer attribution does not decide Fall");
				// Death is a lifecycle termination signal, not proof of an individual
				// harmful Fall. Cancel unconfirmed episodes, while CancelPending keeps
				// an already-confirmed Fall immutable for the ledger/history.
				CancelPending(Victim, pOutput, "server-confirmed death closed pending episode without creating Fall", true);
				if(!AlreadyConfirmedFall && g_Config.m_Debug)
					log_debug("amf-impact", "fall outcome rejected victim=%d reason='%s'", VictimId,
						"death alone does not confirm Fall");
				break;
			}
			case EImpactGameplayMessageType::TEAM_WIPE:
			case EImpactGameplayMessageType::TEAM_RESET:
			{
			if(Message.m_TeamId != 0 && Message.m_TeamId != TeamId)
				break;
			// A team-wipe packet confirms a team outcome, not the individual cause
			// for every Tee. It must never synthesize victim Falls from RecoveryNeed
				// or from an otherwise unclassified pending recovery.
				for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
				{
					SImpactPhase2Debug Debug;
					Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
					Debug.m_EventType = Episode.m_Type;
					Debug.m_EpisodeId = Episode.m_EpisodeId;
					Debug.m_Target = Episode.m_Target;
					Debug.m_PotentialSaver = Episode.m_Actor;
					Debug.m_StartTick = Episode.m_StartTick;
					Debug.m_RestorationTick = Episode.m_RestoreTick;
					Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
					Debug.m_FrozenBefore = true;
					Debug.m_HookEvidence = Episode.m_HookEvidence;
					Debug.m_HammerEvidence = Episode.m_HammerEvidence;
					Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
					Debug.m_HookFirstTick = Episode.m_HookFirstTick;
					Debug.m_HookLastTick = Episode.m_HookLastTick;
					Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
					Debug.m_EpisodeState = Episode.m_State;
					Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
					Debug.m_Reset = true;
					FillDebugPendingEpisodeContext(Debug, Episode);
					Debug.m_Reset = true;
					Debug.m_Reason = "team wipe/reset before recovery confirmation";
					AddDebug(pOutput, Debug);
				}
				m_vPendingEpisodes.clear();
				SImpactEvent Event = MakeEvent(Message.m_Type == EImpactGameplayMessageType::TEAM_RESET || Message.m_First >= 0 ? EImpactEventType::TEAM_RESET_CONFIRMED : EImpactEventType::WIPE_CONFIRMED, TeamId, {}, {}, Tick, Tick, Message.m_First >= 0 ? "server-confirmed team reset" : "server-confirmed team wipe");
				if(Event.m_Type == EImpactEventType::WIPE_CONFIRMED)
				{
					Event.m_RawDelta.m_Wipes = 1;
					Event.m_vTags.push_back("WIPE");
				}
				else
					Event.m_vTags.push_back("TEAM_RESET");
				pOutput->m_vEvents.push_back(std::move(Event));
				break;
			}
			case EImpactGameplayMessageType::FINISH:
				if(IsValidClientId(Message.m_ClientId))
				{
					m_aFinishSignals[Message.m_ClientId] = true;
					m_aFinishTimeKnown[Message.m_ClientId] = Message.m_FinishTimeMillis >= 0;
					m_aFinishTimeMillis[Message.m_ClientId] = Message.m_FinishTimeMillis;
				}
				break;
			case EImpactGameplayMessageType::DISCONNECT:
			{
				SParticipantId Target = Message.m_Target;
				if(!Target.IsValid() && IsValidClientId(Message.m_ClientId))
					Target = m_aObserved[Message.m_ClientId].m_Frame.m_Participant;
				if(Target.IsValid())
					CancelPending(Target, pOutput, "participant disconnected before recovery confirmation");
				if(IsValidClientId(Message.m_ClientId))
				{
					// A disconnect is a lifecycle boundary for the slot. Keep an already
					// emitted Fall in the ledger, but do not let the old observed frame or
					// its causal state attach to a later slot occupant.
					if(pOutput != nullptr && m_ClutchEpisodeActive)
					{
						SImpactPhase2Debug Debug;
						Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
						Debug.m_DebugLabel = "CLUTCH_EPISODE_CLEAR";
						Debug.m_PotentialSaver = m_ClutchActor;
						Debug.m_TeamId = TeamId;
						Debug.m_ServerTick = Message.m_ServerTick;
						Debug.m_ClutchArmedTick = m_ClutchArmedTick;
						Debug.m_Reason = "reason=generation; participant_disconnected client_id=" + std::to_string(Message.m_ClientId);
						AddDebug(pOutput, Debug);
					}
					m_aObserved[Message.m_ClientId] = {};
					m_aFinishSignals[Message.m_ClientId] = false;
					m_aFinishTimeKnown[Message.m_ClientId] = false;
					m_aFinishTimeMillis[Message.m_ClientId] = -1;
					m_ClutchEpisodeActive = false;
					m_ClutchEpisodeConsumed = false;
					m_ClutchAwaitingNewCrisis = false;
					m_ClutchActor = {};
					m_ClutchArmedTick = -1;
					m_ClutchEpisodeId = 0;
					m_ClutchProvisionalObserved = false;
					m_ClutchProvisionalActor = {};
					m_ClutchProvisionalTick = -1;
					m_LastClutchEvaluationKey.clear();
				}
				break;
			}
			case EImpactGameplayMessageType::CONFIRMED_HARM:
			case EImpactGameplayMessageType::CONFIRMED_ALED:
			case EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH:
			case EImpactGameplayMessageType::CONFIRMED_PREVENTED_PROGRESS_LOSS:
			case EImpactGameplayMessageType::CONFIRMED_RECOVERY_DIFFICULTY_INCREASE:
			case EImpactGameplayMessageType::CONFIRMED_JUMP_RESET:
			{
				SImpactGameplayMessage Signal = Message;
				Signal.m_ServerTick = Tick;
				ProcessExplicitSignal(Signal, TickSpeed, TeamId, vFrames, pOutput);
				break;
			}
			}
		}
	}

	void CImpactPhase2::MaybeMarkClutchCandidate(SImpactPendingEpisode &Episode,
		const std::vector<SImpactGameplayFrame> &vContextFrames, SImpactPhase2Output *pOutput)
	{
		// A confirmed Clutch leaves a barrier until the next physical problem
		// episode starts. Release it here as well as in UpdateClutchEpisodeState so a
		// sparse same-tick restoration cannot attach a second pending Save before the
		// normal lifecycle pass runs.
		if(m_ClutchAwaitingNewCrisis)
		{
			const bool NewProblemStarted = std::any_of(vContextFrames.begin(), vContextFrames.end(), [this](const SImpactGameplayFrame &Frame) {
				if(!IsValidClientId(Frame.m_Participant.m_ClientId) || Frame.m_TeamId != m_TeamId)
					return false;
				const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
				return Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame) && Observed.m_ProblemEpisodeStartedThisFrame;
			});
			if(NewProblemStarted)
			{
				m_ClutchAwaitingNewCrisis = false;
				m_ClutchEpisodeConsumed = false;
				m_LastClutchEvaluationKey.clear();
			}
		}
		std::vector<SImpactFunctionalRosterEntry> vFunctionalRoster;
		auto AddFinalProofTrace = [&](bool Accepted, const char *pReason, const std::string &ParticipantTrace) {
			if(pOutput == nullptr)
				return;
			SImpactPhase2Debug Debug;
			Debug.m_Stage = Accepted ? EImpactPhase2DebugStage::CANDIDATE : EImpactPhase2DebugStage::CANCELLED;
			Debug.m_DebugLabel = "CLUTCH_FINAL_PROOF";
			Debug.m_EventType = Episode.m_Type;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_ClutchEpisodeId = Episode.m_ClutchEpisodeId != 0 ? Episode.m_ClutchEpisodeId : m_ClutchEpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_TeamId = Episode.m_TeamId;
			Debug.m_ServerTick = Episode.m_RestoreTick;
			Debug.m_ClutchArmedTick = m_ClutchArmedTick;
			Debug.m_vFunctionalRoster = vFunctionalRoster;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_CandidateCreated = Accepted;
			Debug.m_Reason = "final=" + std::string(Accepted ? "ACCEPTED" : "REJECTED") +
				" actor=" + std::to_string(Episode.m_Actor.m_ClientId) + ":" + std::to_string(Episode.m_Actor.m_SlotGeneration) +
				" target=" + std::to_string(Episode.m_Target.m_ClientId) + ":" + std::to_string(Episode.m_Target.m_SlotGeneration) +
				" target_excluded=YES episode_armed=" + std::to_string(m_ClutchEpisodeActive ? 1 : 0) +
				" episode_consumed=" + std::to_string(m_ClutchEpisodeConsumed ? 1 : 0) +
				" actor_match=" + std::to_string(Episode.m_Actor == m_ClutchActor ? 1 : 0) +
				" reason=" + (pReason == nullptr ? "UNKNOWN" : pReason) +
				" states=" + ParticipantTrace;
			AddDebug(pOutput, Debug);
		};

		const bool IsCanonicalRecovery = Episode.m_Type == EImpactEventType::SAVE_CONFIRMED || Episode.m_Type == EImpactEventType::ALED_CONFIRMED;
		if(!IsCanonicalRecovery)
		{
			AddFinalProofTrace(false, "NOT_CANONICAL_SAVE", "not_evaluated");
			return;
		}
		if(Episode.m_ClutchCandidate)
		{
			AddFinalProofTrace(false, "CANDIDATE_ALREADY_SET", "not_evaluated");
			return;
		}
		if(m_ClutchEpisodeConsumed)
		{
			AddFinalProofTrace(false, "EPISODE_ALREADY_CONSUMED", "not_evaluated");
			return;
		}
		if(!m_ClutchEpisodeActive && !m_ClutchProvisionalObserved)
		{
			AddFinalProofTrace(false, "NO_PROVEN_CRISIS", "not_evaluated");
			return;
		}
		if(!m_ClutchEpisodeActive && m_ClutchProvisionalActor != Episode.m_Actor)
		{
			AddFinalProofTrace(false, "PROVISIONAL_ACTOR_MISMATCH", "not_evaluated");
			return;
		}
		if(Episode.m_OwnershipStatus != EOwnershipStatus::ACTOR)
		{
			AddFinalProofTrace(false, "OWNERSHIP_NOT_ACTOR", "not_evaluated");
			return;
		}
		if(!Episode.m_Actor.IsValid())
		{
			AddFinalProofTrace(false, "INVALID_ACTOR", "not_evaluated");
			return;
		}
		if(m_ClutchEpisodeActive && Episode.m_Actor != m_ClutchActor)
		{
			AddFinalProofTrace(false, "ACTOR_MISMATCH", "not_evaluated");
			return;
		}
		SParticipantId SoleFunctional;
		std::string ParticipantTrace;
		int UnknownCount = 0;
		int FunctionalCount = 0;
		const bool HasSoleFunctional = FindSoleFunctionalParticipant(vContextFrames, Episode.m_TeamId, &Episode.m_Target, true,
			&SoleFunctional, &ParticipantTrace, &UnknownCount, &FunctionalCount, &vFunctionalRoster);
		if(!HasSoleFunctional)
		{
			const char *pReason = UnknownCount > 0 ? "OTHER_UNKNOWN" :
				FunctionalCount > 1 ? "OTHER_FUNCTIONAL" : "NO_FUNCTIONAL_SURVIVOR";
			AddFinalProofTrace(false, pReason, ParticipantTrace);
			return;
		}
		if(SoleFunctional != Episode.m_Actor)
		{
			AddFinalProofTrace(false, "ACTOR_MISMATCH", ParticipantTrace);
			return;
		}
		// A previously UNKNOWN crisis may become provable on the same authoritative
		// restoration frame. The target is excluded here, so the newly restored Tee
		// cannot invalidate the saver before the canonical Save candidate is attached.
		if(!m_ClutchEpisodeActive)
		{
			m_ClutchEpisodeActive = true;
			m_ClutchEpisodeConsumed = false;
			m_ClutchActor = Episode.m_Actor;
			m_ClutchArmedTick = Episode.m_RestoreTick >= 0 ? Episode.m_RestoreTick : Episode.m_StartTick;
			m_ClutchEpisodeId = m_NextClutchEpisodeId++;
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
				Debug.m_DebugLabel = "CLUTCH_EPISODE_ARM";
				Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
				Debug.m_EventType = Episode.m_Type;
				Debug.m_EpisodeId = Episode.m_EpisodeId;
				Debug.m_Target = Episode.m_Target;
				Debug.m_PotentialSaver = Episode.m_Actor;
				Debug.m_TeamId = Episode.m_TeamId;
				Debug.m_ServerTick = Episode.m_RestoreTick;
				Debug.m_ClutchArmedTick = m_ClutchArmedTick;
				Debug.m_vFunctionalRoster = vFunctionalRoster;
				Debug.m_Started = true;
				Debug.m_Eligible = true;
				Debug.m_Reason = "armed=YES crisis_proven=1 source=canonical_restore target_excluded=YES states=" + ParticipantTrace;
				AddDebug(pOutput, Debug);
			}
		}
		AddFinalProofTrace(true, "ACCEPTED", ParticipantTrace);

		Episode.m_ClutchCandidate = true;
		Episode.m_ClutchArmedTick = m_ClutchArmedTick;
		Episode.m_ClutchEpisodeId = m_ClutchEpisodeId;
		m_ClutchEpisodeConsumed = true;
		if(pOutput == nullptr)
			return;

		SImpactPhase2Debug Debug;
		Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
		Debug.m_DebugLabel = "CLUTCH_CANDIDATE";
		Debug.m_EventType = Episode.m_Type;
		Debug.m_EpisodeId = Episode.m_EpisodeId;
		Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
		Debug.m_Target = Episode.m_Target;
		Debug.m_PotentialSaver = Episode.m_Actor;
		Debug.m_TeamId = Episode.m_TeamId;
		Debug.m_ServerTick = Episode.m_RestoreTick;
		Debug.m_ClutchArmedTick = Episode.m_ClutchArmedTick;
		Debug.m_StartTick = Episode.m_StartTick;
		Debug.m_RestorationTick = Episode.m_RestoreTick;
		Debug.m_CandidateCreated = true;
		Debug.m_vFunctionalRoster = vFunctionalRoster;
		Debug.m_Reason = "confirmed Save actor was latched as the sole functionally independent Tee";
		AddDebug(pOutput, Debug);
	}

	void CImpactPhase2::UpdateClutchEpisodeState(int64_t ServerTick, int TeamId, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput)
	{
		SParticipantId SoleFunctional;
		std::string ParticipantTrace;
		int UnknownCount = 0;
		int FunctionalCount = 0;
		std::vector<SImpactFunctionalRosterEntry> vFunctionalRoster;
		// Only engaged current-run members enter the blocker roster. Every admitted
		// physical state still uses the existing Save-side canonical classification.
		const bool HasSoleFunctionalCandidate = FindSoleFunctionalParticipant(vFrames, TeamId, nullptr, false, &SoleFunctional,
			&ParticipantTrace, &UnknownCount, &FunctionalCount, &vFunctionalRoster);
		const bool CrisisProven = HasSoleFunctionalCandidate && UnknownCount == 0;
		const char *pProofStatus = CrisisProven ? "PROVEN" : UnknownCount > 0 ? "UNKNOWN" : "NOT_PROVEN";
		const char *pFailureReason = UnknownCount > 0 ? "OTHER_UNKNOWN" :
			FunctionalCount > 1 ? "OTHER_FUNCTIONAL" : HasSoleFunctionalCandidate ? "NONE" : "NO_FUNCTIONAL_SURVIVOR";
		const std::string EvaluationKey = std::string(pProofStatus) + "|" + pFailureReason + "|" + ParticipantTrace;
		if(pOutput != nullptr && (m_ForensicTraceEnabled || g_Config.m_Debug) && EvaluationKey != m_LastClutchEvaluationKey)
		{
			SImpactPhase2Debug Debug;
			Debug.m_Stage = HasSoleFunctionalCandidate ? EImpactPhase2DebugStage::CANDIDATE : EImpactPhase2DebugStage::CANCELLED;
			Debug.m_DebugLabel = "CLUTCH_CRISIS_EVAL";
			Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
			Debug.m_PotentialSaver = HasSoleFunctionalCandidate ? SoleFunctional : m_ClutchActor;
			Debug.m_TeamId = TeamId;
			Debug.m_ServerTick = ServerTick;
			Debug.m_UnfrozenCount = static_cast<int>(vFrames.size());
			Debug.m_ClutchArmedTick = m_ClutchArmedTick;
			Debug.m_vFunctionalRoster = vFunctionalRoster;
			Debug.m_Started = CrisisProven;
			Debug.m_Eligible = HasSoleFunctionalCandidate;
			Debug.m_Reason = "sole_candidate=" + std::to_string(HasSoleFunctionalCandidate ? 1 : 0) +
				" crisis_proven=" + std::to_string(CrisisProven ? 1 : 0) +
				" proof_status=" + std::string(pProofStatus) +
				" failure_reason=" + pFailureReason +
				" participant_count=" + std::to_string(vFrames.size()) +
				" functional_count=" + std::to_string(FunctionalCount) +
				" unknown_blockers=" + std::to_string(UnknownCount) +
				" states=" + ParticipantTrace;
			AddDebug(pOutput, Debug);
		}
		m_LastClutchEvaluationKey = EvaluationKey;
		auto ClearProvisionalObservation = [&]() {
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
		};

		auto ClearActiveEpisode = [&](const char *pReason) {
			if(!m_ClutchEpisodeActive)
				return;
			const bool PreserveConsumed = m_ClutchEpisodeConsumed;
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "CLUTCH_EPISODE_ENDED";
				Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
				Debug.m_PotentialSaver = m_ClutchActor;
				Debug.m_TeamId = TeamId;
				Debug.m_ServerTick = ServerTick;
				Debug.m_UnfrozenCount = 0;
				Debug.m_ClutchArmedTick = m_ClutchArmedTick;
				Debug.m_Reason = std::string("reason=") + (pReason == nullptr ? "unspecified" : pReason) +
					" proof_status=" + pProofStatus + " states=" + ParticipantTrace;
				AddDebug(pOutput, Debug);
				Debug.m_DebugLabel = "CLUTCH_EPISODE_CLEAR";
				AddDebug(pOutput, Debug);
			}
			m_ClutchEpisodeActive = false;
			m_ClutchEpisodeConsumed = PreserveConsumed;
			m_ClutchActor = {};
			m_ClutchArmedTick = -1;
			m_ClutchEpisodeId = 0;
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
			m_LastClutchEvaluationKey.clear();
		};
		if(m_ClutchAwaitingNewCrisis)
		{
			const bool NewProblemStarted = std::any_of(vFrames.begin(), vFrames.end(), [this, TeamId](const SImpactGameplayFrame &Frame) {
				if(!IsValidClientId(Frame.m_Participant.m_ClientId) || Frame.m_TeamId != TeamId)
					return false;
				const SObservedState &Observed = m_aObserved[Frame.m_Participant.m_ClientId];
				return Observed.m_Valid && SameParticipant(Observed.m_Frame, Frame) && Observed.m_ProblemEpisodeStartedThisFrame;
			});
			if(!NewProblemStarted)
				return;
			m_ClutchAwaitingNewCrisis = false;
			m_ClutchEpisodeConsumed = false;
			m_LastClutchEvaluationKey.clear();
		}
		if(!m_ClutchEpisodeActive && m_ClutchEpisodeConsumed)
		{
			const bool NewFrozenCrisis = std::any_of(vFrames.begin(), vFrames.end(), [TeamId](const SImpactGameplayFrame &Frame) {
				return Frame.m_Confirmed && Frame.m_Present && Frame.m_Eligible && Frame.m_TeamId == TeamId &&
					Frame.m_Participant.IsValid() && Frame.m_Frozen;
			});
			if(NewFrozenCrisis)
				m_ClutchEpisodeConsumed = false;
		}
		// An UNKNOWN observation is never a durable candidate. If the blocker is
		// resolved (for example by a positive SELF_SAFE result), leaves the eligible
		// roster, or the roster returns to bootstrap, do not let that old observation
		// attach to a later physical rescue episode.
		if(!m_ClutchEpisodeActive && m_ClutchProvisionalObserved &&
			(!HasSoleFunctionalCandidate || UnknownCount == 0))
			ClearProvisionalObservation();
		if(!m_ClutchEpisodeActive && !m_ClutchEpisodeConsumed && HasSoleFunctionalCandidate && UnknownCount > 0)
		{
			m_ClutchProvisionalObserved = true;
			m_ClutchProvisionalActor = SoleFunctional;
			m_ClutchProvisionalTick = ServerTick;
		}

		if(CrisisProven)
		{
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
			if(m_ClutchEpisodeActive && m_ClutchActor == SoleFunctional)
				return;

			if(m_ClutchEpisodeActive)
				ClearActiveEpisode("participant_became_functional");

			m_ClutchEpisodeActive = true;
			m_ClutchEpisodeConsumed = false;
			m_ClutchActor = SoleFunctional;
			m_ClutchArmedTick = ServerTick;
			m_ClutchEpisodeId = m_NextClutchEpisodeId++;
			if(pOutput != nullptr)
			{
				SImpactPhase2Debug Debug;
				Debug.m_Stage = EImpactPhase2DebugStage::CANDIDATE;
				Debug.m_DebugLabel = "CLUTCH_EPISODE_ARM";
				Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
				Debug.m_PotentialSaver = m_ClutchActor;
				Debug.m_TeamId = TeamId;
				Debug.m_ServerTick = ServerTick;
				Debug.m_UnfrozenCount = 1;
				Debug.m_ClutchArmedTick = m_ClutchArmedTick;
				Debug.m_Started = true;
				Debug.m_Eligible = true;
				Debug.m_Reason = "armed=YES sole_actor=" + std::to_string(m_ClutchActor.m_ClientId) + ":" +
					std::to_string(m_ClutchActor.m_SlotGeneration) + " proof_status=" + pProofStatus + " participant_count=" +
					std::to_string(vFrames.size()) + " unknown_blockers=0 crisis_proven=1 states=" + ParticipantTrace;
				AddDebug(pOutput, Debug);
			}
			return;
		}

		// UNKNOWN physical functionality is a blocker, not a reason to keep an
		// awardable latch alive. A later authoritative frame may prove the
		// crisis and arm a fresh episode; a pending Save already carrying a valid
		// Clutch id remains independent until its canonical confirmation.
		ClearActiveEpisode(pFailureReason);
	}

	void CImpactPhase2::MaybeFinish(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames)
	{
		if(m_FinishRequested)
			return;
		// SV_RACEFINISH is sent before DDNet may move finished participants to
		// Team 0. Use current non-zero-team frames first, and retain the last
		// authoritative frame for a client that is absent only because that
		// roster transition has already happened. Team 0 itself is never finish
		// evidence: every selected participant still needs its own finish signal.
		auto IsFinishEligible = [TeamId](const SImpactGameplayFrame &Frame) {
			return Frame.m_Confirmed && Frame.m_Eligible && Frame.m_TeamId == TeamId &&
				Frame.m_SoloKnown && !Frame.m_Solo && Frame.m_Participant.IsValid();
		};
		std::array<const SImpactGameplayFrame *, MAX_CLIENTS> apFinishFrames{};
		std::array<bool, MAX_CLIENTS> aCurrentFrames{};
		for(const SImpactGameplayFrame &Frame : vFrames)
		{
			const int ClientId = Frame.m_Participant.m_ClientId;
			if(!IsValidClientId(ClientId))
				continue;
			aCurrentFrames[ClientId] = true;
			if(IsFinishEligible(Frame) && apFinishFrames[ClientId] == nullptr)
				apFinishFrames[ClientId] = &Frame;
		}
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
		{
			if(aCurrentFrames[ClientId] || apFinishFrames[ClientId] != nullptr || !m_aObserved[ClientId].m_Valid)
				continue;
			const SImpactGameplayFrame &Observed = m_aObserved[ClientId].m_Frame;
			if(IsFinishEligible(Observed))
				apFinishFrames[ClientId] = &Observed;
		}

		int Eligible = 0;
		bool FinishTimeKnown = false;
		int FinishTimeMillis = -1;
		for(const SImpactGameplayFrame *pFrame : apFinishFrames)
		{
			if(pFrame == nullptr)
				continue;
			const SImpactGameplayFrame &Frame = *pFrame;
			++Eligible;
			const int ClientId = Frame.m_Participant.m_ClientId;
			if(!IsValidClientId(ClientId) || !m_aFinishSignals[ClientId])
				return;
			// A DDNetPlayer finish/best-time field is not a team-finish signal.
			// Finalize only after the corresponding SV_RACEFINISH message carries
			// the server's current team-race time.
			if(!m_aFinishTimeKnown[ClientId])
				return;
			if(!FinishTimeKnown)
			{
				FinishTimeKnown = true;
				FinishTimeMillis = m_aFinishTimeMillis[ClientId];
			}
			else if(FinishTimeMillis != m_aFinishTimeMillis[ClientId])
				return;
		}
		if(Eligible < 2 || !FinishTimeKnown || FinishTimeMillis < 0)
			return;
		for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
		{
			SImpactPhase2Debug Debug;
			Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
			Debug.m_EventType = Episode.m_Type;
			Debug.m_EpisodeId = Episode.m_EpisodeId;
			Debug.m_Target = Episode.m_Target;
			Debug.m_PotentialSaver = Episode.m_Actor;
			Debug.m_StartTick = Episode.m_StartTick;
			Debug.m_RestorationTick = Episode.m_RestoreTick;
			Debug.m_ConfirmationDeadlineTick = ConfirmationDeadline(Episode);
			Debug.m_FrozenBefore = true;
			Debug.m_HookEvidence = Episode.m_HookEvidence;
			Debug.m_HammerEvidence = Episode.m_HammerEvidence;
			Debug.m_RecoveryNeed = Episode.m_RecoveryNeed;
			Debug.m_HookFirstTick = Episode.m_HookFirstTick;
			Debug.m_HookLastTick = Episode.m_HookLastTick;
			Debug.m_HookControlContinuity = Episode.m_HookControlContinuity;
			Debug.m_EpisodeState = Episode.m_State;
			Debug.m_OwnershipStatus = Episode.m_OwnershipStatus;
			Debug.m_FallAlreadyConfirmed = IsFallConfirmed(Episode);
			Debug.m_ConfirmedFall = IsFallConfirmed(Episode);
			if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED)
			{
				Debug.m_DebugLabel = IsFallConfirmed(Episode) ? "FALL_CLEANUP" : "FALL_NOT_EMITTED";
				Debug.m_Reason = IsFallConfirmed(Episode) ?
					"finish closed context; already-confirmed Fall remains immutable" :
					"finish closed an unconfirmed Fall candidate";
			}
			else
			{
				Debug.m_DebugLabel = "SAVE_REJECTED";
				Debug.m_Reason = "eligible participants confirmed finish before recovery confirmation";
			}
			AddDebug(pOutput, Debug);
		}
		m_vPendingEpisodes.clear();
		SImpactEvent Event = MakeEvent(EImpactEventType::FINISH_CONFIRMED, TeamId, {}, {}, ServerTick, ServerTick, "all eligible team participants confirmed finish");
		Event.m_AuthoritativeFinishTimeMillis = FinishTimeMillis;
		Event.m_vTags.push_back("FINISH");
		pOutput->m_vEvents.push_back(std::move(Event));
		m_FinishRequested = true;
	}

	SImpactPhase2Output CImpactPhase2::Update(uint64_t RunId, int TeamId, int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, const std::vector<SImpactGameplayMessage> &vMessages)
	{
		SImpactPhase2Output Output;
		if(RunId == 0 || !IsValidTeam(TeamId))
		{
			Reset();
			return Output;
		}
		if(m_RunId != RunId || m_TeamId != TeamId)
		{
			Reset();
			m_RunId = RunId;
			m_TeamId = TeamId;
		}

		std::vector<SImpactGameplayFrame> vSortedFrames = vFrames;
		std::sort(vSortedFrames.begin(), vSortedFrames.end(), [](const SImpactGameplayFrame &Left, const SImpactGameplayFrame &Right) {
			if(Left.m_Participant.m_ClientId != Right.m_Participant.m_ClientId)
				return Left.m_Participant.m_ClientId < Right.m_Participant.m_ClientId;
			return ParticipantIdLess(Left.m_Participant, Right.m_Participant);
		});
		const int64_t ServerTick = !vSortedFrames.empty() && vSortedFrames.front().m_ServerTick >= 0 ? vSortedFrames.front().m_ServerTick : 0;
		// Reset/wipe is an authoritative lifecycle barrier. Process it before any
		// snapshot can be interpreted as a recovery or a new movement transition;
		// the post-reset frame becomes only the next clean baseline.
		const bool ResetBoundary = ProcessResetBoundaryMessages(ServerTick, TeamId, &Output, vMessages);
		if(!ResetBoundary)
		{
			for(const SImpactGameplayMessage &Message : vMessages)
			{
				if(!Message.m_Confirmed || (Message.m_TeamId != 0 && Message.m_TeamId != TeamId))
					continue;
				const bool EngagementSignal = Message.m_Type == EImpactGameplayMessageType::CONFIRMED_HARM ||
					Message.m_Type == EImpactGameplayMessageType::CONFIRMED_ALED ||
					Message.m_Type == EImpactGameplayMessageType::CONFIRMED_MISSED_CATCH ||
					Message.m_Type == EImpactGameplayMessageType::CONFIRMED_PREVENTED_PROGRESS_LOSS ||
					Message.m_Type == EImpactGameplayMessageType::CONFIRMED_RECOVERY_DIFFICULTY_INCREASE ||
					Message.m_Type == EImpactGameplayMessageType::CONFIRMED_JUMP_RESET;
				if(!EngagementSignal)
					continue;
				const int64_t ProofTick = Message.m_ServerTick >= 0 ? Message.m_ServerTick : ServerTick;
				AddEngagementProof(vSortedFrames, Message.m_Actor, ProofTick, "canonical current-run gameplay message", &Output);
				AddEngagementProof(vSortedFrames, Message.m_Target, ProofTick, "canonical current-run gameplay message", &Output);
			}
			// Resolve an already-completed 0.7-second problem boundary before a
			// same-tick refreeze is classified. This keeps a post-boundary freeze as a
			// new episode while a shorter unfreeze remains part of the old one.
			CloseStableProblemEpisodes(ServerTick, TickSpeed, vSortedFrames, &Output);
			// Open/resolve the target's own problem episode before recording same-frame
			// rescue interactions. This gives every accepted target frame a single,
			// correctly bound problem identity and prevents the interaction recorder
			// from having to manufacture a recovery object for an episode that has not
			// been routed yet.
			TrackSimpleProblemEpisodes(vSortedFrames, &Output);
			RecordSimpleInteractions(TickSpeed, vSortedFrames, &Output);
			TrackJumpResetEpisodes(vSortedFrames, &Output);
			DetectSimpleRestorations(TickSpeed, vSortedFrames, &Output);
		}
		TrackFrames(ServerTick, vSortedFrames);
		ResolvePending(ServerTick, TeamId, &Output, vSortedFrames);
		ProcessMessages(ServerTick, TickSpeed, TeamId, &Output, vSortedFrames, vMessages, ResetBoundary);
		MaybeFinish(ServerTick, TeamId, &Output, vSortedFrames);
		if(ResetBoundary || m_FinishRequested)
		{
			if(m_ClutchEpisodeActive)
			{
				SImpactPhase2Debug Debug;
				Debug.m_Stage = EImpactPhase2DebugStage::CANCELLED;
				Debug.m_DebugLabel = "CLUTCH_EPISODE_CLEAR";
				Debug.m_ClutchEpisodeId = m_ClutchEpisodeId;
				Debug.m_PotentialSaver = m_ClutchActor;
				Debug.m_TeamId = TeamId;
				Debug.m_ServerTick = ServerTick;
				Debug.m_ClutchArmedTick = m_ClutchArmedTick;
				Debug.m_Reason = std::string("reason=") + (ResetBoundary ? "reset" : "finish");
				AddDebug(&Output, Debug);
			}
			m_ClutchEpisodeActive = false;
			m_ClutchEpisodeConsumed = false;
			m_ClutchAwaitingNewCrisis = false;
			m_ClutchActor = {};
			m_ClutchArmedTick = -1;
			m_ClutchEpisodeId = 0;
			m_ClutchProvisionalObserved = false;
			m_ClutchProvisionalActor = {};
			m_ClutchProvisionalTick = -1;
			m_LastClutchEvaluationKey.clear();
		}
		else
			UpdateClutchEpisodeState(ServerTick, TeamId, vSortedFrames, &Output);
		Output.m_vPendingEpisodes = m_vPendingEpisodes;
		for(SImpactPhase2Debug &Debug : Output.m_vDebug)
			if(Debug.m_EmissionTick < 0)
				Debug.m_EmissionTick = ServerTick;
		return Output;
	}

	void CImpactPhase2::SetPendingEpisodes(const std::vector<SImpactPendingEpisode> &vEpisodes)
	{
		m_vPendingEpisodes.clear();
		m_vPendingEpisodes.reserve(std::min(vEpisodes.size(), MAX_PENDING_EPISODES));
		for(const SImpactPendingEpisode &Episode : vEpisodes)
		{
			if(Episode.m_EpisodeId == 0 || !Episode.m_Target.IsValid() || m_vPendingEpisodes.size() >= MAX_PENDING_EPISODES)
				continue;
			SImpactPendingEpisode Copy = Episode;
			NormalizePendingEpisode(Copy);
			m_NextEpisodeId = std::max(m_NextEpisodeId, Copy.m_EpisodeId + 1);
			m_NextClutchEpisodeId = std::max(m_NextClutchEpisodeId, Copy.m_ClutchEpisodeId + 1);
			if(Copy.m_CausalRecoveryProof.m_Valid)
				m_NextRecoveryProofId = std::max(m_NextRecoveryProofId, Copy.m_CausalRecoveryProof.m_ProofId + 1);
			m_vPendingEpisodes.push_back(std::move(Copy));
		}
	}
}
