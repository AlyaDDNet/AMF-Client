#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RUN_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RUN_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_event_ledger.h"

namespace AmfImpact
{
	// Shared validity rule for consumers of the immutable per-participant score.
	// This only validates persisted dimensions; it never computes them.
	bool HasUsableFrozenFinalScore(const SParticipant &Participant);

	// Orders all finalized participants with valid frozen scores, independently
	// of participant terminal status. The comparator reads exact score values;
	// MVP eligibility is derived separately from the terminal run result and
	// canonical duration. This helper never computes or mutates scores. The
	// legacy boolean argument is retained for callers but no longer gates results.
	std::vector<SFinalRankingEntry> BuildFinalRanking(const SRunSnapshot &SnapshotData, bool IsSavedFinalResult);

	class CImpactRun
	{
		SRunContext m_Context;
		EAnalysisState m_State = EAnalysisState::WAITING;
		ERunResult m_Result = ERunResult::NONE;
		uint64_t m_RunId = 0;
		std::string m_RunUid;
		bool m_HasGlobalRunNumber = false;
		uint64_t m_GlobalRunNumber = 0;
		int64_t m_RunStartedTick = -1;
		int64_t m_AnalysisStartedTick = -1;
		int64_t m_LastConfirmedTick = -1;
		int64_t m_LastStateTick = -1;
		int64_t m_ActiveAnalysisTicks = 0;
		int64_t m_EndTick = -1;
		int m_AuthoritativeFinishTimeMillis = -1;
		SRawStats m_RawStats;
		SScoreSnapshot m_Score;
		std::vector<SParticipant> m_vParticipants;
		int m_ScoringEvidenceVersion = IMPACT_SCORING_EVIDENCE_VERSION;
		bool m_ScoringEvidenceComplete = true;
		uint64_t m_ScoringEvidenceDroppedEvents = 0;
		std::vector<SImpactScoringEvidence> m_vScoringEvidence;
		std::vector<SImpactPendingEpisode> m_vPendingEpisodes;
		std::vector<SImpactForensicEvent> m_vForensicEvents;
		uint64_t m_NextForensicSequence = 1;
		uint64_t m_ForensicDroppedEvents = 0;
		bool m_ForensicTraceComplete = true;
		bool m_ForensicTraceTruncated = false;
		bool m_ForensicTraceEnabled = true;
		CEventLedger m_EventLedger;

		SParticipant *FindParticipant(const SParticipantId &Id);
		const SParticipant *FindParticipant(const SParticipantId &Id) const;
		int PresentParticipantCount() const;
		int EngagedActiveParticipantCount() const;
		void Transition(EAnalysisState NewState, int64_t Tick);
		void UpdateStateForRoster(int64_t Tick);
		void ApplyRawDelta(SParticipant *pParticipant, const SRawStats &Delta);
		bool ApplyCanonicalEvent(const SImpactEvent &Event);
		void CloseParticipantParticipation(SParticipant &Participant, int64_t Tick);
		void AppendScoringEvidence(const SImpactEvent &Event, const SImpactForensicEvent &Forensic, const std::vector<SImpactEvent> &AcceptedBatch);
		void AppendForensicEvent(SImpactForensicEvent Event);

	public:
		void Initialize(uint64_t RunId, const SRunContext &Context);
		bool UpdateMapDifficultyMetadata(const SMapDifficultyMetadata &Metadata);
		bool UpdateAuthoritativeMapDifficultyMetadata(const SMapDifficultyMetadata &Metadata);
		void Clear();

		bool ApplyTickEventBatch(const STickEventBatch &Batch);
		bool Finalize(ERunResult Result, int64_t Tick);
		bool RestoreSnapshot(const SRunSnapshot &Snapshot);
		SRunSnapshot Snapshot(int64_t CurrentTick, int64_t SavedAt = 0) const;
		bool MakeParticipantResetSnapshot(const SParticipantId &Participant, uint64_t SnapshotRunId, int64_t CurrentTick, int64_t SavedAt, SRunSnapshot *pSnapshot) const;

		bool IsInitialized() const { return m_RunId != 0; }
		bool IsFinalized() const { return m_State == EAnalysisState::FINALIZED; }
		static bool IsGameplayProcessingAllowed(EAnalysisState State, int ActiveParticipantCount)
		{
			return ActiveParticipantCount >= 2 &&
				(State == EAnalysisState::ANALYZING || State == EAnalysisState::ACTIVE);
		}
		uint64_t RunId() const { return m_RunId; }
		const std::string &RunUid() const { return m_RunUid; }
		bool HasGlobalRunNumber() const { return m_HasGlobalRunNumber; }
		uint64_t GlobalRunNumber() const { return m_GlobalRunNumber; }
		bool AssignGlobalRunNumber(uint64_t Number)
		{
			if(Number == 0)
				return false;
			if(m_HasGlobalRunNumber)
				return m_GlobalRunNumber == Number;
			m_GlobalRunNumber = Number;
			m_HasGlobalRunNumber = true;
			return true;
		}
		EAnalysisState State() const { return m_State; }
		ERunResult Result() const { return m_Result; }
		const SRunContext &Context() const { return m_Context; }
		const std::vector<SParticipant> &Participants() const { return m_vParticipants; }
		const SRawStats &RawStats() const { return m_RawStats; }
		const SScoreSnapshot &Score() const { return m_Score; }
		int AuthoritativeFinishTimeMillis() const { return m_AuthoritativeFinishTimeMillis; }
		const std::vector<SImpactEvent> &Events() const { return m_EventLedger.Events(); }
		const std::vector<SImpactPendingEpisode> &PendingEpisodes() const { return m_vPendingEpisodes; }
		const std::vector<SImpactForensicEvent> &ForensicEvents() const { return m_vForensicEvents; }
		const std::vector<SImpactScoringEvidence> &ScoringEvidence() const { return m_vScoringEvidence; }
		int ScoringEvidenceVersion() const { return m_ScoringEvidenceVersion; }
		bool ScoringEvidenceComplete() const { return m_ScoringEvidenceComplete; }
		uint64_t ScoringEvidenceDroppedEvents() const { return m_ScoringEvidenceDroppedEvents; }
		void AppendForensicEvents(const std::vector<SImpactForensicEvent> &vEvents, bool OutputTruncated = false, uint64_t DroppedEvents = 0);
		void SetForensicTraceEnabled(bool Enabled)
		{
			if(!Enabled)
				m_ForensicTraceComplete = false;
			m_ForensicTraceEnabled = Enabled;
		}
		int StartedParticipantCount() const;
		int RosterMemberCount() const { return static_cast<int>(m_vParticipants.size()); }
		int EngagedParticipantCount() const;
		int ActiveEngagedParticipantCount() const { return EngagedActiveParticipantCount(); }
		int ActiveParticipantCount() const;
		int FinalizedParticipantCount() const;
		int EligibleParticipantCount() const { return PresentParticipantCount(); }
		bool HasParticipant(const SParticipantId &Id) const { return FindParticipant(Id) != nullptr; }
		bool HasStartedParticipant(const SParticipantId &Id) const;
		bool IsParticipantEngaged(const SParticipantId &Id) const;
		bool IsParticipantFinalized(const SParticipantId &Id) const;
		bool AddRosterMember(const SParticipantId &Id, const char *pName, int64_t Tick, const char *pReason);
		bool ProveParticipantEngaged(const SParticipantId &Id, int64_t Tick, const char *pReason);
		bool ProveEngagementFromEvent(const SImpactEvent &Event);
		bool UpdateParticipantName(const SParticipantId &Id, const char *pName);
		bool ParticipantPresent(const SParticipantId &Id, bool *pPresent) const;
		void SetPendingEpisodes(const std::vector<SImpactPendingEpisode> &vEpisodes);
		bool HasPassedDisplayGate(const SParticipantId &Id, int64_t CurrentTick) const;
		bool IsShortRun(int64_t CurrentTick) const;
		int64_t TeamRaceTicks(int64_t CurrentTick) const;
		int TeamRaceSeconds(int64_t CurrentTick) const;
		int64_t ActiveAnalysisTicks(int64_t CurrentTick) const;
		int ActiveAnalysisSeconds(int64_t CurrentTick) const;
	};
}

#endif
