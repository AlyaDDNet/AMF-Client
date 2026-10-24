/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_persistence.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>
#include <engine/shared/uuid_manager.h>
#include <engine/storage.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace AmfImpact
{
	struct SActiveLoad
	{
		SRunSnapshot *m_pSnapshot;
		uint64_t *m_pNextRunId;
		uint64_t *m_pNextEventId;
	};

	static void WriteInt64(CJsonWriter &Writer, int64_t Value)
	{
		const std::string StringValue = std::to_string(Value);
		Writer.WriteStrValue(StringValue.c_str());
	}

	static void WriteUint64(CJsonWriter &Writer, uint64_t Value)
	{
		const std::string StringValue = std::to_string(Value);
		Writer.WriteStrValue(StringValue.c_str());
	}

	static void WriteDouble(CJsonWriter &Writer, double Value)
	{
		char aValue[64];
		str_format(aValue, sizeof(aValue), "%.17g", Value);
		Writer.WriteStrValue(aValue);
	}

	static void WriteId(CJsonWriter &Writer, const SParticipantId &Id)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("client_id");
		Writer.WriteIntValue(Id.m_ClientId);
		Writer.WriteAttribute("slot_generation");
		Writer.WriteIntValue(static_cast<int>(Id.m_SlotGeneration));
		Writer.WriteAttribute("dummy");
		Writer.WriteBoolValue(Id.m_Dummy);
		Writer.EndObject();
	}

	static bool IsPersistableCosmeticSnapshot(const SParticipantCosmeticSnapshot &Cosmetics)
	{
		return Cosmetics.m_Available && !Cosmetics.m_SkinName.empty() && Cosmetics.m_SkinName.size() < MAX_SKIN_LENGTH;
	}

	static void WriteCosmeticSnapshot(CJsonWriter &Writer, const SParticipantCosmeticSnapshot &Cosmetics)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("skin_name");
		Writer.WriteStrValue(Cosmetics.m_SkinName.c_str());
		Writer.WriteAttribute("use_custom_color");
		Writer.WriteBoolValue(Cosmetics.m_UseCustomColor);
		Writer.WriteAttribute("body_color");
		Writer.WriteIntValue(Cosmetics.m_ColorBody);
		Writer.WriteAttribute("feet_color");
		Writer.WriteIntValue(Cosmetics.m_ColorFeet);
		Writer.EndObject();
	}

	static void WriteCausalRecoveryProof(CJsonWriter &Writer, const SImpactCausalRecoveryProof &Proof)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("valid");
		Writer.WriteBoolValue(Proof.m_Valid);
		Writer.WriteAttribute("proof_id");
		WriteUint64(Writer, Proof.m_ProofId);
		Writer.WriteAttribute("fall_episode_id");
		WriteUint64(Writer, Proof.m_FallEpisodeId);
		Writer.WriteAttribute("recovery_episode_id");
		WriteUint64(Writer, Proof.m_RecoveryEpisodeId);
		Writer.WriteAttribute("target");
		WriteId(Writer, Proof.m_Target);
		Writer.WriteAttribute("decisive_actor");
		WriteId(Writer, Proof.m_DecisiveActor);
		Writer.WriteAttribute("team_id");
		Writer.WriteIntValue(Proof.m_TeamId);
		Writer.WriteAttribute("harm_start_tick");
		WriteInt64(Writer, Proof.m_HarmStartTick);
		Writer.WriteAttribute("action_tick");
		WriteInt64(Writer, Proof.m_ActionTick);
		Writer.WriteAttribute("first_proven_tick");
		WriteInt64(Writer, Proof.m_FirstProvenTick);
		Writer.WriteAttribute("restore_tick");
		WriteInt64(Writer, Proof.m_RestoreTick);
		Writer.WriteAttribute("hook_first_tick");
		WriteInt64(Writer, Proof.m_HookFirstTick);
		Writer.WriteAttribute("hook_last_tick");
		WriteInt64(Writer, Proof.m_HookLastTick);
		Writer.WriteAttribute("pre_action_outcome");
		Writer.WriteStrValue(PhysicalOutcomeName(Proof.m_PreActionOutcome));
		Writer.WriteAttribute("post_action_outcome");
		Writer.WriteStrValue(PhysicalOutcomeName(Proof.m_PostActionOutcome));
		Writer.WriteAttribute("target_already_safe_before_action");
		Writer.WriteBoolValue(Proof.m_TargetAlreadySafeBeforeAction);
		Writer.WriteAttribute("target_specific_evidence");
		Writer.WriteBoolValue(Proof.m_TargetSpecificEvidence);
		Writer.WriteAttribute("physical_change_evidence");
		Writer.WriteBoolValue(Proof.m_PhysicalChangeEvidence);
		Writer.WriteAttribute("safe_outcome_evidence");
		Writer.WriteBoolValue(Proof.m_SafeOutcomeEvidence);
		Writer.WriteAttribute("hook_evidence");
		Writer.WriteBoolValue(Proof.m_HookEvidence);
		Writer.WriteAttribute("hammer_evidence");
		Writer.WriteBoolValue(Proof.m_HammerEvidence);
		Writer.WriteAttribute("ownership_status");
		Writer.WriteStrValue(OwnershipStatusName(Proof.m_OwnershipStatus));
		Writer.WriteAttribute("confidence");
		Writer.WriteStrValue(CausalConfidenceName(Proof.m_Confidence));
		Writer.WriteAttribute("reason");
		Writer.WriteStrValue(Proof.m_Reason.c_str());
		Writer.WriteAttribute("contributors");
		Writer.BeginArray();
		for(const SParticipantId &Contributor : Proof.m_vContributors)
			WriteId(Writer, Contributor);
		Writer.EndArray();
		Writer.EndObject();
	}

	static void WriteStats(CJsonWriter &Writer, const SRawStats &Stats)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("confirmed_events");
		Writer.WriteIntValue(Stats.m_ConfirmedEvents);
		Writer.WriteAttribute("falls");
		Writer.WriteIntValue(Stats.m_Falls);
		Writer.WriteAttribute("wipes");
		Writer.WriteIntValue(Stats.m_Wipes);
		Writer.WriteAttribute("saves");
		Writer.WriteIntValue(Stats.m_Saves);
		Writer.WriteAttribute("useful_saves");
		Writer.WriteIntValue(Stats.m_UsefulSaves);
		Writer.WriteAttribute("meaningful_skips");
		Writer.WriteIntValue(Stats.m_MeaningfulSkips);
		Writer.WriteAttribute("back_recoveries");
		Writer.WriteIntValue(Stats.m_BackRecoveries);
		Writer.WriteAttribute("harmful_events");
		Writer.WriteIntValue(Stats.m_HarmfulEvents);
		Writer.WriteAttribute("positive_events");
		Writer.WriteIntValue(Stats.m_PositiveEvents);
		Writer.WriteAttribute("aleds");
		Writer.WriteIntValue(Stats.m_Aleds);
		Writer.WriteAttribute("clutches");
		Writer.WriteIntValue(Stats.m_Clutches);
		Writer.WriteAttribute("missed_catches");
		Writer.WriteIntValue(Stats.m_MissedCatches);
		Writer.WriteAttribute("team_mistakes");
		Writer.WriteIntValue(Stats.m_TeamMistakes);
		Writer.WriteAttribute("prevented_progress_loss");
		Writer.WriteIntValue(Stats.m_PreventedProgressLoss);
		Writer.WriteAttribute("recovery_difficulty_increases");
		Writer.WriteIntValue(Stats.m_RecoveryDifficultyIncreases);
		Writer.EndObject();
	}

	static void WriteForensicEvent(CJsonWriter &Writer, const SImpactForensicEvent &Event)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("seq");
		WriteUint64(Writer, Event.m_Sequence);
		Writer.WriteAttribute("run_id");
		WriteUint64(Writer, Event.m_RunId);
		Writer.WriteAttribute("event_id");
		WriteUint64(Writer, Event.m_EventId);
		Writer.WriteAttribute("type");
		Writer.WriteIntValue(static_cast<int>(Event.m_Type));
		Writer.WriteAttribute("tick");
		WriteInt64(Writer, Event.m_Tick);
		Writer.WriteAttribute("team_id");
		Writer.WriteIntValue(Event.m_TeamId);
		Writer.WriteAttribute("target");
		WriteId(Writer, Event.m_Target);
		Writer.WriteAttribute("actor");
		WriteId(Writer, Event.m_Actor);
		Writer.WriteAttribute("problem_id");
		WriteUint64(Writer, Event.m_ProblemEpisodeId);
		Writer.WriteAttribute("fall_id");
		WriteUint64(Writer, Event.m_FallEpisodeId);
		Writer.WriteAttribute("save_id");
		WriteUint64(Writer, Event.m_SaveCandidateId);
		Writer.WriteAttribute("clutch_id");
		WriteUint64(Writer, Event.m_ClutchEpisodeId);
		Writer.WriteAttribute("interaction_id");
		WriteUint64(Writer, Event.m_ActorInteractionId);
		Writer.WriteAttribute("reason");
		Writer.WriteIntValue(static_cast<int>(Event.m_Reason));
		Writer.WriteAttribute("detail");
		Writer.WriteStrValue(Event.m_ReasonDetail.c_str());
		Writer.WriteAttribute("run_state_before");
		Writer.WriteIntValue(static_cast<int>(Event.m_RunStateBefore));
		Writer.WriteAttribute("run_state_after");
		Writer.WriteIntValue(static_cast<int>(Event.m_RunStateAfter));
		Writer.WriteAttribute("episode_state_before");
		Writer.WriteIntValue(static_cast<int>(Event.m_EpisodeStateBefore));
		Writer.WriteAttribute("episode_state_after");
		Writer.WriteIntValue(static_cast<int>(Event.m_EpisodeStateAfter));
		Writer.WriteAttribute("physical_outcome");
		Writer.WriteIntValue(static_cast<int>(Event.m_PhysicalOutcome));
		Writer.WriteAttribute("active_count");
		Writer.WriteIntValue(Event.m_ActiveParticipantCount);
		Writer.WriteAttribute("present_count");
		Writer.WriteIntValue(Event.m_PresentParticipantCount);
		Writer.WriteAttribute("historical_count");
		Writer.WriteIntValue(Event.m_HistoricalParticipantCount);
		Writer.WriteAttribute("target_snapshot");
		Writer.WriteBoolValue(Event.m_TargetSnapshotAvailable);
		Writer.WriteAttribute("actor_snapshot");
		Writer.WriteBoolValue(Event.m_ActorSnapshotAvailable);
		Writer.WriteAttribute("target_predicted");
		Writer.WriteBoolValue(Event.m_TargetPredictedStateAvailable);
		Writer.WriteAttribute("interaction_observable");
		Writer.WriteBoolValue(Event.m_ActorTargetInteractionObservable);
		Writer.WriteAttribute("physical_change_evidence");
		Writer.WriteBoolValue(Event.m_PhysicalChangeEvidence);
		Writer.WriteAttribute("decisive_physical_evidence");
		Writer.WriteBoolValue(Event.m_DecisivePhysicalEvidence);
		Writer.WriteAttribute("decision_eligible");
		Writer.WriteBoolValue(Event.m_DecisionEligible);
		Writer.WriteAttribute("decision_accepted");
		Writer.WriteBoolValue(Event.m_DecisionAccepted);
		Writer.WriteAttribute("autonomous_evaluated");
		Writer.WriteBoolValue(Event.m_AutonomousEvidenceEvaluated);
		Writer.WriteAttribute("autonomous_known");
		Writer.WriteBoolValue(Event.m_AutonomousEvidenceKnown);
		Writer.WriteAttribute("autonomous_possible");
		Writer.WriteBoolValue(Event.m_AutonomousRecoveryPossible);
		Writer.WriteAttribute("self_safe");
		Writer.WriteBoolValue(Event.m_SelfSafeProven);
		Writer.WriteAttribute("self_safe_tick");
		WriteInt64(Writer, Event.m_SelfSafeEvidenceTick);
		Writer.WriteAttribute("restoration_tick");
		WriteInt64(Writer, Event.m_RestorationTick);
		Writer.WriteAttribute("confirmation_start_tick");
		WriteInt64(Writer, Event.m_ConfirmationStartTick);
		Writer.WriteAttribute("confirmation_deadline_tick");
		WriteInt64(Writer, Event.m_ConfirmationDeadlineTick);
		Writer.WriteAttribute("confirmed_ticks");
		WriteInt64(Writer, Event.m_ConfirmedTicks);
		Writer.WriteAttribute("required_ticks");
		WriteInt64(Writer, Event.m_RequiredTicks);
		Writer.WriteAttribute("raw_delta");
		WriteStats(Writer, Event.m_RawDelta);
		Writer.WriteAttribute("functional_roster");
		Writer.BeginArray();
		for(const SImpactFunctionalRosterEntry &Entry : Event.m_vFunctionalRoster)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("id");
			WriteId(Writer, Entry.m_Participant);
			Writer.WriteAttribute("class");
			Writer.WriteIntValue(static_cast<int>(Entry.m_Classification));
			Writer.WriteAttribute("roster_member");
			Writer.WriteBoolValue(Entry.m_RosterMember);
			Writer.WriteAttribute("engaged");
			Writer.WriteBoolValue(Entry.m_Engaged);
			Writer.WriteAttribute("finalized");
			Writer.WriteBoolValue(Entry.m_Finalized);
			Writer.WriteAttribute("reason");
			Writer.WriteStrValue(Entry.m_Reason.c_str());
			Writer.WriteAttribute("alive_known");
			Writer.WriteBoolValue(Entry.m_AliveKnown);
			Writer.WriteAttribute("alive");
			Writer.WriteBoolValue(Entry.m_Alive);
			Writer.WriteAttribute("frozen");
			Writer.WriteBoolValue(Entry.m_Frozen);
			Writer.WriteAttribute("autonomous_known");
			Writer.WriteBoolValue(Entry.m_AutonomousRecoveryKnown);
			Writer.WriteAttribute("autonomous_possible");
			Writer.WriteBoolValue(Entry.m_AutonomousRecoveryPossible);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
	}

	static void WriteEvent(CJsonWriter &Writer, const SImpactEvent &Event)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("event_id");
		WriteUint64(Writer, Event.m_EventId);
		Writer.WriteAttribute("type");
		Writer.WriteStrValue(ImpactEventTypeName(Event.m_Type));
		Writer.WriteAttribute("actor");
		WriteId(Writer, Event.m_Actor);
		Writer.WriteAttribute("target");
		WriteId(Writer, Event.m_Target);
		Writer.WriteAttribute("team_id");
		Writer.WriteIntValue(Event.m_TeamId);
		Writer.WriteAttribute("actor_name");
		Writer.WriteStrValue(Event.m_ActorName.c_str());
		Writer.WriteAttribute("start_tick");
		WriteInt64(Writer, Event.m_StartTick);
		Writer.WriteAttribute("resolve_tick");
		WriteInt64(Writer, Event.m_ResolveTick);
		Writer.WriteAttribute("confirmed");
		Writer.WriteBoolValue(Event.m_Confirmed);
		Writer.WriteAttribute("present");
		Writer.WriteBoolValue(Event.m_Present);
		Writer.WriteAttribute("anti_farm");
		Writer.WriteBoolValue(Event.m_AntiFarm);
		Writer.WriteAttribute("team_utility");
		Writer.WriteIntValue(Event.m_TeamUtility);
		Writer.WriteAttribute("difficulty");
		Writer.WriteIntValue(Event.m_Difficulty);
		Writer.WriteAttribute("traversal_saved");
		Writer.WriteIntValue(Event.m_TraversalSaved);
		Writer.WriteAttribute("recovery_saved");
		Writer.WriteIntValue(Event.m_RecoverySaved);
		Writer.WriteAttribute("run_saving_value");
		Writer.WriteIntValue(Event.m_RunSavingValue);
		Writer.WriteAttribute("execution_quality");
		Writer.WriteIntValue(Event.m_ExecutionQuality);
		Writer.WriteAttribute("positive_contribution");
		Writer.WriteIntValue(Event.m_PositiveContribution);
		Writer.WriteAttribute("harmful_contribution");
		Writer.WriteIntValue(Event.m_HarmfulContribution);
		Writer.WriteAttribute("causal_reason");
		Writer.WriteStrValue(Event.m_CausalReason.c_str());
		Writer.WriteAttribute("episode_state");
		Writer.WriteStrValue(ImpactEpisodeStateName(Event.m_EpisodeState));
		Writer.WriteAttribute("causal_confidence");
		Writer.WriteStrValue(CausalConfidenceName(Event.m_CausalConfidence));
		Writer.WriteAttribute("ownership_status");
		Writer.WriteStrValue(OwnershipStatusName(Event.m_OwnershipStatus));
		Writer.WriteAttribute("before_context");
		Writer.WriteStrValue(Event.m_BeforeContext.c_str());
		Writer.WriteAttribute("after_context");
		Writer.WriteStrValue(Event.m_AfterContext.c_str());
		Writer.WriteAttribute("helpers");
		Writer.BeginArray();
		for(const SParticipantId &Helper : Event.m_vHelpers)
			WriteId(Writer, Helper);
		Writer.EndArray();
		Writer.WriteAttribute("applied_exactly_once");
		Writer.WriteBoolValue(Event.m_AppliedExactlyOnce);
		Writer.WriteAttribute("tags");
		Writer.BeginArray();
		for(const std::string &Tag : Event.m_vTags)
			Writer.WriteStrValue(Tag.c_str());
		Writer.EndArray();
		Writer.WriteAttribute("raw_delta");
		WriteStats(Writer, Event.m_RawDelta);
		Writer.EndObject();
	}

	static void WriteScoringEvidence(CJsonWriter &Writer, const SImpactScoringEvidence &Evidence)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("run_id");
		WriteUint64(Writer, Evidence.m_RunId);
		Writer.WriteAttribute("event_id");
		WriteUint64(Writer, Evidence.m_EventId);
		Writer.WriteAttribute("type");
		Writer.WriteStrValue(ImpactEventTypeName(Evidence.m_Type));
		Writer.WriteAttribute("tick");
		WriteInt64(Writer, Evidence.m_Tick);
		Writer.WriteAttribute("actor");
		WriteId(Writer, Evidence.m_Actor);
		Writer.WriteAttribute("target");
		WriteId(Writer, Evidence.m_Target);
		Writer.WriteAttribute("problem_id");
		WriteUint64(Writer, Evidence.m_ProblemEpisodeId);
		Writer.WriteAttribute("fall_id");
		WriteUint64(Writer, Evidence.m_FallEpisodeId);
		Writer.WriteAttribute("rescue_id");
		WriteUint64(Writer, Evidence.m_RescueEpisodeId);
		Writer.WriteAttribute("clutch_id");
		WriteUint64(Writer, Evidence.m_ClutchEpisodeId);
		Writer.WriteAttribute("interaction_id");
		WriteUint64(Writer, Evidence.m_ActorInteractionId);
		Writer.WriteAttribute("save_event_id");
		WriteUint64(Writer, Evidence.m_LinkedSaveEventId);
		Writer.WriteAttribute("is_save");
		Writer.WriteBoolValue(Evidence.m_IsCanonicalSave);
		Writer.WriteAttribute("clutch_confirmed");
		Writer.WriteBoolValue(Evidence.m_ClutchConfirmed);
		Writer.WriteAttribute("aled_confirmed");
		Writer.WriteBoolValue(Evidence.m_AledConfirmed);
		Writer.EndObject();
	}

	static void WriteScoreSnapshot(CJsonWriter &Writer, const SScoreSnapshot &Score)
	{
		const bool Finite = std::isfinite(Score.m_Rating) && std::isfinite(Score.m_Impact) &&
			std::isfinite(Score.m_Stability) && std::isfinite(Score.m_Consistency);
		const bool Available = Score.m_Available && Finite && Score.m_Rating >= 0.0 &&
			Score.m_Stability >= 0.0 && Score.m_Stability <= 100.0 &&
			Score.m_RatingVersion > 0 && Score.m_ScoringVersion > IMPACT_SCORING_VERSION_UNSET;
		Writer.BeginObject();
		Writer.WriteAttribute("frozen");
		// A stored available score is necessarily already immutable, including
		// snapshots written by the pre-freeze-flag Phase 4 foundation.
		Writer.WriteBoolValue(Score.m_Frozen || Score.m_Available || !Finite);
		Writer.WriteAttribute("available");
		Writer.WriteBoolValue(Available);
		Writer.WriteAttribute("rating");
		WriteDouble(Writer, std::isfinite(Score.m_Rating) ? Score.m_Rating : 0.0);
		Writer.WriteAttribute("impact");
		WriteDouble(Writer, std::isfinite(Score.m_Impact) ? Score.m_Impact : 0.0);
		Writer.WriteAttribute("stability");
		WriteDouble(Writer, std::isfinite(Score.m_Stability) ? Score.m_Stability : 0.0);
		Writer.WriteAttribute("consistency");
		WriteDouble(Writer, std::isfinite(Score.m_Consistency) ? Score.m_Consistency : 0.0);
		Writer.WriteAttribute("tier");
		Writer.WriteIntValue(Score.m_Tier);
		Writer.WriteAttribute("rating_version");
		Writer.WriteIntValue(Score.m_RatingVersion);
		Writer.WriteAttribute("scoring_version");
		Writer.WriteIntValue(Score.m_ScoringVersion);
		Writer.EndObject();
	}

	static void WriteRun(CJsonWriter &Writer, const SRunSnapshot &SnapshotData)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("schema_version");
		Writer.WriteIntValue(SnapshotData.m_SchemaVersion);
		Writer.WriteAttribute("algorithm_version");
		Writer.WriteIntValue(SnapshotData.m_AlgorithmVersion);
		Writer.WriteAttribute("static_analysis_version");
		Writer.WriteIntValue(SnapshotData.m_StaticAnalysisVersion);
		Writer.WriteAttribute("run_id");
		WriteUint64(Writer, SnapshotData.m_RunId);
		Writer.WriteAttribute("run_uid");
		Writer.WriteStrValue(SnapshotData.m_RunUid.c_str());
		Writer.WriteAttribute("global_run_number");
		if(SnapshotData.m_HasGlobalRunNumber)
			WriteUint64(Writer, SnapshotData.m_GlobalRunNumber);
		else
			Writer.WriteNullValue();
		Writer.WriteAttribute("server_key");
		Writer.WriteStrValue(SnapshotData.m_Context.m_ServerKey.c_str());
		Writer.WriteAttribute("server_name");
		Writer.WriteStrValue(SnapshotData.m_Context.m_ServerName.c_str());
		Writer.WriteAttribute("community_id");
		Writer.WriteStrValue(SnapshotData.m_Context.m_CommunityId.c_str());
		Writer.WriteAttribute("map");
		Writer.WriteStrValue(SnapshotData.m_Context.m_Map.c_str());
		Writer.WriteAttribute("map_difficulty");
		Writer.WriteStrValue(SnapshotData.m_Context.m_MapDifficulty.c_str());
		Writer.WriteAttribute("map_stars");
		Writer.WriteIntValue(SnapshotData.m_Context.m_MapStars);
		Writer.WriteAttribute("tick_speed");
		Writer.WriteIntValue(SnapshotData.m_Context.m_TickSpeed);
		Writer.WriteAttribute("team_id");
		Writer.WriteIntValue(SnapshotData.m_Context.m_TeamId);
		Writer.WriteAttribute("state");
		Writer.WriteStrValue(AnalysisStateName(SnapshotData.m_State));
		Writer.WriteAttribute("result");
		Writer.WriteStrValue(RunResultName(SnapshotData.m_Result));
		Writer.WriteAttribute("run_started_tick");
		WriteInt64(Writer, SnapshotData.m_RunStartedTick);
		Writer.WriteAttribute("analysis_started_tick");
		WriteInt64(Writer, SnapshotData.m_AnalysisStartedTick);
		Writer.WriteAttribute("last_confirmed_tick");
		WriteInt64(Writer, SnapshotData.m_LastConfirmedTick);
		Writer.WriteAttribute("last_state_tick");
		WriteInt64(Writer, SnapshotData.m_LastStateTick);
		Writer.WriteAttribute("active_analysis_ticks");
		WriteInt64(Writer, SnapshotData.m_ActiveAnalysisTicks);
		Writer.WriteAttribute("end_tick");
		WriteInt64(Writer, SnapshotData.m_EndTick);
		Writer.WriteAttribute("authoritative_finish_time_millis");
		Writer.WriteIntValue(SnapshotData.m_AuthoritativeFinishTimeMillis);
		Writer.WriteAttribute("scoring_evidence_version");
		Writer.WriteIntValue(SnapshotData.m_ScoringEvidenceVersion);
		Writer.WriteAttribute("scoring_evidence_complete");
		Writer.WriteBoolValue(SnapshotData.m_ScoringEvidenceComplete);
		Writer.WriteAttribute("scoring_evidence_dropped");
		WriteUint64(Writer, SnapshotData.m_ScoringEvidenceDroppedEvents);
		Writer.WriteAttribute("saved_at");
		WriteInt64(Writer, SnapshotData.m_SavedAt);
		Writer.WriteAttribute("participants");
		Writer.BeginArray();
		for(const SParticipant &Participant : SnapshotData.m_vParticipants)
		{
			const EParticipantTerminalStatus TerminalStatus = Participant.m_Finalized &&
				Participant.m_TerminalStatus == EParticipantTerminalStatus::NOT_FINALIZED ?
				EParticipantTerminalStatus::UNKNOWN : Participant.m_TerminalStatus;
			Writer.BeginObject();
			Writer.WriteAttribute("id");
			WriteId(Writer, Participant.m_Id);
			Writer.WriteAttribute("name");
			Writer.WriteStrValue(Participant.m_Name.c_str());
			if(IsPersistableCosmeticSnapshot(Participant.m_Cosmetics))
			{
				Writer.WriteAttribute("cosmetics");
				WriteCosmeticSnapshot(Writer, Participant.m_Cosmetics);
			}
			Writer.WriteAttribute("first_start_tick");
			WriteInt64(Writer, Participant.m_FirstStartTick);
			Writer.WriteAttribute("engaged");
			Writer.WriteBoolValue(Participant.m_Engaged);
			Writer.WriteAttribute("engaged_tick");
			WriteInt64(Writer, Participant.m_EngagedTick);
			Writer.WriteAttribute("personal_analysis_start_tick");
			WriteInt64(Writer, Participant.m_PersonalAnalysisStartTick);
			Writer.WriteAttribute("personal_analysis_start_active_ticks");
			WriteInt64(Writer, Participant.m_PersonalAnalysisStartActiveTicks);
			Writer.WriteAttribute("late");
			Writer.WriteBoolValue(Participant.m_Late);
			Writer.WriteAttribute("present");
			Writer.WriteBoolValue(Participant.m_Present);
			Writer.WriteAttribute("finalized");
			Writer.WriteBoolValue(Participant.m_Finalized);
			Writer.WriteAttribute("terminal_status");
			Writer.WriteIntValue(static_cast<int>(TerminalStatus));
			Writer.WriteAttribute("participation_ticks");
			WriteInt64(Writer, Participant.m_ParticipationTicks);
			Writer.WriteAttribute("participation_ticks_known");
			Writer.WriteBoolValue(Participant.m_ParticipationTicksKnown);
			Writer.WriteAttribute("participation_start_tick");
			WriteInt64(Writer, Participant.m_ParticipationStartTick);
			Writer.WriteAttribute("participation_terminal_tick");
			WriteInt64(Writer, Participant.m_ParticipationTerminalTick);
			Writer.WriteAttribute("raw_stats");
			WriteStats(Writer, Participant.m_RawStats);
			Writer.WriteAttribute("phase4_score");
			WriteScoreSnapshot(Writer, Participant.m_Score);
			Writer.WriteAttribute("rescues_received");
			Writer.WriteIntValue(Participant.m_RescuesReceived);
			Writer.WriteAttribute("rescues_received_known");
			Writer.WriteBoolValue(Participant.m_RescuesReceivedKnown);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.WriteAttribute("raw_stats");
		WriteStats(Writer, SnapshotData.m_RawStats);
		Writer.WriteAttribute("score");
		WriteScoreSnapshot(Writer, SnapshotData.m_Score);
		Writer.WriteAttribute("events");
		Writer.BeginArray();
		for(const SImpactEvent &Event : SnapshotData.m_vEvents)
			WriteEvent(Writer, Event);
		Writer.EndArray();
		Writer.WriteAttribute("scoring_evidence");
		Writer.BeginArray();
		for(const SImpactScoringEvidence &Evidence : SnapshotData.m_vScoringEvidence)
			WriteScoringEvidence(Writer, Evidence);
		Writer.EndArray();
		Writer.WriteAttribute("pending_episodes");
		Writer.BeginArray();
		for(const SImpactPendingEpisode &Episode : SnapshotData.m_vPendingEpisodes)
		{
			Writer.BeginObject();
			Writer.WriteAttribute("episode_id");
			WriteUint64(Writer, Episode.m_EpisodeId);
			Writer.WriteAttribute("type");
			Writer.WriteStrValue(ImpactEventTypeName(Episode.m_Type));
			Writer.WriteAttribute("actor");
			WriteId(Writer, Episode.m_Actor);
			Writer.WriteAttribute("target");
			WriteId(Writer, Episode.m_Target);
			Writer.WriteAttribute("team_id");
			Writer.WriteIntValue(Episode.m_TeamId);
			Writer.WriteAttribute("start_tick");
			WriteInt64(Writer, Episode.m_StartTick);
			Writer.WriteAttribute("restore_tick");
			WriteInt64(Writer, Episode.m_RestoreTick);
			Writer.WriteAttribute("required_ticks");
			WriteInt64(Writer, Episode.m_RequiredTicks);
			Writer.WriteAttribute("state");
			Writer.WriteStrValue(ImpactEpisodeStateName(Episode.m_State));
			Writer.WriteAttribute("causal_confidence");
			Writer.WriteStrValue(CausalConfidenceName(Episode.m_CausalConfidence));
			Writer.WriteAttribute("harm_cause");
			Writer.WriteStrValue(HarmCauseName(Episode.m_HarmCause));
			Writer.WriteAttribute("harm_causal_confidence");
			Writer.WriteStrValue(CausalConfidenceName(Episode.m_HarmCausalConfidence));
			Writer.WriteAttribute("ownership_status");
			Writer.WriteStrValue(OwnershipStatusName(Episode.m_OwnershipStatus));
			Writer.WriteAttribute("target_alive");
			Writer.WriteBoolValue(Episode.m_TargetAlive);
			Writer.WriteAttribute("target_unfrozen");
			Writer.WriteBoolValue(Episode.m_TargetUnfrozen);
			Writer.WriteAttribute("hammer_evidence");
			Writer.WriteBoolValue(Episode.m_HammerEvidence);
			Writer.WriteAttribute("hook_evidence");
			Writer.WriteBoolValue(Episode.m_HookEvidence);
			Writer.WriteAttribute("updown_evidence");
			Writer.WriteBoolValue(Episode.m_UpDownEvidence);
			Writer.WriteAttribute("unfreeze_cause");
			Writer.WriteStrValue(UnfreezeCauseName(Episode.m_UnfreezeCause));
			Writer.WriteAttribute("recovery_need");
			Writer.WriteStrValue(RecoveryNeedName(Episode.m_RecoveryNeed));
			Writer.WriteAttribute("physical_outcome");
			Writer.WriteStrValue(PhysicalOutcomeName(Episode.m_PhysicalOutcome));
			Writer.WriteAttribute("physical_change_evidence");
			Writer.WriteBoolValue(Episode.m_PhysicalChangeEvidence);
			Writer.WriteAttribute("decisive_physical_evidence");
			Writer.WriteBoolValue(Episode.m_DecisivePhysicalEvidence);
			Writer.WriteAttribute("initial_recovery_need");
			Writer.WriteStrValue(RecoveryNeedName(Episode.m_InitialRecoveryNeed));
			Writer.WriteAttribute("related_recovery_episode_id");
			WriteUint64(Writer, Episode.m_RelatedRecoveryEpisodeId);
			Writer.WriteAttribute("related_fall_episode_id");
			WriteUint64(Writer, Episode.m_RelatedFallEpisodeId);
			Writer.WriteAttribute("fall_already_confirmed");
			Writer.WriteBoolValue(Episode.m_FallAlreadyConfirmed);
			Writer.WriteAttribute("related_fall_already_confirmed");
			Writer.WriteBoolValue(Episode.m_RelatedFallAlreadyConfirmed);
			Writer.WriteAttribute("recovery_outcome_confirmed");
			Writer.WriteBoolValue(Episode.m_RecoveryOutcomeConfirmed);
			Writer.WriteAttribute("clutch_candidate");
			Writer.WriteBoolValue(Episode.m_ClutchCandidate);
			Writer.WriteAttribute("clutch_armed_tick");
			WriteInt64(Writer, Episode.m_ClutchArmedTick);
			Writer.WriteAttribute("clutch_episode_id");
			WriteUint64(Writer, Episode.m_ClutchEpisodeId);
			Writer.WriteAttribute("origin_external_cause_evidence");
			Writer.WriteBoolValue(Episode.m_OriginExternalCauseEvidence);
			Writer.WriteAttribute("origin_self_origin_evidence");
			Writer.WriteBoolValue(Episode.m_OriginSelfOriginEvidence);
			Writer.WriteAttribute("hook_first_tick");
			WriteInt64(Writer, Episode.m_HookFirstTick);
			Writer.WriteAttribute("hook_last_tick");
			WriteInt64(Writer, Episode.m_HookLastTick);
			Writer.WriteAttribute("hook_control_continuity");
			Writer.WriteBoolValue(Episode.m_HookControlContinuity);
			Writer.WriteAttribute("confirmed_recovery_start_tick");
			WriteInt64(Writer, Episode.m_ConfirmedRecoveryStartTick);
			Writer.WriteAttribute("target_was_eligible");
			Writer.WriteBoolValue(Episode.m_TargetWasEligible);
			Writer.WriteAttribute("before_context");
			Writer.WriteStrValue(Episode.m_BeforeContext.c_str());
			Writer.WriteAttribute("after_context");
			Writer.WriteStrValue(Episode.m_AfterContext.c_str());
			Writer.WriteAttribute("debug_reason");
			Writer.WriteStrValue(Episode.m_DebugReason.c_str());
			Writer.WriteAttribute("helpers");
			Writer.BeginArray();
			for(const SParticipantId &Helper : Episode.m_vHelpers)
				WriteId(Writer, Helper);
			Writer.EndArray();
			Writer.WriteAttribute("causal_recovery_proof");
			WriteCausalRecoveryProof(Writer, Episode.m_CausalRecoveryProof);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.WriteAttribute("forensic");
		Writer.BeginObject();
		Writer.WriteAttribute("version");
		Writer.WriteIntValue(SnapshotData.m_ForensicVersion);
		Writer.WriteAttribute("enabled");
		Writer.WriteBoolValue(SnapshotData.m_ForensicTraceEnabled);
		Writer.WriteAttribute("complete_from_start");
		Writer.WriteBoolValue(SnapshotData.m_ForensicTraceComplete);
		Writer.WriteAttribute("trace_truncated");
		Writer.WriteBoolValue(SnapshotData.m_ForensicTraceTruncated);
		Writer.WriteAttribute("dropped_events");
		WriteUint64(Writer, SnapshotData.m_ForensicDroppedEvents);
		Writer.WriteAttribute("events");
		Writer.BeginArray();
		for(const SImpactForensicEvent &Event : SnapshotData.m_vForensicEvents)
			WriteForensicEvent(Writer, Event);
		Writer.EndArray();
		Writer.EndObject();
		Writer.EndObject();
	}

	static const json_value *Field(const json_value *pObject, const char *pName)
	{
		return pObject && pObject->type == json_object ? json_object_get(pObject, pName) : nullptr;
	}

	static bool ReadInt(const json_value *pObject, const char *pName, int *pValue, bool Required = true)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type != json_integer)
			return false;
		*pValue = json_int_get(pValueJson);
		return true;
	}

	static bool ReadBool(const json_value *pObject, const char *pName, bool *pValue, bool Required = true)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type != json_boolean)
			return false;
		*pValue = json_boolean_get(pValueJson) != 0;
		return true;
	}

	static bool ReadString(const json_value *pObject, const char *pName, std::string *pValue, bool Required = true, size_t MaxLength = MAX_CONTEXT_LENGTH)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type != json_string)
			return false;
		*pValue = std::string(json_string_get(pValueJson)).substr(0, MaxLength);
		return true;
	}

	template<typename T>
	static bool ReadSignedString(const json_value *pObject, const char *pName, T *pValue, bool Required = true)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type == json_integer)
		{
			*pValue = static_cast<T>(pValueJson->u.integer);
			return true;
		}
		if(pValueJson->type != json_string)
			return false;
		const char *pText = json_string_get(pValueJson);
		char *pEnd = nullptr;
		errno = 0;
		const long long Value = std::strtoll(pText, &pEnd, 10);
		if(errno != 0 || pEnd == pText || *pEnd != '\0')
			return false;
		*pValue = static_cast<T>(Value);
		return true;
	}

	static bool ReadUint64(const json_value *pObject, const char *pName, uint64_t *pValue, bool Required = true)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type == json_integer)
		{
			if(pValueJson->u.integer < 0)
				return false;
			*pValue = static_cast<uint64_t>(pValueJson->u.integer);
			return true;
		}
		if(pValueJson->type != json_string)
			return false;
		const char *pText = json_string_get(pValueJson);
		char *pEnd = nullptr;
		errno = 0;
		const unsigned long long Value = std::strtoull(pText, &pEnd, 10);
		if(errno != 0 || pEnd == pText || *pEnd != '\0')
			return false;
		*pValue = static_cast<uint64_t>(Value);
		return true;
	}

	static bool ReadDouble(const json_value *pObject, const char *pName, double *pValue, bool Required = true)
	{
		const json_value *pValueJson = Field(pObject, pName);
		if(pValueJson == nullptr || pValueJson->type == json_none)
			return !Required;
		if(pValueJson->type == json_double)
		{
			*pValue = pValueJson->u.dbl;
			return true;
		}
		if(pValueJson->type != json_string)
			return false;
		char *pEnd = nullptr;
		errno = 0;
		*pValue = std::strtod(json_string_get(pValueJson), &pEnd);
		return errno == 0 && pEnd != json_string_get(pValueJson) && *pEnd == '\0';
	}

	static bool ReadScoreSnapshot(const json_value *pScoreJson, SScoreSnapshot *pScore)
	{
		if(pScore == nullptr)
			return false;
		*pScore = {};
		if(pScoreJson == nullptr || pScoreJson->type == json_none || pScoreJson->type == json_null)
			return true;
		if(pScoreJson->type != json_object)
		{
			pScore->m_Frozen = true;
			return true;
		}

		const json_value *pFrozen = Field(pScoreJson, "frozen");
		const bool HasFrozen = pFrozen != nullptr && pFrozen->type != json_none && pFrozen->type != json_null;
		bool Frozen = false;
		const bool FrozenValid = !HasFrozen || ReadBool(pScoreJson, "frozen", &Frozen);
		SScoreSnapshot Parsed;
		const bool AvailableValid = ReadBool(pScoreJson, "available", &Parsed.m_Available);
		const bool FieldsValid = ReadDouble(pScoreJson, "rating", &Parsed.m_Rating) &&
			ReadDouble(pScoreJson, "impact", &Parsed.m_Impact) &&
			ReadDouble(pScoreJson, "stability", &Parsed.m_Stability, false) &&
			ReadDouble(pScoreJson, "consistency", &Parsed.m_Consistency) &&
			ReadInt(pScoreJson, "tier", &Parsed.m_Tier) &&
			ReadInt(pScoreJson, "rating_version", &Parsed.m_RatingVersion) &&
			ReadInt(pScoreJson, "scoring_version", &Parsed.m_ScoringVersion, false);
		Parsed.m_Frozen = HasFrozen && FrozenValid ? Frozen : Parsed.m_Available;

		const bool Finite = std::isfinite(Parsed.m_Rating) && std::isfinite(Parsed.m_Impact) &&
			std::isfinite(Parsed.m_Stability) && std::isfinite(Parsed.m_Consistency);
		const json_value *pStability = Field(pScoreJson, "stability");
		const json_value *pScoringVersion = Field(pScoreJson, "scoring_version");
		const bool AvailableSnapshotValid = !Parsed.m_Available ||
			(Parsed.m_Frozen && Parsed.m_Rating >= 0.0 && pStability != nullptr && pStability->type != json_null &&
			pScoringVersion != nullptr && pScoringVersion->type != json_null && Parsed.m_Stability >= 0.0 && Parsed.m_Stability <= 100.0 &&
			Parsed.m_RatingVersion > 0 && Parsed.m_ScoringVersion > IMPACT_SCORING_VERSION_UNSET);
		if(!FrozenValid || !AvailableValid || !FieldsValid || !Finite || !AvailableSnapshotValid)
		{
			// Score corruption is isolated from the containing run. Keep the rest of
			// history loadable, but freeze an explicitly present malformed score so
			// later lifecycle activity cannot silently reconstruct it.
			pScore->m_Frozen = true;
			return true;
		}
		*pScore = Parsed;
		return true;
	}

	static bool ReadIdObject(const json_value *pIdJson, SParticipantId *pId)
	{
		if(pIdJson == nullptr || pIdJson->type != json_object)
			return false;
		int ClientId = -1;
		int Generation = 0;
		bool Dummy = false;
		if(!ReadInt(pIdJson, "client_id", &ClientId) || !ReadInt(pIdJson, "slot_generation", &Generation) || !ReadBool(pIdJson, "dummy", &Dummy))
			return false;
		pId->m_ClientId = ClientId;
		pId->m_SlotGeneration = Generation > 0 ? static_cast<uint32_t>(Generation) : 0;
		pId->m_Dummy = Dummy;
		return pId->IsValid() || (ClientId == -1 && Generation == 0 && !Dummy);
	}

	static bool ReadId(const json_value *pObject, const char *pName, SParticipantId *pId)
	{
		return ReadIdObject(Field(pObject, pName), pId);
	}

	static bool ReadStats(const json_value *pObject, const char *pName, SRawStats *pStats)
	{
		const json_value *pStatsJson = Field(pObject, pName);
		if(pStatsJson == nullptr || pStatsJson->type != json_object)
			return false;
		return ReadInt(pStatsJson, "confirmed_events", &pStats->m_ConfirmedEvents, false) &&
			ReadInt(pStatsJson, "falls", &pStats->m_Falls, false) &&
			ReadInt(pStatsJson, "wipes", &pStats->m_Wipes, false) &&
			ReadInt(pStatsJson, "saves", &pStats->m_Saves, false) &&
			ReadInt(pStatsJson, "useful_saves", &pStats->m_UsefulSaves, false) &&
			ReadInt(pStatsJson, "meaningful_skips", &pStats->m_MeaningfulSkips, false) &&
			ReadInt(pStatsJson, "back_recoveries", &pStats->m_BackRecoveries, false) &&
			ReadInt(pStatsJson, "harmful_events", &pStats->m_HarmfulEvents, false) &&
			ReadInt(pStatsJson, "positive_events", &pStats->m_PositiveEvents, false) &&
			ReadInt(pStatsJson, "aleds", &pStats->m_Aleds, false) &&
			ReadInt(pStatsJson, "clutches", &pStats->m_Clutches, false) &&
			// Legacy history may still contain this field. Read it optionally for
			// compatibility, but current code never applies or writes it.
			ReadInt(pStatsJson, "updowns", &pStats->m_UpDowns, false) &&
			ReadInt(pStatsJson, "missed_catches", &pStats->m_MissedCatches, false) &&
			ReadInt(pStatsJson, "team_mistakes", &pStats->m_TeamMistakes, false) &&
			ReadInt(pStatsJson, "prevented_progress_loss", &pStats->m_PreventedProgressLoss, false) &&
			ReadInt(pStatsJson, "recovery_difficulty_increases", &pStats->m_RecoveryDifficultyIncreases, false);
	}

	static bool ReadForensicEvent(const json_value *pEventJson, int ForensicVersion, SImpactForensicEvent *pEvent)
	{
		if(pEventJson == nullptr || pEventJson->type != json_object || pEvent == nullptr)
			return false;
		int Type = -1;
		int Reason = -1;
		int RunStateBefore = -1;
		int RunStateAfter = -1;
		int EpisodeStateBefore = -1;
		int EpisodeStateAfter = -1;
		int PhysicalOutcome = -1;
		const int MaxForensicType = ForensicVersion >= IMPACT_FORENSIC_VERSION ?
			static_cast<int>(EImpactForensicType::ENGAGED_FINALIZED) : static_cast<int>(EImpactForensicType::CLUTCH_ROSTER_EVALUATED);
		if(!ReadUint64(pEventJson, "seq", &pEvent->m_Sequence) || pEvent->m_Sequence == 0 ||
			!ReadUint64(pEventJson, "run_id", &pEvent->m_RunId) ||
			!ReadUint64(pEventJson, "event_id", &pEvent->m_EventId) ||
			!ReadInt(pEventJson, "type", &Type) || Type < 0 || Type > MaxForensicType ||
			!ReadSignedString(pEventJson, "tick", &pEvent->m_Tick) ||
			!ReadInt(pEventJson, "team_id", &pEvent->m_TeamId) ||
			!ReadId(pEventJson, "target", &pEvent->m_Target) ||
			!ReadId(pEventJson, "actor", &pEvent->m_Actor) ||
			!ReadUint64(pEventJson, "problem_id", &pEvent->m_ProblemEpisodeId) ||
			!ReadUint64(pEventJson, "fall_id", &pEvent->m_FallEpisodeId) ||
			!ReadUint64(pEventJson, "save_id", &pEvent->m_SaveCandidateId) ||
			!ReadUint64(pEventJson, "clutch_id", &pEvent->m_ClutchEpisodeId) ||
			!ReadUint64(pEventJson, "interaction_id", &pEvent->m_ActorInteractionId) ||
			!ReadInt(pEventJson, "reason", &Reason) || Reason < 0 || Reason > static_cast<int>(EImpactForensicReason::EXISTING_GENERIC_REJECT) ||
			!ReadString(pEventJson, "detail", &pEvent->m_ReasonDetail, true, MAX_FORENSIC_DETAIL_LENGTH) ||
			!ReadInt(pEventJson, "run_state_before", &RunStateBefore) || RunStateBefore < 0 || RunStateBefore > static_cast<int>(EAnalysisState::FINALIZED) ||
			!ReadInt(pEventJson, "run_state_after", &RunStateAfter) || RunStateAfter < 0 || RunStateAfter > static_cast<int>(EAnalysisState::FINALIZED) ||
			!ReadInt(pEventJson, "episode_state_before", &EpisodeStateBefore) || EpisodeStateBefore < 0 || EpisodeStateBefore > static_cast<int>(EImpactEpisodeState::CONFIRMED_RECOVERY) ||
			!ReadInt(pEventJson, "episode_state_after", &EpisodeStateAfter) || EpisodeStateAfter < 0 || EpisodeStateAfter > static_cast<int>(EImpactEpisodeState::CONFIRMED_RECOVERY) ||
			!ReadInt(pEventJson, "physical_outcome", &PhysicalOutcome) || PhysicalOutcome < 0 || PhysicalOutcome > static_cast<int>(EImpactPhysicalOutcome::HARMFUL) ||
			!ReadInt(pEventJson, "active_count", &pEvent->m_ActiveParticipantCount) ||
			!ReadInt(pEventJson, "present_count", &pEvent->m_PresentParticipantCount) ||
			!ReadInt(pEventJson, "historical_count", &pEvent->m_HistoricalParticipantCount) ||
			!ReadBool(pEventJson, "target_snapshot", &pEvent->m_TargetSnapshotAvailable) ||
			!ReadBool(pEventJson, "actor_snapshot", &pEvent->m_ActorSnapshotAvailable) ||
			!ReadBool(pEventJson, "target_predicted", &pEvent->m_TargetPredictedStateAvailable) ||
			!ReadBool(pEventJson, "interaction_observable", &pEvent->m_ActorTargetInteractionObservable) ||
			!ReadBool(pEventJson, "physical_change_evidence", &pEvent->m_PhysicalChangeEvidence) ||
			!ReadBool(pEventJson, "decisive_physical_evidence", &pEvent->m_DecisivePhysicalEvidence) ||
			!ReadBool(pEventJson, "decision_eligible", &pEvent->m_DecisionEligible) ||
			!ReadBool(pEventJson, "decision_accepted", &pEvent->m_DecisionAccepted) ||
			!ReadBool(pEventJson, "autonomous_evaluated", &pEvent->m_AutonomousEvidenceEvaluated) ||
			!ReadBool(pEventJson, "autonomous_known", &pEvent->m_AutonomousEvidenceKnown) ||
			!ReadBool(pEventJson, "autonomous_possible", &pEvent->m_AutonomousRecoveryPossible) ||
			!ReadBool(pEventJson, "self_safe", &pEvent->m_SelfSafeProven) ||
			!ReadSignedString(pEventJson, "self_safe_tick", &pEvent->m_SelfSafeEvidenceTick) ||
			!ReadSignedString(pEventJson, "restoration_tick", &pEvent->m_RestorationTick) ||
			!ReadSignedString(pEventJson, "confirmation_start_tick", &pEvent->m_ConfirmationStartTick) ||
			!ReadSignedString(pEventJson, "confirmation_deadline_tick", &pEvent->m_ConfirmationDeadlineTick) ||
			!ReadSignedString(pEventJson, "confirmed_ticks", &pEvent->m_ConfirmedTicks) ||
			!ReadSignedString(pEventJson, "required_ticks", &pEvent->m_RequiredTicks) ||
			!ReadStats(pEventJson, "raw_delta", &pEvent->m_RawDelta))
			return false;
		pEvent->m_Type = static_cast<EImpactForensicType>(Type);
		pEvent->m_Reason = static_cast<EImpactForensicReason>(Reason);
		pEvent->m_RunStateBefore = static_cast<EAnalysisState>(RunStateBefore);
		pEvent->m_RunStateAfter = static_cast<EAnalysisState>(RunStateAfter);
		pEvent->m_EpisodeStateBefore = static_cast<EImpactEpisodeState>(EpisodeStateBefore);
		pEvent->m_EpisodeStateAfter = static_cast<EImpactEpisodeState>(EpisodeStateAfter);
		pEvent->m_PhysicalOutcome = static_cast<EImpactPhysicalOutcome>(PhysicalOutcome);

		const json_value *pRoster = Field(pEventJson, "functional_roster");
		if(pRoster == nullptr || pRoster->type != json_array || json_array_length(pRoster) > static_cast<int>(MAX_PARTICIPANTS))
			return false;
		for(int i = 0; i < json_array_length(pRoster); ++i)
		{
			const json_value *pEntryJson = json_array_get(pRoster, i);
			SImpactFunctionalRosterEntry Entry;
			int Classification = -1;
			if(pEntryJson == nullptr || pEntryJson->type != json_object ||
				!ReadId(pEntryJson, "id", &Entry.m_Participant) ||
				!ReadInt(pEntryJson, "class", &Classification) || Classification < 0 || Classification > static_cast<int>(EImpactFunctionalClass::EXCLUDED) ||
				!ReadBool(pEntryJson, "roster_member", &Entry.m_RosterMember, false) ||
				!ReadBool(pEntryJson, "engaged", &Entry.m_Engaged, false) ||
				!ReadBool(pEntryJson, "finalized", &Entry.m_Finalized, false) ||
				!ReadString(pEntryJson, "reason", &Entry.m_Reason, true, MAX_EVENT_REASON_LENGTH) ||
				!ReadBool(pEntryJson, "alive_known", &Entry.m_AliveKnown) ||
				!ReadBool(pEntryJson, "alive", &Entry.m_Alive) ||
				!ReadBool(pEntryJson, "frozen", &Entry.m_Frozen) ||
				!ReadBool(pEntryJson, "autonomous_known", &Entry.m_AutonomousRecoveryKnown) ||
				!ReadBool(pEntryJson, "autonomous_possible", &Entry.m_AutonomousRecoveryPossible))
				return false;
			Entry.m_Classification = static_cast<EImpactFunctionalClass>(Classification);
			pEvent->m_vFunctionalRoster.push_back(std::move(Entry));
		}
		return true;
	}

	static bool ReadTags(const json_value *pObject, const char *pName, std::vector<std::string> *pTags)
	{
		const json_value *pTagsJson = Field(pObject, pName);
		if(pTagsJson == nullptr || pTagsJson->type == json_none)
			return true;
		if(pTagsJson->type != json_array || json_array_length(pTagsJson) > static_cast<int>(MAX_EVENT_TAGS))
			return false;
		pTags->clear();
		pTags->reserve(json_array_length(pTagsJson));
		for(int i = 0; i < json_array_length(pTagsJson); ++i)
		{
			const json_value *pTag = json_array_get(pTagsJson, i);
			if(pTag == nullptr || pTag->type != json_string)
				return false;
			pTags->push_back(std::string(json_string_get(pTag)).substr(0, MAX_EVENT_TAG_LENGTH));
		}
		return true;
	}

	static bool ReadHelpers(const json_value *pObject, const char *pName, std::vector<SParticipantId> *pHelpers)
	{
		const json_value *pHelpersJson = Field(pObject, pName);
		if(pHelpersJson == nullptr || pHelpersJson->type == json_none)
			return true;
		if(pHelpersJson->type != json_array || json_array_length(pHelpersJson) > static_cast<int>(MAX_EVENT_HELPERS))
			return false;
		pHelpers->clear();
		pHelpers->reserve(json_array_length(pHelpersJson));
		for(int i = 0; i < json_array_length(pHelpersJson); ++i)
		{
			SParticipantId Helper;
			if(!ReadIdObject(json_array_get(pHelpersJson, i), &Helper) || !Helper.IsValid())
				return false;
			pHelpers->push_back(Helper);
		}
		return true;
	}

	static bool ReadCausalRecoveryProof(const json_value *pEpisodeJson, SImpactCausalRecoveryProof *pProof)
	{
		if(pProof == nullptr)
			return false;
		const json_value *pProofJson = Field(pEpisodeJson, "causal_recovery_proof");
		if(pProofJson == nullptr || pProofJson->type == json_none)
			return true;
		if(pProofJson->type != json_object)
			return false;
		if(!ReadBool(pProofJson, "valid", &pProof->m_Valid, false))
			return false;
		if(!pProof->m_Valid)
		{
			*pProof = {};
			return true;
		}
		std::string PreActionOutcome;
		std::string PostActionOutcome;
		std::string OwnershipStatus;
		std::string Confidence;
		if(!ReadUint64(pProofJson, "proof_id", &pProof->m_ProofId) ||
			!ReadUint64(pProofJson, "fall_episode_id", &pProof->m_FallEpisodeId) ||
			!ReadUint64(pProofJson, "recovery_episode_id", &pProof->m_RecoveryEpisodeId) ||
			!ReadId(pProofJson, "target", &pProof->m_Target) ||
			!ReadId(pProofJson, "decisive_actor", &pProof->m_DecisiveActor) ||
			!ReadInt(pProofJson, "team_id", &pProof->m_TeamId, false) ||
			!ReadSignedString(pProofJson, "harm_start_tick", &pProof->m_HarmStartTick) ||
			!ReadSignedString(pProofJson, "action_tick", &pProof->m_ActionTick) ||
			!ReadSignedString(pProofJson, "first_proven_tick", &pProof->m_FirstProvenTick) ||
			!ReadSignedString(pProofJson, "restore_tick", &pProof->m_RestoreTick) ||
			!ReadSignedString(pProofJson, "hook_first_tick", &pProof->m_HookFirstTick) ||
			!ReadSignedString(pProofJson, "hook_last_tick", &pProof->m_HookLastTick) ||
			!ReadString(pProofJson, "pre_action_outcome", &PreActionOutcome, false) ||
			!ReadString(pProofJson, "post_action_outcome", &PostActionOutcome, false) ||
			!ReadBool(pProofJson, "target_already_safe_before_action", &pProof->m_TargetAlreadySafeBeforeAction) ||
			!ReadBool(pProofJson, "target_specific_evidence", &pProof->m_TargetSpecificEvidence) ||
			!ReadBool(pProofJson, "physical_change_evidence", &pProof->m_PhysicalChangeEvidence) ||
			!ReadBool(pProofJson, "safe_outcome_evidence", &pProof->m_SafeOutcomeEvidence) ||
			!ReadBool(pProofJson, "hook_evidence", &pProof->m_HookEvidence) ||
			!ReadBool(pProofJson, "hammer_evidence", &pProof->m_HammerEvidence) ||
			!ReadString(pProofJson, "ownership_status", &OwnershipStatus, false) ||
			!ReadString(pProofJson, "confidence", &Confidence, false) ||
			!ReadString(pProofJson, "reason", &pProof->m_Reason, false, MAX_EVENT_REASON_LENGTH) ||
			!ReadHelpers(pProofJson, "contributors", &pProof->m_vContributors))
			return false;
		pProof->m_PreActionOutcome = PhysicalOutcomeFromName(PreActionOutcome.c_str());
		pProof->m_PostActionOutcome = PhysicalOutcomeFromName(PostActionOutcome.c_str());
		pProof->m_OwnershipStatus = OwnershipStatusFromName(OwnershipStatus.c_str());
		pProof->m_Confidence = CausalConfidenceFromName(Confidence.c_str());
		return pProof->m_Target.IsValid() && pProof->m_ProofId != 0;
	}

	static bool ReadEvent(const json_value *pEventJson, SImpactEvent *pEvent)
	{
		if(pEventJson == nullptr || pEventJson->type != json_object)
			return false;
		uint64_t EventId = 0;
		std::string Type;
		std::string EpisodeState;
		std::string CausalConfidence;
		std::string OwnershipStatus;
		if(!ReadUint64(pEventJson, "event_id", &EventId) || !ReadString(pEventJson, "type", &Type) || !ReadId(pEventJson, "actor", &pEvent->m_Actor) || !ReadId(pEventJson, "target", &pEvent->m_Target) || !ReadInt(pEventJson, "team_id", &pEvent->m_TeamId, false))
			return false;
		pEvent->m_EventId = EventId;
		pEvent->m_Type = ImpactEventTypeFromName(Type.c_str());
		if(!ReadString(pEventJson, "actor_name", &pEvent->m_ActorName, false) ||
			!ReadSignedString(pEventJson, "start_tick", &pEvent->m_StartTick) ||
			!ReadSignedString(pEventJson, "resolve_tick", &pEvent->m_ResolveTick) ||
			!ReadBool(pEventJson, "confirmed", &pEvent->m_Confirmed) ||
			!ReadBool(pEventJson, "present", &pEvent->m_Present) ||
			!ReadBool(pEventJson, "anti_farm", &pEvent->m_AntiFarm) ||
			!ReadInt(pEventJson, "team_utility", &pEvent->m_TeamUtility) ||
			!ReadInt(pEventJson, "difficulty", &pEvent->m_Difficulty) ||
			!ReadInt(pEventJson, "traversal_saved", &pEvent->m_TraversalSaved) ||
			!ReadInt(pEventJson, "recovery_saved", &pEvent->m_RecoverySaved) ||
			!ReadInt(pEventJson, "run_saving_value", &pEvent->m_RunSavingValue) ||
			!ReadInt(pEventJson, "execution_quality", &pEvent->m_ExecutionQuality) ||
			!ReadInt(pEventJson, "positive_contribution", &pEvent->m_PositiveContribution) ||
			!ReadInt(pEventJson, "harmful_contribution", &pEvent->m_HarmfulContribution) ||
			!ReadString(pEventJson, "causal_reason", &pEvent->m_CausalReason, false, MAX_EVENT_REASON_LENGTH) ||
			!ReadString(pEventJson, "before_context", &pEvent->m_BeforeContext, false) ||
			!ReadString(pEventJson, "after_context", &pEvent->m_AfterContext, false) ||
			!ReadBool(pEventJson, "applied_exactly_once", &pEvent->m_AppliedExactlyOnce, false) ||
			!ReadTags(pEventJson, "tags", &pEvent->m_vTags) ||
			!ReadHelpers(pEventJson, "helpers", &pEvent->m_vHelpers) ||
			!ReadStats(pEventJson, "raw_delta", &pEvent->m_RawDelta))
			return false;
		if(!ReadString(pEventJson, "episode_state", &EpisodeState, false) ||
			!ReadString(pEventJson, "causal_confidence", &CausalConfidence, false) ||
			!ReadString(pEventJson, "ownership_status", &OwnershipStatus, false))
			return false;
		pEvent->m_EpisodeState = ImpactEpisodeStateFromName(EpisodeState.c_str());
		pEvent->m_CausalConfidence = CausalConfidenceFromName(CausalConfidence.c_str());
		pEvent->m_OwnershipStatus = OwnershipStatusFromName(OwnershipStatus.c_str());
		return true;
	}

	static bool ReadScoringEvidence(const json_value *pEvidenceJson, SImpactScoringEvidence *pEvidence)
	{
		if(pEvidenceJson == nullptr || pEvidenceJson->type != json_object || pEvidence == nullptr)
			return false;
		std::string Type;
		if(!ReadUint64(pEvidenceJson, "run_id", &pEvidence->m_RunId) ||
			!ReadUint64(pEvidenceJson, "event_id", &pEvidence->m_EventId) ||
			!ReadString(pEvidenceJson, "type", &Type) ||
			!ReadSignedString(pEvidenceJson, "tick", &pEvidence->m_Tick) ||
			!ReadId(pEvidenceJson, "actor", &pEvidence->m_Actor) ||
			!ReadId(pEvidenceJson, "target", &pEvidence->m_Target) ||
			!ReadUint64(pEvidenceJson, "problem_id", &pEvidence->m_ProblemEpisodeId) ||
			!ReadUint64(pEvidenceJson, "fall_id", &pEvidence->m_FallEpisodeId) ||
			!ReadUint64(pEvidenceJson, "rescue_id", &pEvidence->m_RescueEpisodeId) ||
			!ReadUint64(pEvidenceJson, "clutch_id", &pEvidence->m_ClutchEpisodeId) ||
			!ReadUint64(pEvidenceJson, "interaction_id", &pEvidence->m_ActorInteractionId) ||
			!ReadUint64(pEvidenceJson, "save_event_id", &pEvidence->m_LinkedSaveEventId) ||
			!ReadBool(pEvidenceJson, "is_save", &pEvidence->m_IsCanonicalSave) ||
			!ReadBool(pEvidenceJson, "clutch_confirmed", &pEvidence->m_ClutchConfirmed) ||
			!ReadBool(pEvidenceJson, "aled_confirmed", &pEvidence->m_AledConfirmed))
			return false;
		pEvidence->m_Type = ImpactEventTypeFromName(Type.c_str());
		return pEvidence->m_RunId != 0 && pEvidence->m_EventId != 0 && pEvidence->m_Tick >= -1 &&
			(pEvidence->m_Type == EImpactEventType::SAVE_CONFIRMED || pEvidence->m_Type == EImpactEventType::FALL_CONFIRMED || pEvidence->m_Type == EImpactEventType::ALED_CONFIRMED) &&
			(!pEvidence->m_IsCanonicalSave || pEvidence->m_LinkedSaveEventId == pEvidence->m_EventId) &&
			(pEvidence->m_IsCanonicalSave || pEvidence->m_LinkedSaveEventId != pEvidence->m_EventId) &&
			(!pEvidence->m_ClutchConfirmed || pEvidence->m_IsCanonicalSave) &&
			(!pEvidence->m_AledConfirmed || pEvidence->m_Type == EImpactEventType::ALED_CONFIRMED);
	}

	static bool ReadPendingEpisode(const json_value *pEpisodeJson, SImpactPendingEpisode *pEpisode)
	{
		if(pEpisodeJson == nullptr || pEpisodeJson->type != json_object || pEpisode == nullptr)
			return false;
		uint64_t EpisodeId = 0;
		std::string Type;
		std::string State;
		std::string CausalConfidence;
		std::string HarmCause;
		std::string HarmCausalConfidence;
		std::string OwnershipStatus;
		std::string UnfreezeCause;
		std::string RecoveryNeed;
		std::string PhysicalOutcome;
		std::string InitialRecoveryNeed;
		if(!ReadUint64(pEpisodeJson, "episode_id", &EpisodeId) ||
			!ReadString(pEpisodeJson, "type", &Type) ||
			!ReadId(pEpisodeJson, "actor", &pEpisode->m_Actor) ||
			!ReadId(pEpisodeJson, "target", &pEpisode->m_Target) ||
			!ReadInt(pEpisodeJson, "team_id", &pEpisode->m_TeamId, false) ||
			!ReadSignedString(pEpisodeJson, "start_tick", &pEpisode->m_StartTick) ||
			!ReadSignedString(pEpisodeJson, "restore_tick", &pEpisode->m_RestoreTick) ||
			!ReadSignedString(pEpisodeJson, "required_ticks", &pEpisode->m_RequiredTicks) ||
			!ReadString(pEpisodeJson, "state", &State) ||
			!ReadString(pEpisodeJson, "causal_confidence", &CausalConfidence) ||
			!ReadString(pEpisodeJson, "harm_cause", &HarmCause, false) ||
			!ReadString(pEpisodeJson, "harm_causal_confidence", &HarmCausalConfidence, false) ||
			!ReadString(pEpisodeJson, "ownership_status", &OwnershipStatus) ||
			!ReadBool(pEpisodeJson, "target_alive", &pEpisode->m_TargetAlive) ||
			!ReadBool(pEpisodeJson, "target_unfrozen", &pEpisode->m_TargetUnfrozen) ||
			!ReadBool(pEpisodeJson, "hammer_evidence", &pEpisode->m_HammerEvidence) ||
			!ReadBool(pEpisodeJson, "hook_evidence", &pEpisode->m_HookEvidence) ||
			!ReadBool(pEpisodeJson, "updown_evidence", &pEpisode->m_UpDownEvidence, false) ||
			!ReadString(pEpisodeJson, "unfreeze_cause", &UnfreezeCause, false) ||
			!ReadString(pEpisodeJson, "recovery_need", &RecoveryNeed, false) ||
			!ReadString(pEpisodeJson, "physical_outcome", &PhysicalOutcome, false) ||
			!ReadBool(pEpisodeJson, "physical_change_evidence", &pEpisode->m_PhysicalChangeEvidence, false) ||
			!ReadBool(pEpisodeJson, "decisive_physical_evidence", &pEpisode->m_DecisivePhysicalEvidence, false) ||
			!ReadString(pEpisodeJson, "initial_recovery_need", &InitialRecoveryNeed, false) ||
			!ReadUint64(pEpisodeJson, "related_recovery_episode_id", &pEpisode->m_RelatedRecoveryEpisodeId, false) ||
			!ReadUint64(pEpisodeJson, "related_fall_episode_id", &pEpisode->m_RelatedFallEpisodeId, false) ||
			!ReadBool(pEpisodeJson, "fall_already_confirmed", &pEpisode->m_FallAlreadyConfirmed, false) ||
			!ReadBool(pEpisodeJson, "related_fall_already_confirmed", &pEpisode->m_RelatedFallAlreadyConfirmed, false) ||
			!ReadBool(pEpisodeJson, "recovery_outcome_confirmed", &pEpisode->m_RecoveryOutcomeConfirmed, false) ||
			!ReadBool(pEpisodeJson, "clutch_candidate", &pEpisode->m_ClutchCandidate, false) ||
			!ReadSignedString(pEpisodeJson, "clutch_armed_tick", &pEpisode->m_ClutchArmedTick, false) ||
			!ReadUint64(pEpisodeJson, "clutch_episode_id", &pEpisode->m_ClutchEpisodeId, false) ||
			!ReadBool(pEpisodeJson, "origin_external_cause_evidence", &pEpisode->m_OriginExternalCauseEvidence, false) ||
			!ReadBool(pEpisodeJson, "origin_self_origin_evidence", &pEpisode->m_OriginSelfOriginEvidence, false) ||
			!ReadSignedString(pEpisodeJson, "hook_first_tick", &pEpisode->m_HookFirstTick, false) ||
			!ReadSignedString(pEpisodeJson, "hook_last_tick", &pEpisode->m_HookLastTick, false) ||
			!ReadBool(pEpisodeJson, "hook_control_continuity", &pEpisode->m_HookControlContinuity, false) ||
			!ReadSignedString(pEpisodeJson, "confirmed_recovery_start_tick", &pEpisode->m_ConfirmedRecoveryStartTick, false) ||
			!ReadBool(pEpisodeJson, "target_was_eligible", &pEpisode->m_TargetWasEligible) ||
			!ReadString(pEpisodeJson, "before_context", &pEpisode->m_BeforeContext, false) ||
			!ReadString(pEpisodeJson, "after_context", &pEpisode->m_AfterContext, false) ||
			!ReadString(pEpisodeJson, "debug_reason", &pEpisode->m_DebugReason, false, MAX_EVENT_REASON_LENGTH) ||
			!ReadHelpers(pEpisodeJson, "helpers", &pEpisode->m_vHelpers) ||
			!ReadCausalRecoveryProof(pEpisodeJson, &pEpisode->m_CausalRecoveryProof))
			return false;
		pEpisode->m_EpisodeId = EpisodeId;
		pEpisode->m_Type = ImpactEventTypeFromName(Type.c_str());
		pEpisode->m_State = ImpactEpisodeStateFromName(State.c_str());
		pEpisode->m_CausalConfidence = CausalConfidenceFromName(CausalConfidence.c_str());
		pEpisode->m_HarmCause = HarmCauseFromName(HarmCause.c_str());
		pEpisode->m_HarmCausalConfidence = CausalConfidenceFromName(HarmCausalConfidence.c_str());
		pEpisode->m_OwnershipStatus = OwnershipStatusFromName(OwnershipStatus.c_str());
		pEpisode->m_UnfreezeCause = UnfreezeCauseFromName(UnfreezeCause.c_str());
		pEpisode->m_RecoveryNeed = RecoveryNeedFromName(RecoveryNeed.c_str());
		pEpisode->m_PhysicalOutcome = PhysicalOutcomeFromName(PhysicalOutcome.c_str());
		pEpisode->m_InitialRecoveryNeed = RecoveryNeedFromName(InitialRecoveryNeed.c_str());
		NormalizePendingEpisode(*pEpisode);
		return true;
	}

	static bool DeriveLegacyRescuesReceived(SRunSnapshot &SnapshotData)
	{
		std::vector<int> vActorSaveCounts(SnapshotData.m_vParticipants.size(), 0);
		std::vector<int> vTargetSaveCounts(SnapshotData.m_vParticipants.size(), 0);
		int64_t ParticipantSaveTotal = 0;
		for(const SParticipant &Participant : SnapshotData.m_vParticipants)
			ParticipantSaveTotal += Participant.m_RawStats.m_Saves;

		for(const SImpactEvent &Event : SnapshotData.m_vEvents)
		{
			if(Event.m_Type != EImpactEventType::SAVE_CONFIRMED)
				continue;
			if(!Event.m_Confirmed || !Event.m_Actor.IsValid() || !Event.m_Target.IsValid() ||
				Event.m_Actor == Event.m_Target || Event.m_RawDelta.m_Saves != 1)
				return false;
			auto Actor = std::find_if(SnapshotData.m_vParticipants.begin(), SnapshotData.m_vParticipants.end(), [&Event](const SParticipant &Participant) {
				return Participant.m_Id == Event.m_Actor;
			});
			auto Target = std::find_if(SnapshotData.m_vParticipants.begin(), SnapshotData.m_vParticipants.end(), [&Event](const SParticipant &Participant) {
				return Participant.m_Id == Event.m_Target;
			});
			if(Actor == SnapshotData.m_vParticipants.end() || Target == SnapshotData.m_vParticipants.end())
				return false;
			++vActorSaveCounts[static_cast<size_t>(std::distance(SnapshotData.m_vParticipants.begin(), Actor))];
			++vTargetSaveCounts[static_cast<size_t>(std::distance(SnapshotData.m_vParticipants.begin(), Target))];
		}

		if(ParticipantSaveTotal != SnapshotData.m_RawStats.m_Saves)
			return false;
		for(size_t i = 0; i < SnapshotData.m_vParticipants.size(); ++i)
		{
			const SParticipant &Participant = SnapshotData.m_vParticipants[i];
			if(vActorSaveCounts[i] != Participant.m_RawStats.m_Saves ||
				(Participant.m_RescuesReceivedKnown && Participant.m_RescuesReceived != vTargetSaveCounts[i]))
				return false;
		}

		for(size_t i = 0; i < SnapshotData.m_vParticipants.size(); ++i)
		{
			SParticipant &Participant = SnapshotData.m_vParticipants[i];
			if(!Participant.m_RescuesReceivedKnown)
			{
				Participant.m_RescuesReceived = vTargetSaveCounts[i];
				Participant.m_RescuesReceivedKnown = true;
			}
		}
		return true;
	}

	static bool ReadRun(const json_value *pRunJson, SRunSnapshot *pSnapshot)
	{
		if(pRunJson == nullptr || pRunJson->type != json_object || pSnapshot == nullptr)
			return false;
		SRunSnapshot SnapshotData;
		std::string State;
		std::string Result;
		if(!ReadInt(pRunJson, "schema_version", &SnapshotData.m_SchemaVersion) ||
			!ReadInt(pRunJson, "algorithm_version", &SnapshotData.m_AlgorithmVersion) ||
			!ReadInt(pRunJson, "static_analysis_version", &SnapshotData.m_StaticAnalysisVersion) ||
			!ReadUint64(pRunJson, "run_id", &SnapshotData.m_RunId) ||
			!ReadString(pRunJson, "run_uid", &SnapshotData.m_RunUid, false, UUID_MAXSTRSIZE) ||
			!ReadString(pRunJson, "server_key", &SnapshotData.m_Context.m_ServerKey, false) ||
			!ReadString(pRunJson, "server_name", &SnapshotData.m_Context.m_ServerName, false) ||
			!ReadString(pRunJson, "community_id", &SnapshotData.m_Context.m_CommunityId, false) ||
			!ReadString(pRunJson, "map", &SnapshotData.m_Context.m_Map, false) ||
			!ReadString(pRunJson, "map_difficulty", &SnapshotData.m_Context.m_MapDifficulty, false) ||
			!ReadInt(pRunJson, "map_stars", &SnapshotData.m_Context.m_MapStars, false) ||
			!ReadInt(pRunJson, "tick_speed", &SnapshotData.m_Context.m_TickSpeed) ||
			!ReadInt(pRunJson, "team_id", &SnapshotData.m_Context.m_TeamId, false) ||
			!ReadString(pRunJson, "state", &State) ||
			!ReadString(pRunJson, "result", &Result) ||
			!ReadSignedString(pRunJson, "run_started_tick", &SnapshotData.m_RunStartedTick) ||
			!ReadSignedString(pRunJson, "analysis_started_tick", &SnapshotData.m_AnalysisStartedTick) ||
			!ReadSignedString(pRunJson, "last_confirmed_tick", &SnapshotData.m_LastConfirmedTick) ||
			!ReadSignedString(pRunJson, "last_state_tick", &SnapshotData.m_LastStateTick) ||
			!ReadSignedString(pRunJson, "active_analysis_ticks", &SnapshotData.m_ActiveAnalysisTicks) ||
			!ReadSignedString(pRunJson, "end_tick", &SnapshotData.m_EndTick, false) ||
			!ReadInt(pRunJson, "authoritative_finish_time_millis", &SnapshotData.m_AuthoritativeFinishTimeMillis, false) ||
			!ReadSignedString(pRunJson, "saved_at", &SnapshotData.m_SavedAt))
			return false;
		if(!SnapshotData.m_RunUid.empty())
		{
			CUuid RunUid;
			if(SnapshotData.m_RunUid.size() != UUID_MAXSTRSIZE - 1 || ParseUuid(&RunUid, SnapshotData.m_RunUid.c_str()) != 0)
				return false;
		}
		const json_value *pGlobalRunNumber = Field(pRunJson, "global_run_number");
		if(pGlobalRunNumber != nullptr && pGlobalRunNumber->type != json_none && pGlobalRunNumber->type != json_null)
		{
			if(!ReadUint64(pRunJson, "global_run_number", &SnapshotData.m_GlobalRunNumber))
				return false;
			if(SnapshotData.m_GlobalRunNumber == 0)
				return false;
			SnapshotData.m_HasGlobalRunNumber = true;
		}
		const json_value *pScoringEvidenceVersion = Field(pRunJson, "scoring_evidence_version");
		const json_value *pScoringEvidenceComplete = Field(pRunJson, "scoring_evidence_complete");
		const json_value *pScoringEvidenceDropped = Field(pRunJson, "scoring_evidence_dropped");
		const json_value *pScoringEvidence = Field(pRunJson, "scoring_evidence");
		auto HasJsonField = [](const json_value *pValue) {
			return pValue != nullptr && pValue->type != json_none && pValue->type != json_null;
		};
		const bool HasScoringEvidenceMetadata =
			HasJsonField(pScoringEvidenceVersion) || HasJsonField(pScoringEvidenceComplete) ||
			HasJsonField(pScoringEvidenceDropped) || HasJsonField(pScoringEvidence);
		if(HasScoringEvidenceMetadata)
		{
			if(!HasJsonField(pScoringEvidenceVersion) || !HasJsonField(pScoringEvidenceComplete) ||
				!HasJsonField(pScoringEvidenceDropped) || !HasJsonField(pScoringEvidence) ||
				!ReadInt(pRunJson, "scoring_evidence_version", &SnapshotData.m_ScoringEvidenceVersion) ||
				!ReadBool(pRunJson, "scoring_evidence_complete", &SnapshotData.m_ScoringEvidenceComplete) ||
				!ReadUint64(pRunJson, "scoring_evidence_dropped", &SnapshotData.m_ScoringEvidenceDroppedEvents) ||
				pScoringEvidence->type != json_array || json_array_length(pScoringEvidence) > static_cast<int>(MAX_SCORING_EVIDENCE_EVENTS))
				return false;
			for(int i = 0; i < json_array_length(pScoringEvidence); ++i)
			{
				SImpactScoringEvidence Evidence;
				if(!ReadScoringEvidence(json_array_get(pScoringEvidence, i), &Evidence))
					return false;
				SnapshotData.m_vScoringEvidence.push_back(std::move(Evidence));
			}
		}
		SnapshotData.m_State = AnalysisStateFromName(State.c_str());
		SnapshotData.m_Result = RunResultFromName(Result.c_str());
		if(SnapshotData.m_Context.m_MapDifficulty.empty() || SnapshotData.m_Context.m_MapStars < 0 || SnapshotData.m_Context.m_MapStars > MAX_MAP_DIFFICULTY_STARS)
			SnapshotData.m_Context.m_MapStars = -1;

		const json_value *pParticipants = Field(pRunJson, "participants");
		if(pParticipants == nullptr || pParticipants->type != json_array || json_array_length(pParticipants) > static_cast<int>(MAX_PARTICIPANTS))
			return false;
		for(int i = 0; i < json_array_length(pParticipants); ++i)
		{
			const json_value *pParticipantJson = json_array_get(pParticipants, i);
			if(pParticipantJson == nullptr || pParticipantJson->type != json_object)
				return false;
			SParticipant Participant;
			const json_value *pRescuesReceived = Field(pParticipantJson, "rescues_received");
			const bool HasRescuesReceived = pRescuesReceived != nullptr && pRescuesReceived->type != json_none && pRescuesReceived->type != json_null;
			const json_value *pRescuesReceivedKnown = Field(pParticipantJson, "rescues_received_known");
			const bool HasRescuesReceivedKnown = pRescuesReceivedKnown != nullptr && pRescuesReceivedKnown->type != json_none && pRescuesReceivedKnown->type != json_null;
			const json_value *pTerminalStatus = Field(pParticipantJson, "terminal_status");
			const bool HasTerminalStatus = pTerminalStatus != nullptr && pTerminalStatus->type != json_none && pTerminalStatus->type != json_null;
			const json_value *pCosmetics = Field(pParticipantJson, "cosmetics");
			const json_value *pPhase4Score = Field(pParticipantJson, "phase4_score");
			const json_value *pParticipationTicks = Field(pParticipantJson, "participation_ticks");
			const json_value *pParticipationTicksKnown = Field(pParticipantJson, "participation_ticks_known");
			const json_value *pParticipationStartTick = Field(pParticipantJson, "participation_start_tick");
			const json_value *pParticipationTerminalTick = Field(pParticipantJson, "participation_terminal_tick");
			const bool HasParticipationTime = HasJsonField(pParticipationTicks) || HasJsonField(pParticipationTicksKnown) ||
				HasJsonField(pParticipationStartTick) || HasJsonField(pParticipationTerminalTick);
			int TerminalStatus = static_cast<int>(EParticipantTerminalStatus::NOT_FINALIZED);
			if(!ReadId(pParticipantJson, "id", &Participant.m_Id) ||
				!ReadString(pParticipantJson, "name", &Participant.m_Name, false) ||
				!ReadSignedString(pParticipantJson, "first_start_tick", &Participant.m_FirstStartTick) ||
				!ReadBool(pParticipantJson, "engaged", &Participant.m_Engaged, false) ||
				!ReadSignedString(pParticipantJson, "engaged_tick", &Participant.m_EngagedTick, false) ||
				!ReadSignedString(pParticipantJson, "personal_analysis_start_tick", &Participant.m_PersonalAnalysisStartTick) ||
				!ReadSignedString(pParticipantJson, "personal_analysis_start_active_ticks", &Participant.m_PersonalAnalysisStartActiveTicks, false) ||
				!ReadBool(pParticipantJson, "late", &Participant.m_Late) ||
				!ReadBool(pParticipantJson, "present", &Participant.m_Present) ||
				!ReadBool(pParticipantJson, "finalized", &Participant.m_Finalized, false) ||
				(HasTerminalStatus && !ReadInt(pParticipantJson, "terminal_status", &TerminalStatus)) ||
				(HasParticipationTime &&
					(!HasJsonField(pParticipationTicks) || !HasJsonField(pParticipationTicksKnown) ||
						!HasJsonField(pParticipationStartTick) || !HasJsonField(pParticipationTerminalTick) ||
						!ReadSignedString(pParticipantJson, "participation_ticks", &Participant.m_ParticipationTicks) ||
						!ReadBool(pParticipantJson, "participation_ticks_known", &Participant.m_ParticipationTicksKnown) ||
						!ReadSignedString(pParticipantJson, "participation_start_tick", &Participant.m_ParticipationStartTick) ||
						!ReadSignedString(pParticipantJson, "participation_terminal_tick", &Participant.m_ParticipationTerminalTick))) ||
				!ReadStats(pParticipantJson, "raw_stats", &Participant.m_RawStats) ||
				(HasRescuesReceived && !ReadInt(pParticipantJson, "rescues_received", &Participant.m_RescuesReceived)) ||
				(HasRescuesReceivedKnown && !ReadBool(pParticipantJson, "rescues_received_known", &Participant.m_RescuesReceivedKnown)))
				return false;
			if(HasTerminalStatus)
			{
				if(TerminalStatus < static_cast<int>(EParticipantTerminalStatus::NOT_FINALIZED) ||
					TerminalStatus > static_cast<int>(EParticipantTerminalStatus::OTHER))
					return false;
				Participant.m_TerminalStatus = static_cast<EParticipantTerminalStatus>(TerminalStatus);
			}
			else if(Participant.m_Finalized)
				Participant.m_TerminalStatus = EParticipantTerminalStatus::UNKNOWN;
			if(!ReadScoreSnapshot(pPhase4Score, &Participant.m_Score))
				return false;
			// Cosmetics are optional and presentation-only. Partial/legacy-invalid
			// cosmetics degrade to the UI fallback without invalidating the run.
			if(pCosmetics != nullptr && pCosmetics->type == json_object)
			{
				SParticipantCosmeticSnapshot Cosmetics;
				if(ReadString(pCosmetics, "skin_name", &Cosmetics.m_SkinName, true, MAX_CONTEXT_LENGTH) &&
					ReadBool(pCosmetics, "use_custom_color", &Cosmetics.m_UseCustomColor) &&
					ReadInt(pCosmetics, "body_color", &Cosmetics.m_ColorBody) &&
					ReadInt(pCosmetics, "feet_color", &Cosmetics.m_ColorFeet) &&
					!Cosmetics.m_SkinName.empty() && Cosmetics.m_SkinName.size() < MAX_SKIN_LENGTH)
				{
					Cosmetics.m_Available = true;
					Participant.m_Cosmetics = std::move(Cosmetics);
				}
			}
			if(!HasRescuesReceivedKnown)
				Participant.m_RescuesReceivedKnown = HasRescuesReceived;
			if(Participant.m_RescuesReceived < 0 || (Participant.m_RescuesReceivedKnown && !HasRescuesReceived))
				return false;
			if(Participant.m_ParticipationTicks < 0 ||
				(Participant.m_ParticipationStartTick >= 0 &&
					(!Participant.m_ParticipationTicksKnown || !Participant.m_Engaged || Participant.m_Finalized || Participant.m_ParticipationTerminalTick >= 0)) ||
				(Participant.m_ParticipationTerminalTick >= 0 && !Participant.m_Finalized))
				return false;
			if(Field(pParticipantJson, "engaged") == nullptr)
				Participant.m_Engaged = Participant.m_FirstStartTick >= 0;
			if(Participant.m_Engaged && Participant.m_EngagedTick < 0)
				Participant.m_EngagedTick = Participant.m_FirstStartTick;
			SnapshotData.m_vParticipants.push_back(std::move(Participant));
		}

		if(!ReadStats(pRunJson, "raw_stats", &SnapshotData.m_RawStats))
			return false;
		const json_value *pScore = Field(pRunJson, "score");
		if(!ReadScoreSnapshot(pScore, &SnapshotData.m_Score))
			return false;

		const json_value *pEvents = Field(pRunJson, "events");
		if(pEvents == nullptr || pEvents->type != json_array || json_array_length(pEvents) > static_cast<int>(MAX_LEDGER_EVENTS))
			return false;
		for(int i = 0; i < json_array_length(pEvents); ++i)
		{
			SImpactEvent Event;
			if(!ReadEvent(json_array_get(pEvents, i), &Event))
				return false;
			SnapshotData.m_vEvents.push_back(std::move(Event));
		}
		if(SnapshotData.m_ScoringEvidenceVersion < 0 || SnapshotData.m_ScoringEvidenceVersion > IMPACT_SCORING_EVIDENCE_VERSION ||
			(SnapshotData.m_ScoringEvidenceVersion == 0 && (!SnapshotData.m_vScoringEvidence.empty() || SnapshotData.m_ScoringEvidenceComplete || SnapshotData.m_ScoringEvidenceDroppedEvents != 0)) ||
			(SnapshotData.m_ScoringEvidenceDroppedEvents > 0 && SnapshotData.m_ScoringEvidenceComplete) ||
			!IsScoringEvidenceConsistent(SnapshotData))
			return false;
		std::unordered_set<uint64_t> ScoringEvidenceIds;
		for(const SImpactScoringEvidence &Evidence : SnapshotData.m_vScoringEvidence)
			if(Evidence.m_RunId != SnapshotData.m_RunId || !ScoringEvidenceIds.insert(Evidence.m_EventId).second)
				return false;
		if(std::any_of(SnapshotData.m_vParticipants.begin(), SnapshotData.m_vParticipants.end(), [](const SParticipant &Participant) {
			return !Participant.m_RescuesReceivedKnown;
		}))
			DeriveLegacyRescuesReceived(SnapshotData);
		const json_value *pPendingEpisodes = Field(pRunJson, "pending_episodes");
		if(pPendingEpisodes != nullptr && pPendingEpisodes->type != json_none)
		{
			if(pPendingEpisodes->type != json_array || json_array_length(pPendingEpisodes) > static_cast<int>(MAX_PENDING_EPISODES))
				return false;
			for(int i = 0; i < json_array_length(pPendingEpisodes); ++i)
			{
				SImpactPendingEpisode Episode;
				if(!ReadPendingEpisode(json_array_get(pPendingEpisodes, i), &Episode))
					return false;
				SnapshotData.m_vPendingEpisodes.push_back(std::move(Episode));
			}
		}
		const json_value *pForensic = Field(pRunJson, "forensic");
		if(pForensic == nullptr || pForensic->type == json_none)
		{
			SnapshotData.m_ForensicVersion = 0;
			SnapshotData.m_ForensicTraceEnabled = false;
			SnapshotData.m_ForensicTraceComplete = false;
		}
		else
		{
			if(pForensic->type != json_object ||
				!ReadInt(pForensic, "version", &SnapshotData.m_ForensicVersion))
				return false;
			const json_value *pForensicEvents = Field(pForensic, "events");
			if(SnapshotData.m_ForensicVersion == 0)
			{
				bool Enabled = false;
				bool CompleteFromStart = false;
				bool Truncated = false;
				uint64_t DroppedEvents = 0;
				if(!ReadBool(pForensic, "enabled", &Enabled) || Enabled ||
					!ReadBool(pForensic, "complete_from_start", &CompleteFromStart) || CompleteFromStart ||
					!ReadBool(pForensic, "trace_truncated", &Truncated) || Truncated ||
					!ReadUint64(pForensic, "dropped_events", &DroppedEvents) || DroppedEvents != 0 ||
					pForensicEvents == nullptr || pForensicEvents->type != json_array || json_array_length(pForensicEvents) != 0)
					return false;
				SnapshotData.m_ForensicTraceEnabled = false;
				SnapshotData.m_ForensicTraceComplete = false;
			}
			else if((SnapshotData.m_ForensicVersion != IMPACT_FORENSIC_LEGACY_VERSION && SnapshotData.m_ForensicVersion != IMPACT_FORENSIC_VERSION) ||
				!ReadBool(pForensic, "enabled", &SnapshotData.m_ForensicTraceEnabled) ||
				!ReadBool(pForensic, "complete_from_start", &SnapshotData.m_ForensicTraceComplete) ||
				!ReadBool(pForensic, "trace_truncated", &SnapshotData.m_ForensicTraceTruncated) ||
				!ReadUint64(pForensic, "dropped_events", &SnapshotData.m_ForensicDroppedEvents) ||
				pForensicEvents == nullptr || pForensicEvents->type != json_array ||
				json_array_length(pForensicEvents) > static_cast<int>(MAX_FORENSIC_EVENTS))
				return false;
			if(SnapshotData.m_ForensicVersion != 0)
			{
				uint64_t PreviousSequence = 0;
				for(int i = 0; i < json_array_length(pForensicEvents); ++i)
				{
					SImpactForensicEvent Event;
					if(!ReadForensicEvent(json_array_get(pForensicEvents, i), SnapshotData.m_ForensicVersion, &Event) ||
						Event.m_RunId != SnapshotData.m_RunId || Event.m_Sequence <= PreviousSequence)
						return false;
					PreviousSequence = Event.m_Sequence;
					SnapshotData.m_vForensicEvents.push_back(std::move(Event));
				}
				if(SnapshotData.m_ForensicDroppedEvents > 0 && !SnapshotData.m_ForensicTraceTruncated)
					return false;
			}
		}
		*pSnapshot = std::move(SnapshotData);
		return true;
	}

	static bool ParseJsonData(const void *pJsonData, unsigned Length, bool (*pReader)(const void *, void *), void *pUser)
	{
		json_value *pJson = JsonParse(static_cast<const json_char *>(pJsonData), Length);
		if(pJson == nullptr)
			return false;
		const bool Result = pReader(pJson, pUser);
		json_value_free(pJson);
		return Result;
	}

	bool CPersistence::WriteAtomic(IStorage *pStorage, const char *pFile, const char *pTempFile, const char *pBackupFile, const std::string &Data)
	{
		if(pStorage == nullptr || !pStorage->CreateFolder("amf_impact", IStorage::TYPE_SAVE))
			return false;
		IOHANDLE File = pStorage->OpenFile(pTempFile, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Written = io_write(File, Data.data(), static_cast<unsigned>(Data.size())) == Data.size();
		const bool Synced = Written && io_sync(File) == 0 && io_error(File) == 0;
		const bool Closed = io_close(File) == 0;
		if(!Written || !Synced || !Closed)
		{
			pStorage->RemoveFile(pTempFile, IStorage::TYPE_SAVE);
			return false;
		}

		const bool HadTarget = pStorage->FileExists(pFile, IStorage::TYPE_SAVE);
		if(HadTarget)
		{
			pStorage->RemoveFile(pBackupFile, IStorage::TYPE_SAVE);
			if(!pStorage->RenameFile(pFile, pBackupFile, IStorage::TYPE_SAVE))
			{
				pStorage->RemoveFile(pTempFile, IStorage::TYPE_SAVE);
				return false;
			}
		}
		if(!pStorage->RenameFile(pTempFile, pFile, IStorage::TYPE_SAVE))
		{
			if(HadTarget)
				pStorage->RenameFile(pBackupFile, pFile, IStorage::TYPE_SAVE);
			pStorage->RemoveFile(pTempFile, IStorage::TYPE_SAVE);
			return false;
		}
		return true;
	}

	bool CPersistence::ReadJson(IStorage *pStorage, const char *pFile, bool (*pReader)(const void *, void *), void *pUser)
	{
		if(pStorage == nullptr || pReader == nullptr || !pStorage->FileExists(pFile, IStorage::TYPE_SAVE))
			return false;
		void *pData = nullptr;
		unsigned Length = 0;
		if(!pStorage->ReadFile(pFile, IStorage::TYPE_SAVE, &pData, &Length) || pData == nullptr)
			return false;
		const bool Result = ParseJsonData(pData, Length, pReader, pUser);
		free(pData);
		return Result;
	}

	bool CPersistence::ReadHistoryDocument(const void *pJson, void *pUser)
	{
		if(pJson == nullptr || pUser == nullptr)
			return false;
		const json_value *pRoot = static_cast<const json_value *>(pJson);
		const json_value *pRuns = Field(pRoot, "runs");
		std::vector<SRunSnapshot> *pHistory = static_cast<std::vector<SRunSnapshot> *>(pUser);
		if(pRoot->type != json_object || pRuns == nullptr || pRuns->type != json_array)
			return false;
		std::vector<SRunSnapshot> vHistory;
		for(int i = 0; i < json_array_length(pRuns); ++i)
		{
			SRunSnapshot SnapshotData;
			if(!ReadRun(json_array_get(pRuns, i), &SnapshotData) || !SnapshotData.m_RunId)
				return false;
			vHistory.push_back(std::move(SnapshotData));
		}
		*pHistory = std::move(vHistory);
		return true;
	}

	bool CPersistence::ReadActiveDocument(const void *pJson, void *pUser)
	{
		if(pJson == nullptr || pUser == nullptr)
			return false;
		SActiveLoad *pLoad = static_cast<SActiveLoad *>(pUser);
		const json_value *pRoot = static_cast<const json_value *>(pJson);
		const json_value *pRun = Field(pRoot, "run");
		if(pRoot->type != json_object || pRun == nullptr ||
			!ReadUint64(pRoot, "next_run_id", pLoad->m_pNextRunId) ||
			!ReadUint64(pRoot, "next_event_id", pLoad->m_pNextEventId) ||
			!ReadRun(pRun, pLoad->m_pSnapshot))
			return false;
		return pLoad->m_pSnapshot->m_RunId != 0;
	}

	bool CPersistence::SaveHistory(IStorage *pStorage, const std::vector<SRunSnapshot> &vHistory, bool HistoryLoadSucceeded)
	{
		if(!HistoryLoadSucceeded || pStorage == nullptr)
			return false;
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("schema_version");
		Writer.WriteIntValue(IMPACT_SCHEMA_VERSION);
		Writer.WriteAttribute("runs");
		Writer.BeginArray();
		for(const SRunSnapshot &SnapshotData : vHistory)
			WriteRun(Writer, SnapshotData);
		Writer.EndArray();
		Writer.EndObject();
		const std::string Data = Writer.GetOutputString();
		return WriteAtomic(pStorage, HISTORY_FILE, HISTORY_TEMP_FILE, HISTORY_BACKUP_FILE, Data);
	}

	bool CPersistence::LoadHistory(IStorage *pStorage, std::vector<SRunSnapshot> *pHistory)
	{
		if(pStorage == nullptr || pHistory == nullptr)
			return false;
		const bool MainExists = pStorage->FileExists(HISTORY_FILE, IStorage::TYPE_SAVE);
		const bool BackupExists = pStorage->FileExists(HISTORY_BACKUP_FILE, IStorage::TYPE_SAVE);
		std::vector<SRunSnapshot> vHistory;
		if(MainExists && ReadJson(pStorage, HISTORY_FILE, ReadHistoryDocument, &vHistory))
		{
			*pHistory = std::move(vHistory);
			return true;
		}
		vHistory.clear();
		if(BackupExists && ReadJson(pStorage, HISTORY_BACKUP_FILE, ReadHistoryDocument, &vHistory))
		{
			*pHistory = std::move(vHistory);
			return true;
		}
		if(!MainExists && !BackupExists)
		{
			pHistory->clear();
			return true;
		}
		return false;
	}

	bool CPersistence::SaveActive(IStorage *pStorage, const SRunSnapshot &SnapshotData, uint64_t NextRunId, uint64_t NextEventId)
	{
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("schema_version");
		Writer.WriteIntValue(IMPACT_SCHEMA_VERSION);
		Writer.WriteAttribute("next_run_id");
		WriteUint64(Writer, NextRunId);
		Writer.WriteAttribute("next_event_id");
		WriteUint64(Writer, NextEventId);
		Writer.WriteAttribute("run");
		WriteRun(Writer, SnapshotData);
		Writer.EndObject();
		const std::string Data = Writer.GetOutputString();
		return WriteAtomic(pStorage, ACTIVE_FILE, ACTIVE_TEMP_FILE, ACTIVE_BACKUP_FILE, Data);
	}

	bool CPersistence::LoadActive(IStorage *pStorage, SRunSnapshot *pSnapshot, uint64_t *pNextRunId, uint64_t *pNextEventId)
	{
		if(pSnapshot == nullptr || pNextRunId == nullptr || pNextEventId == nullptr)
			return false;
		SActiveLoad Load{pSnapshot, pNextRunId, pNextEventId};
		if(ReadJson(pStorage, ACTIVE_FILE, ReadActiveDocument, &Load))
			return true;
		return ReadJson(pStorage, ACTIVE_BACKUP_FILE, ReadActiveDocument, &Load);
	}

	void CPersistence::RemoveActive(IStorage *pStorage)
	{
		if(pStorage == nullptr)
			return;
		pStorage->RemoveFile(ACTIVE_FILE, IStorage::TYPE_SAVE);
		pStorage->RemoveFile(ACTIVE_TEMP_FILE, IStorage::TYPE_SAVE);
		pStorage->RemoveFile(ACTIVE_BACKUP_FILE, IStorage::TYPE_SAVE);
	}
}
