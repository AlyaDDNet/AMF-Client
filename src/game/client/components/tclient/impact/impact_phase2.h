#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PHASE2_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PHASE2_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_types.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>
#include <game/teamscore.h>

#include <array>
#include <string>
#include <vector>

namespace AmfImpact
{
	enum class EImpactPositionalTeleportType
	{
		NONE,
		NORMAL,
		EVIL,
	};

	// Raw server-snapshot input. Both the live adapter and replay/live-equivalent
	// tests must pass through NormalizeGameplayFrame before Phase2 sees a frame.
	struct SImpactGameplayInput
	{
		SParticipantId m_Participant;
		int m_TeamId = 0;
		int64_t m_ServerTick = -1;
		bool m_Present = false;
		bool m_Confirmed = true;
		bool m_CurrentCharacterKnown = false;
		bool m_PreviousCharacterKnown = false;
		// Server-authored SpecChar snapshot: the participant's CCharacter is
		// physically paused/removed from the world, not merely absent this tick.
		bool m_PhysicallyPausedCharacter = false;
		bool m_PositionKnown = false;
		bool m_VelocityKnown = false;
		bool m_SupportKnown = false;
		bool m_Supported = false;
		// Positive production evidence that the target is in stable physical
		// contact with one or more other characters and its ordinary freeze timer
		// is counting down without a refresh. This is not inferred from proximity
		// or map support; the live adapter derives it from authoritative character
		// snapshots and the existing character-collision geometry.
		bool m_DynamicSupportKnown = false;
		bool m_DynamicSupport = false;
		// Collision/support is an observation, not proof of a counterfactual
		// autonomous recovery. The live adapter may leave the latter unknown until
		// the episode itself provides enough authoritative outcome evidence.
		bool m_SafeCollisionPositionKnown = false;
		bool m_SafeCollisionPosition = false;
		// Static/front-layer ordinary freeze tile at the authoritative character
		// center. Switched tiles are not treated as positive evidence by this bit.
		bool m_OrdinaryFreezeTileKnown = false;
		bool m_OrdinaryFreezeTile = false;
		bool m_AutonomousRecoveryKnown = false;
		bool m_AutonomousRecoveryPossible = false;
		// Save-only counterfactual proof from the last authoritative target sample
		// before a newly observed teammate hook. It never changes Fall/Clutch state.
		int64_t m_PreActionAutonomousSelfSafeTick = -1;
		bool m_AliveKnown = false;
		bool m_Alive = false;
		bool m_FreezeKnown = false;
		bool m_Frozen = false;
		bool m_DeepFrozen = false;
		bool m_LiveFrozen = false;
		int64_t m_FreezeEnd = 0;
		int64_t m_FreezeStart = -1;
		// Server-confirmed jump state. The live adapter fills this from the
		// character core/DDNet character objects; missing metadata stays UNKNOWN.
		EImpactJumpState m_JumpState = EImpactJumpState::UNKNOWN;
		// A self-owned downward attack edge observed between two confirmed
		// character snapshots. This is evidence for the live Jump Reset producer,
		// not an intent shortcut and not a Fall cause by itself.
		bool m_DownwardShotKnown = false;
		bool m_DownwardShot = false;
		// Ordinary DDNet snapshots do not carry the cause/intent of a freeze
		// entry. Keep this separate from the observable shot/freeze sequence so
		// an unproven panic shot can never become a confirmed reset.
		bool m_SelfInitiatedFreezeKnown = false;
		bool m_SelfInitiatedFreeze = false;
		// Exact map death-tile occupancy is a harmful physical producer. A separate
		// confirmed pre-freeze teammate cause still suppresses victim Fall.
		bool m_HarmfulOutcomeCandidateKnown = false;
		bool m_HarmfulOutcomeCandidate = false;
		// Positional tele-in metadata is kept separate from weapon, hook and
		// checkpoint teleports. The output-set flags describe the complete map
		// output set before a teleport is resolved; they never pick a random output.
		EImpactPositionalTeleportType m_TeleportType = EImpactPositionalTeleportType::NONE;
		int m_TeleportNumber = 0;
		bool m_TeleportOutputSetKnown = false;
		bool m_TeleportOutputSetAllSafe = false;
		bool m_TeleportOutputSetAllHarmful = false;
		bool m_TeleportOutputSetClearsDeepFreeze = false;
		// A single numbered output is a deterministic destination from map data;
		// with multiple outputs the actual destination is known only after the
		// authoritative snapshot can match the current position to an output tile.
		bool m_TeleportDestinationKnown = false;
		bool m_TeleportDestinationSafe = false;
		bool m_TeleportDestinationHarmful = false;
		bool m_TeleportDestinationClearsDeepFreeze = false;
		bool m_SoloKnown = false;
		bool m_Solo = false;
		bool m_Eligible = false;
		bool m_Engaged = false;
		bool m_Finalized = false;
		EImpactRecoveryNeed m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		// This is an explicit normalized harm-attribution input. The live DDNet
		// adapter leaves it UNKNOWN unless an authoritative producer can prove the
		// cause; RecoveryNeed must never be used as a substitute for it.
		EImpactHarmCause m_HarmCause = EImpactHarmCause::UNKNOWN;
		ECausalConfidence m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
		bool m_Finished = false;
		// Filled only by an authoritative finish message in the live path. A
		// missing value must not be replaced by the local analysis duration.
		bool m_FinishTimeKnown = false;
		int m_FinishTimeMillis = -1;
		bool m_HookKnown = false;
		int m_HookedPlayer = -1;
		int m_HookState = 0;
		// Positive, target-specific evidence that the hook materially supported
		// the target during this recovery episode. A grabbed hook state alone is
		// not this evidence; the live adapter leaves it unknown when the current
		// DDNet protocol cannot prove the causal role.
		bool m_MeaningfulHookSupportKnown = false;
		bool m_MeaningfulHookSupport = false;
		bool m_AttackKnown = false;
		int m_AttackTick = -1;
		int m_Weapon = -1;
		int m_X = 0;
		int m_Y = 0;
		int m_VelX = 0;
		int m_VelY = 0;
		int m_TargetX = 0;
		int m_TargetY = 0;
		// No current DDNet client object supplies target-specific hammer ownership
		// to the live adapter. This is therefore false for live frames unless a
		// future authoritative protocol source explicitly fills it.
		bool m_HammerTargetEvidenceKnown = false;
		bool m_HammerTargetEvidence = false;
	};

	// This is a compact, server-confirmed view of the gameplay state needed by
	// the Phase 2 resolver. It intentionally contains no Render() or prediction
	// state. Fields that cannot be filled authoritatively by the current client
	// remain false/unknown; the practical baseline resolver uses only the
	// observable transition and the explicit autonomous-safe exception.
	struct SImpactGameplayFrame
	{
		SParticipantId m_Participant;
		int m_TeamId = 0;
		int64_t m_ServerTick = -1;
		bool m_Present = false;
		bool m_CurrentCharacterKnown = false;
		bool m_PreviousCharacterKnown = false;
		bool m_PhysicallyPausedCharacter = false;
		bool m_PositionKnown = false;
		bool m_VelocityKnown = false;
		bool m_SupportKnown = false;
		bool m_Supported = false;
		bool m_DynamicSupportKnown = false;
		bool m_DynamicSupport = false;
		bool m_SafeCollisionPositionKnown = false;
		bool m_SafeCollisionPosition = false;
		bool m_OrdinaryFreezeTileKnown = false;
		bool m_OrdinaryFreezeTile = false;
		bool m_AutonomousRecoveryKnown = false;
		bool m_AutonomousRecoveryPossible = false;
		// Marker set by the isolated native shadow evaluator. It distinguishes a
		// native UNRESOLVED result from a frame that was never evaluated; unresolved
		// remains neutral and cannot be promoted by the generic Fall fallback.
		bool m_AutonomousRecoveryEvaluated = false;
		// A native autonomous replay from the authoritative sample before one
		// actor's new interaction. Kept separate from ordinary SELF_SAFE so it can
		// exclude only that actor's non-causal Save attribution.
		int64_t m_PreActionAutonomousSelfSafeTick = -1;
		bool m_AliveKnown = false;
		bool m_Active = false;
		bool m_Alive = false;
		bool m_FreezeKnown = false;
		bool m_Frozen = false;
		bool m_DeepFrozen = false;
		bool m_LiveFrozen = false;
		// True only when the live adapter has an authoritative current snapshot
		// whose character object tick is older than that snapshot's server tick.
		// Freeze lifecycle evidence may still be used in this case, while physical
		// position predicates continue to use the frame's own known/unknown flags.
		bool m_AuthoritativePhysicalStateStale = false;
		bool m_SoloKnown = false;
		EImpactJumpState m_JumpState = EImpactJumpState::UNKNOWN;
		bool m_DownwardShotKnown = false;
		bool m_DownwardShot = false;
		bool m_SelfInitiatedFreezeKnown = false;
		bool m_SelfInitiatedFreeze = false;
		bool m_HarmfulOutcomeCandidateKnown = false;
		bool m_HarmfulOutcomeCandidate = false;
		EImpactPositionalTeleportType m_TeleportType = EImpactPositionalTeleportType::NONE;
		int m_TeleportNumber = 0;
		bool m_TeleportOutputSetKnown = false;
		bool m_TeleportOutputSetAllSafe = false;
		bool m_TeleportOutputSetAllHarmful = false;
		bool m_TeleportOutputSetClearsDeepFreeze = false;
		bool m_TeleportDestinationKnown = false;
		bool m_TeleportDestinationSafe = false;
		bool m_TeleportDestinationHarmful = false;
		bool m_TeleportDestinationClearsDeepFreeze = false;
		EImpactPhysicalOutcome m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
		bool m_Solo = false;
		bool m_Eligible = false;
		EImpactRecoveryNeed m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		EImpactHarmCause m_HarmCause = EImpactHarmCause::UNKNOWN;
		ECausalConfidence m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
		bool m_Finished = false;
		bool m_Engaged = false;
		bool m_Finalized = false;
		bool m_FinishTimeKnown = false;
		int m_FinishTimeMillis = -1;
		bool m_Confirmed = true;
		int64_t m_FreezeEnd = 0;
		int64_t m_FreezeStart = -1;
		int m_X = 0;
		int m_Y = 0;
		int m_VelX = 0;
		int m_VelY = 0;
		int m_HookedPlayer = -1;
		int m_HookState = 0;
		bool m_HookKnown = false;
		// This is an explicit normalized causal-evidence bit, not a count of hook
		// observations. It is false/unknown for ordinary live snapshots unless an
		// authoritative producer can establish meaningful support.
		bool m_MeaningfulHookSupportKnown = false;
		bool m_MeaningfulHookSupport = false;
		// Compatibility alias for older callers. Normalized live/test frames set
		// this exactly to m_HookKnown; Phase2 never treats it as optional.
		bool m_HookStateConfirmed = false;
		bool m_AttackKnown = false;
		int m_AttackTick = -1;
		int m_Weapon = -1;
		int m_TargetX = 0;
		int m_TargetY = 0;
		// Target-specific authoritative hammer evidence. A generic attack tick or
		// weapon switch is deliberately not enough to set this in live frames.
		bool m_HammerTargetEvidenceKnown = false;
		bool m_HammerTargetEvidence = false;
	};

	SImpactGameplayFrame NormalizeGameplayFrame(const SImpactGameplayInput &Input);

	enum class EImpactGameplayMessageType
	{
		KILL,
		TEAM_WIPE,
		FINISH,
		TEAM_RESET,
		DISCONNECT,
		CONFIRMED_HARM,
		CONFIRMED_ALED,
		CONFIRMED_MISSED_CATCH,
		CONFIRMED_PREVENTED_PROGRESS_LOSS,
		CONFIRMED_RECOVERY_DIFFICULTY_INCREASE,
		CONFIRMED_JUMP_RESET,
	};

	struct SImpactGameplayMessage
	{
		uint64_t m_MessageId = 0;
		EImpactGameplayMessageType m_Type = EImpactGameplayMessageType::KILL;
		int64_t m_ServerTick = -1;
		bool m_Confirmed = true;
		int m_Killer = -1;
		int m_Victim = -1;
		int m_Weapon = WEAPON_WORLD;
		int m_TeamId = 0;
		int m_First = -1;
		int m_ClientId = -1;
		// SV_RACEFINISH.m_Time is the authoritative current team-race time in
		// milliseconds. It is intentionally distinct from DDNetPlayer best time.
		int m_FinishTimeMillis = -1;
		SParticipantId m_Actor;
		SParticipantId m_Target;
		ECausalConfidence m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
		bool m_CausalConfirmed = false;
		bool m_Feasible = false;
		bool m_OutcomeWorsened = false;
		bool m_ThirdPartyInterference = false;
		bool m_IntentionalReset = false;
		EImpactPhysicalOutcome m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
		std::string m_Reason;
	};

	enum class EImpactPhase2DebugStage
	{
		CANDIDATE,
		CONFIRMED,
		CANCELLED,
	};

	struct SImpactHookSpanDebug
	{
		SParticipantId m_Actor;
		SParticipantId m_Target;
		int64_t m_StartTick = -1;
		int64_t m_LastGrabbedTick = -1;
		int64_t m_EndTick = -1;
		uint32_t m_ConfirmedObservations = 0;
		bool m_Active = false;
		bool m_Rehook = false;
		bool m_MeaningfulControl = false;
	};

	struct SImpactPhase2Debug
	{
		// Diagnostic-only label. It does not participate in event resolution or
		// persistence and is intentionally kept separate from gameplay semantics.
		std::string m_DebugLabel;
		EImpactPhase2DebugStage m_Stage = EImpactPhase2DebugStage::CANDIDATE;
		EImpactEventType m_EventType = EImpactEventType::SAVE_CONFIRMED;
		uint64_t m_EpisodeId = 0;
		uint64_t m_ClutchEpisodeId = 0;
		uint64_t m_RelatedFallEpisodeId = 0;
		uint64_t m_RelatedRecoveryEpisodeId = 0;
		uint64_t m_ProblemEpisodeId = 0;
		uint64_t m_EventId = 0;
		uint64_t m_InteractionId = 0;
		SParticipantId m_Target;
		SParticipantId m_PotentialSaver;
		// The phase2 source tick is the tick at which the observed evidence was
		// produced. Emission is kept separately so sparse-frame cleanup records do
		// not look as if they were emitted at their original source tick.
		int m_TeamId = 0;
		int64_t m_ServerTick = -1;
		int64_t m_EmissionTick = -1;
		int m_UnfrozenCount = -1;
		int64_t m_ClutchArmedTick = -1;
		int64_t m_StartTick = -1;
		int64_t m_RestorationTick = -1;
		int64_t m_ConfirmationDeadlineTick = -1;
		int64_t m_ConfirmationStartTick = -1;
		int64_t m_ConfirmedTicks = -1;
		int64_t m_RequiredTicks = -1;
		bool m_Present = false;
		bool m_TargetSnapshotAvailable = false;
		bool m_ActorSnapshotAvailable = false;
		bool m_TargetPredictedStateAvailable = false;
		bool m_ActorTargetInteractionObservable = false;
		bool m_AliveKnown = false;
		bool m_Alive = false;
		bool m_Started = false;
		bool m_Eligible = false;
		bool m_FreezeKnown = false;
		bool m_FrozenBefore = false;
		bool m_FrozenNow = false;
		bool m_DeepFrozen = false;
		int64_t m_FreezeStart = -1;
		int64_t m_FreezeEnd = 0;
		bool m_PositionKnown = false;
		bool m_VelocityKnown = false;
		bool m_SupportKnown = false;
		bool m_Supported = false;
		bool m_SafeCollisionPositionKnown = false;
		bool m_SafeCollisionPosition = false;
		bool m_OrdinaryFreezeTileKnown = false;
		bool m_OrdinaryFreezeTile = false;
		bool m_AutonomousRecoveryKnown = false;
		bool m_AutonomousRecoveryPossible = false;
		bool m_AutonomousRecoveryEvaluated = false;
		bool m_AutonomousSelfRecoveryProven = false;
		int64_t m_SelfSafeEvidenceTick = -1;
		int64_t m_FirstPhysicalChangeTick = -1;
		bool m_DisplacementRecoveryEvidence = false;
		int m_X = 0;
		int m_Y = 0;
		int m_VelX = 0;
		int m_VelY = 0;
		bool m_HookEvidence = false;
		bool m_HammerEvidence = false;
		bool m_HookKnown = false;
		int m_HookedPlayer = -1;
		int m_HookState = 0;
		bool m_HammerTargetEvidenceKnown = false;
		bool m_TargetSpecificEvidence = false;
		EImpactUnfreezeCause m_UnfreezeCause = EImpactUnfreezeCause::UNKNOWN;
		bool m_UnknownInterruption = false;
		EImpactRecoveryNeed m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		EImpactRecoveryNeed m_InitialRecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		EImpactRecoveryNeed m_PreviousRecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
		EImpactJumpState m_JumpStateBeforeShot = EImpactJumpState::UNKNOWN;
		bool m_DownwardShotKnown = false;
		bool m_DownwardShot = false;
		bool m_JumpResetShotSeen = false;
		int64_t m_JumpResetShotTick = -1;
		bool m_SelfOriginEvidence = false;
		bool m_ExternalCauseEvidence = false;
		bool m_FallSuppressed = false;
		ECausalConfidence m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
		int64_t m_HookFirstTick = -1;
		int64_t m_HookLastTick = -1;
		bool m_HookControlContinuity = false;
		uint64_t m_RescueProofId = 0;
		int64_t m_HarmStartTick = -1;
		int64_t m_FirstProvenTick = -1;
		int64_t m_ActionTick = -1;
		bool m_PreActionSafe = false;
		bool m_RecoveryCreated = false;
		bool m_CandidateCreated = false;
		EImpactEpisodeState m_EpisodeState = EImpactEpisodeState::NONE;
		EImpactEpisodeState m_EpisodeStateBefore = EImpactEpisodeState::NONE;
		EImpactEpisodeState m_EpisodeStateAfter = EImpactEpisodeState::NONE;
		EOwnershipStatus m_OwnershipStatus = EOwnershipStatus::NONE;
		int m_Killer = -1;
		int m_Weapon = -1;
		bool m_ManualKill = false;
		bool m_Reset = false;
		bool m_Disconnect = false;
		bool m_TeammateCause = false;
		bool m_ConfirmedFall = false;
		EImpactPhysicalOutcome m_PhysicalOutcome = EImpactPhysicalOutcome::UNKNOWN;
		EImpactPhysicalOutcome m_PreActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
		EImpactPhysicalOutcome m_PostActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
		bool m_PhysicalChangeEvidence = false;
		bool m_DecisivePhysicalEvidence = false;
		bool m_PreActionSafeKnown = false;
		// Save-side diagnostics: platform preexistence is causality evidence, while
		// only a positive autonomous proof may make pre_action_safe true.
		std::string m_PreActionSafeSource;
		std::string m_NativePreActionResult;
		bool m_PlatformPreexistedBeforeAction = false;
		bool m_FreezeRefreshObservedBeforeAction = false;
		// Diagnostic-only state: true means a later lifecycle path observed that
		// this Fall had already been emitted at harmful entry.
		bool m_FallAlreadyConfirmed = false;
		EImpactHarmCause m_HarmCause = EImpactHarmCause::UNKNOWN;
		ECausalConfidence m_HarmCausalConfidence = ECausalConfidence::INSUFFICIENT;
		bool m_ParticipantFound = false;
		bool m_LastCharacterContext = false;
		std::string m_Reason;
		SRawStats m_RawDelta;
		std::vector<SImpactHookSpanDebug> m_vHookSpans;
		std::vector<SParticipantId> m_vContributors;
		std::vector<SImpactFunctionalRosterEntry> m_vFunctionalRoster;
	};

	struct SImpactPhase2Output
	{
		std::vector<SImpactEvent> m_vEvents;
		struct SEngagementProof
		{
			SParticipantId m_Participant;
			int64_t m_Tick = -1;
			std::string m_Reason;
		};
		std::vector<SEngagementProof> m_vEngagementProofs;
		std::vector<SImpactPendingEpisode> m_vPendingEpisodes;
		std::vector<SImpactPhase2Debug> m_vDebug;
		std::vector<SImpactForensicEvent> m_vForensic;
		bool m_ForensicTraceTruncated = false;
		uint64_t m_ForensicDroppedEvents = 0;
	};

	class CImpactPhase2
	{
		struct SHookSpan
		{
			int64_t m_StartTick = -1;
			int64_t m_LastGrabbedTick = -1;
			int64_t m_EndTick = -1;
			uint32_t m_ConfirmedObservations = 0;
			bool m_MeaningfulSupport = false;
			bool m_Active = false;
		};

		struct SCausalInteraction
		{
			uint64_t m_InteractionId = 0;
			SParticipantId m_Actor;
			SParticipantId m_Target;
			int64_t m_FirstTick = -1;
			bool m_HammerEvidence = false;
			int64_t m_HammerFirstTick = -1;
			int64_t m_HammerLastTick = -1;
			// False means the frame source could not determine whether a
			// target-specific hammer hit occurred for this actor/episode. Missing
			// protocol evidence must not later be rewritten as NATURAL unfreeze.
			bool m_HammerAttributionKnown = false;
			bool m_HookEvidence = false;
			int64_t m_HookFirstTick = -1;
			int64_t m_HookLastTick = -1;
			std::vector<SHookSpan> m_vHookSpans;
			// Aggregated explicit target-specific support evidence from this
			// episode's hook spans. It is never derived from an observation count.
			bool m_HookControlContinuity = false;
			bool m_TargetSpecificEvidence = false;
			bool m_RelevantToThisFreezeEpisode = true;
			EImpactRecoveryNeed m_RecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
			ECausalConfidence m_CausalConfidence = ECausalConfidence::INSUFFICIENT;
			int64_t m_LastTick = -1;
			// Physical evidence is kept per actor/target interaction for the full
			// recovery episode. Presence in this history is not Save credit by itself.
			EImpactPhysicalOutcome m_PreActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
			EImpactPhysicalOutcome m_PostActionOutcome = EImpactPhysicalOutcome::UNKNOWN;
			bool m_PreActionProblemStateKnown = false;
			bool m_PreActionAutonomousSafe = false;
			bool m_PreActionNativeEvaluated = false;
			bool m_PreActionNativeSelfSafe = false;
			bool m_PreActionPlatformPreexisted = false;
			bool m_PreActionFreezeRefreshObserved = false;
			bool m_PreActionSafeCollisionPositionKnown = false;
			bool m_PreActionSafeCollisionPosition = false;
			bool m_PhysicalChangeObserved = false;
			// A movement delta first observed while the target was still frozen is
			// the episode-local physical link between the interaction and the later
			// recovery. A delta first seen after natural unfreeze is not, by itself,
			// evidence that the old interaction caused the recovery.
			bool m_PhysicalChangeWhileFrozen = false;
			bool m_CausalSafeTransitionObserved = false;
			bool m_CausalContributionObserved = false;
			int64_t m_SafeTransitionTick = -1;
			int64_t m_ActionStartTick = -1;
			bool m_ActionStartPositionKnown = false;
			int m_ActionStartX = 0;
			int m_ActionStartY = 0;
			bool m_ActionStartVelocityKnown = false;
			int m_ActionStartVelX = 0;
			int m_ActionStartVelY = 0;
			// Retain the authoritative timer/state at action start. This lets a
			// previously UNKNOWN episode prove an early restoration without inventing
			// a time window; natural expiry remains non-causal.
			int64_t m_PreActionFreezeEnd = 0;
			bool m_PreActionDeepFrozen = false;
			bool m_PreActionDynamicSupportKnown = false;
			bool m_PreActionDynamicSupport = false;
			bool m_PreActionTeleportDestinationKnown = false;
			bool m_PreActionTeleportDestinationSafe = false;
			bool m_ActionActive = false;
			// Keep observing the physical result after hook release. The action belongs
			// to the whole freeze/recovery episode, not to the hook's last active tick.
			bool m_TrackPostActionOutcome = false;
			// Positive native no-interaction replay at this source tick showed that
			// this actor's interaction was unnecessary before its first physical delta.
			int64_t m_PreActionAutonomousSelfSafeTick = -1;
			int64_t m_FirstPhysicalChangeTick = -1;
			int64_t m_LastPhysicalChangeTick = -1;
		};

		struct SObservedState
		{
			bool m_Valid = false;
			SImpactGameplayFrame m_Frame;
			// One bounded prior frame is enough to distinguish a continuous safe
			// platform observation from a one-frame edge contact. It is physical
			// history only; it is not a new Fall/Save decision state.
			bool m_HasPreviousFrame = false;
			SImpactGameplayFrame m_PreviousFrame;
			// A live kill snapshot can still contain PlayerInfo while the
			// CNetObj_Character has already disappeared. Keep the last confirmed
			// character frame for outcome context; it is never used as a current
			// gameplay frame or as a fabricated transition.
			bool m_HasLastKnownCharacter = false;
			SImpactGameplayFrame m_LastKnownCharacterFrame;
			bool m_HasLastKnownFreeze = false;
			SImpactGameplayFrame m_LastKnownFreezeFrame;
			int64_t m_AliveUnfrozenSince = -1;
			int64_t m_PreviousAliveUnfrozenSince = -1;
			// Stable problem-episode identity. A brief unfreeze is part of the same
			// episode; this is reset only after the authoritative 0.7-second boundary.
			int64_t m_ProblemStartTick = -1;
			bool m_ProblemEpisodeStartedThisFrame = false;
			bool m_ProblemOriginTeammateCaused = false;
			// Positive autonomous recovery evidence is scoped to this exact problem
			// episode. It is not a second lifecycle state: it is only the immutable
			// positive veto recorded before any causal teammate action. The observed
			// participant identity and m_ProblemStartTick provide generation/episode
			// isolation; the source tick is retained for diagnostics.
			bool m_AutonomousSelfRecoveryProven = false;
			uint64_t m_AutonomousSelfRecoveryEpisodeId = 0;
			int64_t m_AutonomousSelfRecoverySourceTick = -1;
			// Passive episode-scoped observation of the last isolated native result.
			// Save uses only a positive result; UNRESOLVED never becomes self-safe.
			bool m_AutonomousSelfRecoveryNativeProof = false;
			bool m_NativeAutonomousResultObserved = false;
			bool m_NativeAutonomousResultSelfSafe = false;
			int64_t m_NativeAutonomousResultTick = -1;
			// A positive autonomous-harmful fact observed before the first
			// target-specific interaction remains valid for this episode. This is a
			// bounded evidence latch, not a new lifecycle state and not Save state.
			bool m_AutonomousHarmfulEvidenceProven = false;
			// SELF_SAFE invalidation clears only the old safe proof. It is not itself
			// harmful evidence. A later Fall in this same problem episode is allowed
			// only after a fresh positive harmful result has been observed.
			int64_t m_SelfSafeInvalidatedTick = -1;
			int64_t m_PostInvalidationHarmfulEvidenceTick = -1;
			// A same-frame helper and first safe-platform observation leave the
			// pre-action outcome causally unresolved. Keep the later unsupported
			// frame neutral instead of treating post-action trajectory as Fall proof.
			bool m_ExternalInteractionOutcomeUnavailable = false;
			// Evidence is kept for the whole server-confirmed frozen episode.
			// A single snapshot is not sufficient: a hook can be released, or a
			// hammer attack can be followed by a weapon switch, before restoration.
			std::vector<SCausalInteraction> m_vFrozenInteractions;
			// RecoveryNeed is retained as episode context rather than used as a strict
			// Save gate. A supported platform is not by itself proof of SELF_SAFE.
			EImpactRecoveryNeed m_EpisodeRecoveryNeed = EImpactRecoveryNeed::UNKNOWN;
			// First authoritative unfrozen boundary observed in this physical problem
			// episode. This is lifecycle bookkeeping only: it is not a self-safe latch
			// and is never used to decide Fall or Save attribution.
			bool m_RestorationObserved = false;
			int64_t m_RestorationTick = -1;
			// Generation-scoped live Jump Reset evidence. These fields are cleared on
			// participant replacement, a new freeze episode, or a confirmed outcome;
			// no duration/count threshold is used.
			bool m_JumpResetShotSeen = false;
			int64_t m_JumpResetShotTick = -1;
			EImpactJumpState m_JumpResetStateBefore = EImpactJumpState::UNKNOWN;
			bool m_JumpResetActive = false;
			int64_t m_JumpResetFreezeStart = -1;
		};

		std::array<SObservedState, MAX_CLIENTS> m_aObserved{};
		std::array<bool, MAX_CLIENTS> m_aFinishSignals{};
		std::array<bool, MAX_CLIENTS> m_aFinishTimeKnown{};
		std::array<int, MAX_CLIENTS> m_aFinishTimeMillis{};
		std::vector<SImpactPendingEpisode> m_vPendingEpisodes;
		uint64_t m_RunId = 0;
		int m_TeamId = 0;
		uint64_t m_NextEpisodeId = 1;
		uint64_t m_NextRecoveryProofId = 1;
		uint64_t m_NextClutchEpisodeId = 1;
		uint64_t m_NextInteractionId = 1;
		uint64_t m_LastMessageId = 0;
		bool m_FinishRequested = false;
		bool m_ClutchEpisodeActive = false;
		bool m_ClutchEpisodeConsumed = false;
		// A confirmed Clutch closes the current crisis immediately. Keep a short
		// lifecycle barrier until a genuinely new physical problem episode starts;
		// otherwise an unrelated teammate that is still in the old confirmed Fall
		// would re-arm the just-closed crisis on the same update.
		bool m_ClutchAwaitingNewCrisis = false;
		SParticipantId m_ClutchActor;
		int64_t m_ClutchArmedTick = -1;
		uint64_t m_ClutchEpisodeId = 0;
		// Non-awardable observation retained while an eligible participant is still
		// UNKNOWN. It allows that same rescue window to become provable later, but it
		// cannot itself attach to a Save or produce a Clutch.
		bool m_ClutchProvisionalObserved = false;
		SParticipantId m_ClutchProvisionalActor;
		int64_t m_ClutchProvisionalTick = -1;
		// Diagnostic-only deduplication for event-scoped Clutch evaluation traces.
		// It never participates in Clutch or Save decisions.
		std::string m_LastClutchEvaluationKey;
		bool m_ForensicTraceEnabled = true;

		static bool IsValidTeam(int TeamId);
		static bool IsContinuouslyAliveUnfrozen(const SImpactGameplayFrame &Frame);
		static bool SameParticipant(const SImpactGameplayFrame &Left, const SImpactGameplayFrame &Right);
		static const SImpactGameplayFrame *FindParticipant(const std::vector<SImpactGameplayFrame> &vFrames, const SParticipantId &Id);
		static const SImpactGameplayFrame *FindClient(const std::vector<SImpactGameplayFrame> &vFrames, int ClientId);
		static void AddEngagementProof(std::vector<SImpactGameplayFrame> &vFrames, const SParticipantId &Participant,
			int64_t Tick, const char *pReason, SImpactPhase2Output *pOutput);
		bool FindSoleFunctionalParticipant(const std::vector<SImpactGameplayFrame> &vFrames, int TeamId,
			const SParticipantId *pExcludedParticipant, bool RequireResolved, SParticipantId *pParticipant,
			std::string *pDiagnostic = nullptr, int *pUnknownCount = nullptr, int *pFunctionalCount = nullptr,
			std::vector<SImpactFunctionalRosterEntry> *pFunctionalRoster = nullptr);
		// This is the Save-side positive self-recovery veto reused by Clutch. It
		// does not classify recovery independently; it reports the same evidence
		// that makes Save reject unnecessary teammate assistance.
		bool HasCanonicalSaveSelfRecoveryProof(const SImpactGameplayFrame &Frame) const;
		static bool HasHookEvidence(const SImpactGameplayFrame &Actor, int TargetClientId);
		static SCausalInteraction *FindInteraction(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target);
		static void UpdateHookSpan(SCausalInteraction &Interaction, int64_t Tick, bool MeaningfulSupportEvidence);
		static void CloseHookSpan(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target, int64_t Tick);
		static bool HasMeaningfulHookControl(const SCausalInteraction &Interaction);
		static void ObserveInteractionPhysicalEvidence(SCausalInteraction &Interaction, const SImpactGameplayFrame &Target, bool ActionActive, int64_t SafeOutcomeTick = -1, bool AllowCausalSafeTransition = true);
		static SImpactEvent MakeEvent(EImpactEventType Type, int TeamId, const SParticipantId &Actor, const SParticipantId &Target, int64_t StartTick, int64_t ResolveTick, const char *pReason);
		static void AddHelper(std::vector<SParticipantId> &vHelpers, const SParticipantId &Id);
		static void AddInteraction(std::vector<SCausalInteraction> &vInteractions, const SParticipantId &Actor, const SParticipantId &Target, bool HammerEvidence, bool HookEvidence, bool MeaningfulSupportEvidence, bool HammerAttributionKnown, bool AutonomousSafeObserved, int64_t Tick, const SImpactGameplayFrame &TargetFrame);
		void AddDebug(SImpactPhase2Output *pOutput, const SImpactPhase2Debug &Debug);
		void AddDeathDebug(SImpactPhase2Output *pOutput, const SImpactGameplayMessage &Message, const SParticipantId &Target, bool LastCharacterContext, bool ConfirmedFall, const char *pReason);
		void CancelPending(const SParticipantId &Target, SImpactPhase2Output *pOutput, const char *pReason, bool IncludeFall = true, bool IncludeRecovery = true);
		void ResetObserved();
		void TrackJumpResetEpisodes(const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void TrackSimpleProblemEpisodes(std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void RecordSimpleInteractions(int TickSpeed, std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void TrackFrames(int64_t ServerTick, const std::vector<SImpactGameplayFrame> &vFrames);
		void DetectSimpleRestorations(int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void ResolvePending(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames);
		void CloseStableProblemEpisodes(int64_t ServerTick, int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		bool MaterializeRecovery(const std::vector<SCausalInteraction> &vInteractions, uint64_t RelatedFallEpisodeId, const SParticipantId &Target, int TeamId, int64_t ProblemStartTick, int64_t ServerTick, int TickSpeed, const std::vector<SImpactGameplayFrame> &vContextFrames, const SImpactGameplayFrame &TargetFrame, bool RestorationObserved, int64_t SelfSafeEvidenceTick, SImpactPhase2Output *pOutput, const char **ppRejectReason = nullptr);
		bool HasPreHelperRescueFallCandidate(const SImpactPendingEpisode &Recovery, const SImpactPendingEpisode &FallEpisode, const std::vector<SCausalInteraction> &vInteractions) const;
		bool ConfirmFallEpisode(uint64_t EpisodeId, int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const char *pReason, bool KeepEpisode = false, bool FrozenNow = true, const SImpactGameplayFrame *pPhysicalFrame = nullptr);
		void ResetTransientForBoundary();
		bool ProcessResetBoundaryMessages(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayMessage> &vMessages);
		void ProcessMessages(int64_t ServerTick, int TickSpeed, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames, const std::vector<SImpactGameplayMessage> &vMessages, bool SkipResetMessages = false);
		void ProcessExplicitSignal(const SImpactGameplayMessage &Message, int TickSpeed, int TeamId,
			const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void MaybeFinish(int64_t ServerTick, int TeamId, SImpactPhase2Output *pOutput, const std::vector<SImpactGameplayFrame> &vFrames);
		void UpdateClutchEpisodeState(int64_t ServerTick, int TeamId, const std::vector<SImpactGameplayFrame> &vFrames, SImpactPhase2Output *pOutput);
		void MaybeMarkClutchCandidate(SImpactPendingEpisode &Episode, const std::vector<SImpactGameplayFrame> &vContextFrames, SImpactPhase2Output *pOutput);

	public:
		void Reset();
		static int64_t ConfirmationTicks(int TickSpeed);
		static EImpactPhysicalOutcome EvaluatePhysicalOutcome(const SImpactGameplayFrame &Frame);
		static bool HasPotentialEngagementEvidence(const std::vector<SImpactGameplayFrame> &vFrames, int TeamId,
			const std::vector<SImpactGameplayMessage> &vMessages = {});
		SImpactPhase2Output Update(uint64_t RunId, int TeamId, int TickSpeed, const std::vector<SImpactGameplayFrame> &vFrames, const std::vector<SImpactGameplayMessage> &vMessages);
		void SetRunContext(uint64_t RunId, int TeamId) { m_RunId = RunId; m_TeamId = TeamId; }
		void SetForensicTraceEnabled(bool Enabled) { m_ForensicTraceEnabled = Enabled; }
		void SetPendingEpisodes(const std::vector<SImpactPendingEpisode> &vEpisodes);
		const std::vector<SImpactPendingEpisode> &PendingEpisodes() const { return m_vPendingEpisodes; }
		bool DebugHasLastCharacterContext(int ClientId) const
		{
			return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aObserved[ClientId].m_HasLastKnownCharacter;
		}
		// Query used by the isolated native evaluator. It exposes the existing
		// lifecycle identity and stops reevaluation after a positive episode-scoped
		// self-recovery proof; no negative/unknown evaluator result is retained.
		bool DebugHasActiveFrozenEpisode(const SParticipantId &Participant) const
		{
			if(!Participant.IsValid() || Participant.m_ClientId < 0 || Participant.m_ClientId >= MAX_CLIENTS)
				return false;
			for(const SImpactPendingEpisode &Episode : m_vPendingEpisodes)
			{
				if(Episode.m_Type == EImpactEventType::FALL_CONFIRMED &&
					Episode.m_Target == Participant && Episode.m_TeamId == m_TeamId &&
					Episode.m_FallAlreadyConfirmed)
					return false;
			}
			const SObservedState &Observed = m_aObserved[Participant.m_ClientId];
			return Observed.m_Valid && Observed.m_Frame.m_Participant == Participant &&
				Observed.m_ProblemStartTick >= 0 && Observed.m_Frame.m_Frozen &&
				!Observed.m_AutonomousSelfRecoveryProven;
		}
	};
}

#endif
