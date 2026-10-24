/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_run.h"
#include "impact_rating.h"

#include <base/str.h>
#include <engine/shared/uuid_manager.h>

#include <algorithm>
#include <climits>

namespace AmfImpact
{
	static bool IsGameplayImpactEvent(EImpactEventType Type);

	namespace
	{
		void FreezeFinalScoreSnapshots(SRunSnapshot &SnapshotData, const SParticipantId *pOnlyParticipant = nullptr)
		{
			if(SnapshotData.m_State != EAnalysisState::FINALIZED || !IsTerminalResult(SnapshotData.m_Result))
				return;

			std::vector<SParticipantId> vScoresToCompute;
			vScoresToCompute.reserve(SnapshotData.m_vParticipants.size());
			// First freeze availability for every selected identity. A failed/unsupported
			// aggregation must remain explicitly unavailable and must not be retried later.
			for(SParticipant &Participant : SnapshotData.m_vParticipants)
			{
				if(pOnlyParticipant != nullptr && Participant.m_Id != *pOnlyParticipant)
					continue;
				if(Participant.m_Score.m_Frozen)
					continue;
				vScoresToCompute.push_back(Participant.m_Id);
				Participant.m_Score = {};
				Participant.m_Score.m_Frozen = true;
				Participant.m_Score.m_RatingVersion = IMPACT_RATING_VERSION;
				Participant.m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
			}

			CRunScoringContext Context;
			const SPhase4ScoringAggregationOptions Options;
			if(!BuildPhase4ScoringFeatures(SnapshotData, Options, &Context))
				return;

			for(const SPlayerScoringFeatures &Player : Context.m_vPlayers)
			{
				if(!Player.m_InFinalizedScoringCohort ||
					(pOnlyParticipant != nullptr && Player.m_Id != *pOnlyParticipant))
					continue;
				if(std::find(vScoresToCompute.begin(), vScoresToCompute.end(), Player.m_Id) == vScoresToCompute.end())
					continue;
				auto ParticipantIt = std::find_if(SnapshotData.m_vParticipants.begin(), SnapshotData.m_vParticipants.end(), [&Player](const SParticipant &Participant) {
					return Participant.m_Id == Player.m_Id;
				});
				if(ParticipantIt == SnapshotData.m_vParticipants.end())
					continue;

				SStabilityResult Stability;
				SImpactResult Impact;
				if(!CalculateStabilityV1(Player, Context, &Stability) ||
					!CalculateImpactV1(Player, Context, &Impact, &Stability) ||
					Stability.m_Id != Player.m_Id || Impact.m_Id != Player.m_Id ||
					!Stability.m_Available || !Impact.m_Available)
					continue;

				const SFinalizedScoringDimensions Dimensions = MakeFinalizedScoringDimensions(Impact, Stability);
				SRatingResult Rating;
				if(!CalculateRatingV1(Dimensions, &Rating) || !Rating.m_Available || Rating.m_Id != Player.m_Id ||
					!std::isfinite(Impact.m_FinalImpact) || !std::isfinite(Stability.m_Value) ||
					!std::isfinite(Rating.m_FinalRating))
					continue;

				ParticipantIt->m_Score.m_Impact = Impact.m_FinalImpact;
				ParticipantIt->m_Score.m_Stability = Stability.m_Value;
				ParticipantIt->m_Score.m_Rating = Rating.m_FinalRating;
				ParticipantIt->m_Score.m_Available = true;
				ParticipantIt->m_Score.m_RatingVersion = IMPACT_RATING_VERSION;
				ParticipantIt->m_Score.m_ScoringVersion = IMPACT_SCORING_VERSION;
			}
		}

		void CopyFrozenParticipantScores(const SRunSnapshot &SnapshotData, std::vector<SParticipant> &vParticipants)
		{
			for(SParticipant &Participant : vParticipants)
			{
				const auto It = std::find_if(SnapshotData.m_vParticipants.begin(), SnapshotData.m_vParticipants.end(), [&Participant](const SParticipant &SnapshotParticipant) {
					return SnapshotParticipant.m_Id == Participant.m_Id;
				});
				if(It != SnapshotData.m_vParticipants.end())
					Participant.m_Score = It->m_Score;
			}
		}
	}

	namespace
	{
		bool HasNonFinishMvpDuration(const SRunSnapshot &SnapshotData)
		{
			const int TickSpeed = SnapshotData.m_Context.m_TickSpeed;
			return TickSpeed > 0 && TickSpeed <= 1000 && SnapshotData.m_ActiveAnalysisTicks >= 0 &&
				SnapshotData.m_ActiveAnalysisTicks >= static_cast<int64_t>(300) * TickSpeed;
		}

		bool FinalParticipantLess(const SRunSnapshot &SnapshotData, size_t LeftIndex, size_t RightIndex)
		{
			const SParticipant &Left = SnapshotData.m_vParticipants[LeftIndex];
			const SParticipant &Right = SnapshotData.m_vParticipants[RightIndex];
			if(Left.m_Score.m_Rating != Right.m_Score.m_Rating)
				return Left.m_Score.m_Rating > Right.m_Score.m_Rating;
			if(Left.m_Score.m_Impact != Right.m_Score.m_Impact)
				return Left.m_Score.m_Impact > Right.m_Score.m_Impact;
			if(Left.m_Score.m_Stability != Right.m_Score.m_Stability)
				return Left.m_Score.m_Stability > Right.m_Score.m_Stability;

			// Client ID + generation is persisted and stable within the immutable
			// run. The participant-vector index is a deterministic last resort for
			// malformed duplicate identities and is preserved by history round-trip.
			if(Left.m_Id.m_ClientId != Right.m_Id.m_ClientId)
				return Left.m_Id.m_ClientId < Right.m_Id.m_ClientId;
			if(Left.m_Id.m_SlotGeneration != Right.m_Id.m_SlotGeneration)
				return Left.m_Id.m_SlotGeneration < Right.m_Id.m_SlotGeneration;
			return LeftIndex < RightIndex;
		}
	}

	bool HasUsableFrozenFinalScore(const SParticipant &Participant)
	{
		const SScoreSnapshot &Score = Participant.m_Score;
		return Participant.m_Engaged && Participant.m_Id.IsValid() && Score.m_Frozen && Score.m_Available &&
			Score.m_RatingVersion == IMPACT_RATING_VERSION && Score.m_ScoringVersion == IMPACT_SCORING_VERSION &&
			std::isfinite(Score.m_Rating) && std::isfinite(Score.m_Impact) && std::isfinite(Score.m_Stability);
	}

	std::vector<SFinalRankingEntry> BuildFinalRanking(const SRunSnapshot &SnapshotData, bool)
	{
		if(SnapshotData.m_State != EAnalysisState::FINALIZED || !IsTerminalResult(SnapshotData.m_Result))
			return {};

		std::vector<size_t> vParticipantIndices;
		vParticipantIndices.reserve(SnapshotData.m_vParticipants.size());
		for(size_t i = 0; i < SnapshotData.m_vParticipants.size(); ++i)
		{
			const SParticipant &Participant = SnapshotData.m_vParticipants[i];
			if(Participant.m_Finalized && HasUsableFrozenFinalScore(Participant))
				vParticipantIndices.push_back(i);
		}

		std::sort(vParticipantIndices.begin(), vParticipantIndices.end(), [&SnapshotData](size_t LeftIndex, size_t RightIndex) {
			return FinalParticipantLess(SnapshotData, LeftIndex, RightIndex);
		});

		// Places include every finalized participant with a comparable frozen
		// score. MVP eligibility is intentionally independent from place eligibility.
		size_t MvpParticipantIndex = SnapshotData.m_vParticipants.size();
		const bool IsSuccessfulFinish = SnapshotData.m_Result == ERunResult::FINISHED;
		if(IsSuccessfulFinish || HasNonFinishMvpDuration(SnapshotData))
		{
			for(const size_t ParticipantIndex : vParticipantIndices)
			{
				const SParticipant &Participant = SnapshotData.m_vParticipants[ParticipantIndex];
				if(IsSuccessfulFinish ? Participant.m_TerminalStatus == EParticipantTerminalStatus::FINISHED : Participant.m_TerminalStatus != EParticipantTerminalStatus::LEFT)
				{
					MvpParticipantIndex = ParticipantIndex;
					break;
				}
			}
		}

		std::vector<SFinalRankingEntry> vRanking;
		vRanking.reserve(vParticipantIndices.size());
		for(size_t i = 0; i < vParticipantIndices.size(); ++i)
		{
			SFinalRankingEntry Entry;
			Entry.m_ParticipantIndex = vParticipantIndices[i];
			Entry.m_Rank = static_cast<int>(i + 1);
			Entry.m_IsMvp = Entry.m_ParticipantIndex == MvpParticipantIndex;
			vRanking.push_back(Entry);
		}
		return vRanking;
	}

	static void CopyContextString(std::string &Target, const std::string &Source)
	{
		Target = Source.substr(0, MAX_CONTEXT_LENGTH);
	}

	static std::string NewRunUid()
	{
		char aRunUid[UUID_MAXSTRSIZE];
		FormatUuid(RandomUuid(), aRunUid, sizeof(aRunUid));
		return aRunUid;
	}

	void CImpactRun::Initialize(uint64_t RunId, const SRunContext &Context)
	{
		Clear();
		m_RunId = RunId;
		m_RunUid = NewRunUid();
		m_ScoringEvidenceVersion = IMPACT_SCORING_EVIDENCE_VERSION;
		m_ScoringEvidenceComplete = true;
		CopyContextString(m_Context.m_ServerKey, Context.m_ServerKey);
		CopyContextString(m_Context.m_ServerName, Context.m_ServerName);
		CopyContextString(m_Context.m_CommunityId, Context.m_CommunityId);
		CopyContextString(m_Context.m_Map, Context.m_Map);
		CopyContextString(m_Context.m_MapDifficulty, Context.m_MapDifficulty);
		m_Context.m_MapStars = Context.m_MapStars >= 0 && Context.m_MapStars <= MAX_MAP_DIFFICULTY_STARS ? Context.m_MapStars : -1;
		m_Context.m_TickSpeed = std::clamp(Context.m_TickSpeed, 1, 1000);
		m_Context.m_TeamId = Context.m_TeamId;
		m_vParticipants.reserve(MAX_PARTICIPANTS);
	}

	bool CImpactRun::UpdateMapDifficultyMetadata(const SMapDifficultyMetadata &Metadata)
	{
		if(!IsInitialized() || IsFinalized() || !IsMapDifficultyMetadataUsable(Metadata))
			return false;
		bool Changed = false;
		if(m_Context.m_MapDifficulty.empty() && !Metadata.m_Category.empty())
		{
			CopyContextString(m_Context.m_MapDifficulty, Metadata.m_Category);
			Changed = true;
		}
		if(m_Context.m_MapStars < 0 && Metadata.m_Stars >= 0 && Metadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS)
		{
			m_Context.m_MapStars = Metadata.m_Stars;
			Changed = true;
		}
		if(!Changed)
			return false;
		return true;
	}

	bool CImpactRun::UpdateAuthoritativeMapDifficultyMetadata(const SMapDifficultyMetadata &Metadata)
	{
		if(!IsInitialized() || IsFinalized() || !IsMapDifficultyMetadataUsable(Metadata))
			return false;
		bool Changed = false;
		if(!Metadata.m_Category.empty() && m_Context.m_MapDifficulty != Metadata.m_Category)
		{
			CopyContextString(m_Context.m_MapDifficulty, Metadata.m_Category);
			Changed = true;
		}
		if(Metadata.m_Stars >= 0 && Metadata.m_Stars <= MAX_MAP_DIFFICULTY_STARS && m_Context.m_MapStars != Metadata.m_Stars)
		{
			m_Context.m_MapStars = Metadata.m_Stars;
			Changed = true;
		}
		return Changed;
	}

	void CImpactRun::Clear()
	{
		m_Context = {};
		m_State = EAnalysisState::WAITING;
		m_Result = ERunResult::NONE;
		m_RunId = 0;
		m_RunUid.clear();
		m_HasGlobalRunNumber = false;
		m_GlobalRunNumber = 0;
		m_RunStartedTick = -1;
		m_AnalysisStartedTick = -1;
		m_LastConfirmedTick = -1;
		m_LastStateTick = -1;
		m_ActiveAnalysisTicks = 0;
		m_EndTick = -1;
		m_AuthoritativeFinishTimeMillis = -1;
		m_RawStats = {};
		m_Score = {};
		m_vParticipants.clear();
		m_ScoringEvidenceVersion = 0;
		m_ScoringEvidenceComplete = false;
		m_ScoringEvidenceDroppedEvents = 0;
		m_vScoringEvidence.clear();
		m_vPendingEpisodes.clear();
		m_vForensicEvents.clear();
		m_NextForensicSequence = 1;
		m_ForensicDroppedEvents = 0;
		m_ForensicTraceComplete = true;
		m_ForensicTraceTruncated = false;
		m_ForensicTraceEnabled = true;
		m_EventLedger.Clear();
	}

	SParticipant *CImpactRun::FindParticipant(const SParticipantId &Id)
	{
		for(SParticipant &Participant : m_vParticipants)
			if(Participant.m_Id == Id)
				return &Participant;
		return nullptr;
	}

	const SParticipant *CImpactRun::FindParticipant(const SParticipantId &Id) const
	{
		for(const SParticipant &Participant : m_vParticipants)
			if(Participant.m_Id == Id)
				return &Participant;
		return nullptr;
	}

	int CImpactRun::PresentParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += Participant.m_Present && !Participant.m_Finalized ? 1 : 0;
		return Count;
	}

	int CImpactRun::EngagedActiveParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += Participant.m_Engaged && Participant.m_Present && !Participant.m_Finalized ? 1 : 0;
		return Count;
	}

	int CImpactRun::EngagedParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += Participant.m_Engaged && !Participant.m_Finalized ? 1 : 0;
		return Count;
	}

	int CImpactRun::StartedParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += Participant.m_FirstStartTick >= 0 ? 1 : 0;
		return Count;
	}

	int CImpactRun::ActiveParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += !Participant.m_Finalized ? 1 : 0;
		return Count;
	}

	int CImpactRun::FinalizedParticipantCount() const
	{
		int Count = 0;
		for(const SParticipant &Participant : m_vParticipants)
			Count += Participant.m_Finalized ? 1 : 0;
		return Count;
	}

	bool CImpactRun::HasStartedParticipant(const SParticipantId &Id) const
	{
		const SParticipant *pParticipant = FindParticipant(Id);
		return pParticipant != nullptr && !pParticipant->m_Finalized && pParticipant->m_FirstStartTick >= 0;
	}

	bool CImpactRun::IsParticipantEngaged(const SParticipantId &Id) const
	{
		const SParticipant *pParticipant = FindParticipant(Id);
		return pParticipant != nullptr && !pParticipant->m_Finalized && pParticipant->m_Engaged;
	}

	bool CImpactRun::IsParticipantFinalized(const SParticipantId &Id) const
	{
		const SParticipant *pParticipant = FindParticipant(Id);
		return pParticipant != nullptr && pParticipant->m_Finalized;
	}

	void CImpactRun::Transition(EAnalysisState NewState, int64_t Tick)
	{
		if(m_State == NewState || m_State == EAnalysisState::FINALIZED)
			return;
		const EAnalysisState PreviousState = m_State;
		if(m_State == EAnalysisState::ACTIVE && Tick >= m_LastStateTick)
			m_ActiveAnalysisTicks += Tick - m_LastStateTick;
		m_LastStateTick = Tick;
		m_State = NewState;
		if(NewState == EAnalysisState::ACTIVE && m_AnalysisStartedTick < 0)
		{
			m_AnalysisStartedTick = Tick;
			for(SParticipant &Participant : m_vParticipants)
			{
				if(Participant.m_PersonalAnalysisStartTick < 0)
					Participant.m_PersonalAnalysisStartTick = Tick;
				if(Participant.m_PersonalAnalysisStartActiveTicks < 0)
					Participant.m_PersonalAnalysisStartActiveTicks = m_ActiveAnalysisTicks;
			}
		}
		if((PreviousState == EAnalysisState::ACTIVE && NewState == EAnalysisState::PAUSED_NO_TEAM) ||
			(PreviousState == EAnalysisState::PAUSED_NO_TEAM && NewState == EAnalysisState::ACTIVE))
		{
			SImpactForensicEvent Event;
			Event.m_Type = NewState == EAnalysisState::ACTIVE ? EImpactForensicType::PROCESSING_RESUMED : EImpactForensicType::PROCESSING_PAUSED;
			Event.m_Tick = Tick;
			Event.m_TeamId = m_Context.m_TeamId;
			Event.m_RunStateBefore = PreviousState;
			Event.m_RunStateAfter = NewState;
			Event.m_ActiveParticipantCount = ActiveParticipantCount();
			Event.m_PresentParticipantCount = PresentParticipantCount();
			Event.m_HistoricalParticipantCount = static_cast<int>(m_vParticipants.size());
			Event.m_ReasonDetail = NewState == EAnalysisState::ACTIVE ? "active roster quorum restored" : "active roster quorum lost";
			AppendForensicEvent(std::move(Event));
		}
	}

	void CImpactRun::AppendForensicEvent(SImpactForensicEvent Event)
	{
		if(!m_ForensicTraceEnabled)
			return;
		if(m_vForensicEvents.size() >= MAX_FORENSIC_EVENTS)
		{
			m_ForensicTraceTruncated = true;
			++m_ForensicDroppedEvents;
			return;
		}
		Event.m_Sequence = m_NextForensicSequence++;
		Event.m_RunId = m_RunId;
		if(Event.m_TeamId == 0)
			Event.m_TeamId = m_Context.m_TeamId;
		if(Event.m_Tick < 0)
			Event.m_Tick = m_LastStateTick;
		if(Event.m_ActiveParticipantCount < 0)
			Event.m_ActiveParticipantCount = ActiveParticipantCount();
		if(Event.m_PresentParticipantCount < 0)
			Event.m_PresentParticipantCount = PresentParticipantCount();
		if(Event.m_HistoricalParticipantCount < 0)
			Event.m_HistoricalParticipantCount = static_cast<int>(m_vParticipants.size());
		if(Event.m_RunStateBefore == EAnalysisState::WAITING && Event.m_RunStateAfter == EAnalysisState::WAITING && m_State != EAnalysisState::WAITING)
			Event.m_RunStateBefore = Event.m_RunStateAfter = m_State;
		Event.m_ReasonDetail.resize(std::min(Event.m_ReasonDetail.size(), MAX_FORENSIC_DETAIL_LENGTH));
		if(Event.m_vFunctionalRoster.size() > MAX_PARTICIPANTS)
			Event.m_vFunctionalRoster.resize(MAX_PARTICIPANTS);
		for(SImpactFunctionalRosterEntry &Entry : Event.m_vFunctionalRoster)
			Entry.m_Reason.resize(std::min(Entry.m_Reason.size(), MAX_EVENT_REASON_LENGTH));

		if(Event.m_ActorInteractionId == 0 && Event.m_Actor.IsValid() && Event.m_Target.IsValid())
		{
			for(auto It = m_vForensicEvents.rbegin(); It != m_vForensicEvents.rend(); ++It)
			{
				if(It->m_Type == EImpactForensicType::INTERACTION_OBSERVED &&
					It->m_Actor == Event.m_Actor && It->m_Target == Event.m_Target &&
					It->m_TeamId == Event.m_TeamId &&
					(It->m_ProblemEpisodeId == Event.m_ProblemEpisodeId || Event.m_ProblemEpisodeId == 0) &&
					(It->m_Tick < 0 || Event.m_Tick < 0 || It->m_Tick <= Event.m_Tick))
				{
					Event.m_ActorInteractionId = It->m_ActorInteractionId;
					break;
				}
			}
		}
		m_vForensicEvents.push_back(std::move(Event));
	}

	void CImpactRun::AppendForensicEvents(const std::vector<SImpactForensicEvent> &vEvents, bool OutputTruncated, uint64_t DroppedEvents)
	{
		if(!m_ForensicTraceEnabled)
			return;
		for(const SImpactForensicEvent &Event : vEvents)
			AppendForensicEvent(Event);
		if(OutputTruncated)
		{
			m_ForensicTraceTruncated = true;
			m_ForensicDroppedEvents += std::max<uint64_t>(1, DroppedEvents);
		}
	}

	void CImpactRun::UpdateStateForRoster(int64_t Tick)
	{
		const int ActiveRosterCount = ActiveParticipantCount();
		if(m_State == EAnalysisState::ACTIVE && ActiveRosterCount < 2)
			Transition(EAnalysisState::PAUSED_NO_TEAM, Tick);
		else if((m_State == EAnalysisState::ANALYZING || m_State == EAnalysisState::PAUSED_NO_TEAM) && ActiveRosterCount >= 2)
			Transition(EAnalysisState::ACTIVE, Tick);
	}

	bool CImpactRun::AddRosterMember(const SParticipantId &Id, const char *pName, int64_t Tick, const char *pReason)
	{
		if(!IsInitialized() || IsFinalized() || !Id.IsValid() || m_vParticipants.size() >= MAX_PARTICIPANTS || FindParticipant(Id) != nullptr)
			return false;
		SParticipant Participant;
		Participant.m_Id = Id;
		if(pName != nullptr)
			Participant.m_Name = std::string(pName).substr(0, MAX_CONTEXT_LENGTH);
		Participant.m_Late = m_State == EAnalysisState::ACTIVE || m_State == EAnalysisState::PAUSED_NO_TEAM;
		Participant.m_Present = true;
		Participant.m_ParticipationTicksKnown = true;
		if(m_AnalysisStartedTick >= 0)
		{
			Participant.m_PersonalAnalysisStartTick = Tick;
			Participant.m_PersonalAnalysisStartActiveTicks = m_ActiveAnalysisTicks;
		}
		m_vParticipants.push_back(std::move(Participant));
		SImpactForensicEvent Forensic;
		Forensic.m_Type = EImpactForensicType::ROSTER_MEMBER_ADDED;
		Forensic.m_Tick = Tick;
		Forensic.m_Target = Id;
		Forensic.m_DecisionAccepted = true;
		Forensic.m_ReasonDetail = pReason == nullptr ? "confirmed current team roster member" : pReason;
		AppendForensicEvent(std::move(Forensic));
		UpdateStateForRoster(Tick);
		return true;
	}

	bool CImpactRun::ProveParticipantEngaged(const SParticipantId &Id, int64_t Tick, const char *pReason)
	{
		SParticipant *pParticipant = FindParticipant(Id);
		if(pParticipant == nullptr || pParticipant->m_Finalized || pParticipant->m_Engaged)
			return false;
		// A confirmed current-run event is itself positive presence evidence. This
		// also recovers engagement across a transient roster/snapshot gap.
		pParticipant->m_Present = true;
		pParticipant->m_Engaged = true;
		pParticipant->m_EngagedTick = Tick;
		if(Tick >= 0 && pParticipant->m_ParticipationTerminalTick < 0)
		{
			pParticipant->m_ParticipationTicksKnown = true;
			pParticipant->m_ParticipationStartTick = Tick;
		}
		else
		{
			pParticipant->m_ParticipationTicksKnown = false;
			pParticipant->m_ParticipationStartTick = -1;
		}
		SImpactForensicEvent Forensic;
		Forensic.m_Type = EImpactForensicType::ENGAGED_PROVEN;
		Forensic.m_Tick = Tick;
		Forensic.m_Target = Id;
		Forensic.m_DecisionAccepted = true;
		Forensic.m_ReasonDetail = pReason == nullptr ? "positive current-run participation evidence" : pReason;
		AppendForensicEvent(std::move(Forensic));
		UpdateStateForRoster(Tick);
		return true;
	}

	bool CImpactRun::ProveEngagementFromEvent(const SImpactEvent &Event)
	{
		if(!Event.m_Confirmed || Event.m_TeamId != m_Context.m_TeamId || IsFinalized())
			return false;
		const int64_t Tick = Event.m_ResolveTick >= 0 ? Event.m_ResolveTick : Event.m_StartTick;
		if(Event.m_Type == EImpactEventType::START_CROSSING)
		{
			if(!Event.m_Actor.IsValid())
				return false;
			if(!HasParticipant(Event.m_Actor))
				AddRosterMember(Event.m_Actor, Event.m_ActorName.c_str(), Tick, "canonical START participant");
			return ProveParticipantEngaged(Event.m_Actor, Tick, "canonical observed START crossing");
		}
		if(!IsGameplayImpactEvent(Event.m_Type))
			return false;
		const char *pReason = ImpactEventTypeName(Event.m_Type);
		bool Changed = false;
		if(Event.m_Actor.IsValid())
			Changed = ProveParticipantEngaged(Event.m_Actor, Tick, pReason) || Changed;
		if(Event.m_Target.IsValid())
			Changed = ProveParticipantEngaged(Event.m_Target, Tick, pReason) || Changed;
		return Changed;
	}

	void CImpactRun::CloseParticipantParticipation(SParticipant &Participant, int64_t Tick)
	{
		if(Participant.m_ParticipationStartTick >= 0)
		{
			if(Tick < Participant.m_ParticipationStartTick || !Participant.m_ParticipationTicksKnown)
				Participant.m_ParticipationTicksKnown = false;
			else
			{
				const int64_t Elapsed = Tick - Participant.m_ParticipationStartTick;
				if(Participant.m_ParticipationTicks > std::numeric_limits<int64_t>::max() - Elapsed)
					Participant.m_ParticipationTicks = std::numeric_limits<int64_t>::max();
				else
					Participant.m_ParticipationTicks += Elapsed;
			}
		}
		Participant.m_ParticipationStartTick = -1;
		Participant.m_ParticipationTerminalTick = Tick >= 0 ? Tick : -1;
	}

	void CImpactRun::AppendScoringEvidence(const SImpactEvent &Event, const SImpactForensicEvent &Forensic, const std::vector<SImpactEvent> &AcceptedBatch)
	{
		if(m_ScoringEvidenceVersion != IMPACT_SCORING_EVIDENCE_VERSION)
			return;
		if(Event.m_Type != EImpactEventType::SAVE_CONFIRMED &&
			Event.m_Type != EImpactEventType::FALL_CONFIRMED &&
			Event.m_Type != EImpactEventType::ALED_CONFIRMED)
			return;
		if(m_vScoringEvidence.size() >= MAX_SCORING_EVIDENCE_EVENTS)
		{
			m_ScoringEvidenceComplete = false;
			++m_ScoringEvidenceDroppedEvents;
			return;
		}
		SImpactScoringEvidence Evidence;
		Evidence.m_RunId = m_RunId;
		Evidence.m_EventId = Event.m_EventId;
		Evidence.m_Type = Event.m_Type;
		Evidence.m_Tick = Event.m_ResolveTick >= 0 ? Event.m_ResolveTick : Event.m_StartTick;
		Evidence.m_Actor = Event.m_Actor;
		Evidence.m_Target = Event.m_Target;
		Evidence.m_ProblemEpisodeId = Forensic.m_ProblemEpisodeId;
		Evidence.m_FallEpisodeId = Forensic.m_FallEpisodeId;
		Evidence.m_RescueEpisodeId = Forensic.m_SaveCandidateId;
		Evidence.m_ClutchEpisodeId = Forensic.m_ClutchEpisodeId;
		Evidence.m_ActorInteractionId = Forensic.m_ActorInteractionId;
		Evidence.m_IsCanonicalSave = Event.m_RawDelta.m_Saves > 0 &&
			(Event.m_Type == EImpactEventType::SAVE_CONFIRMED || Event.m_Type == EImpactEventType::ALED_CONFIRMED);
		if(Evidence.m_IsCanonicalSave)
			Evidence.m_LinkedSaveEventId = Event.m_EventId;
		else if(Event.m_Type == EImpactEventType::ALED_CONFIRMED && Event.m_RawDelta.m_Aleds > 0)
		{
			// The production ALED side-car copies the confirmed Save's actor,
			// target and resolved interval but has an ALED-only raw delta. Link it
			// to that exact accepted Save from this same canonical batch; ambiguity
			// deliberately leaves the relation unknown.
			for(const SImpactEvent &Candidate : AcceptedBatch)
			{
				if(Candidate.m_Type != EImpactEventType::SAVE_CONFIRMED || Candidate.m_RawDelta.m_Saves <= 0 ||
					Candidate.m_EventId == 0 || Candidate.m_Actor != Event.m_Actor || Candidate.m_Target != Event.m_Target ||
					Candidate.m_StartTick != Event.m_StartTick || Candidate.m_ResolveTick != Event.m_ResolveTick)
					continue;
				if(Evidence.m_LinkedSaveEventId != 0)
				{
					Evidence.m_LinkedSaveEventId = 0;
					break;
				}
				Evidence.m_LinkedSaveEventId = Candidate.m_EventId;
			}
		}
		Evidence.m_ClutchConfirmed = Evidence.m_IsCanonicalSave && Event.m_RawDelta.m_Clutches > 0;
		Evidence.m_AledConfirmed = Event.m_Type == EImpactEventType::ALED_CONFIRMED && Event.m_RawDelta.m_Aleds > 0;
		m_vScoringEvidence.push_back(std::move(Evidence));
	}

	static void AddCounter(int &Target, int Delta)
	{
		const int64_t Sum = static_cast<int64_t>(Target) + Delta;
		Target = static_cast<int>(std::clamp<int64_t>(Sum, 0, INT_MAX));
	}

	static bool IsGameplayImpactEvent(EImpactEventType Type)
	{
		switch(Type)
		{
		case EImpactEventType::START_CROSSING:
		case EImpactEventType::PARTICIPANT_PRESENCE:
		case EImpactEventType::PARTICIPANT_FINALIZED:
		case EImpactEventType::FINISH_CONFIRMED:
		case EImpactEventType::WIPE_CONFIRMED:
		case EImpactEventType::TEAM_RESET_CONFIRMED:
		case EImpactEventType::MAP_CHANGE:
		case EImpactEventType::SERVER_CHANGE:
		case EImpactEventType::CONNECTION_LOST:
			return false;
		default:
			// Unknown/new event types are gameplay by default, so they cannot
			// accidentally bypass the minimum-active-participant gate.
			return true;
		}
	}

	void CImpactRun::ApplyRawDelta(SParticipant *pParticipant, const SRawStats &Delta)
	{
		AddCounter(m_RawStats.m_Falls, Delta.m_Falls);
		AddCounter(m_RawStats.m_Wipes, Delta.m_Wipes);
		AddCounter(m_RawStats.m_Saves, Delta.m_Saves);
		AddCounter(m_RawStats.m_UsefulSaves, Delta.m_UsefulSaves);
		AddCounter(m_RawStats.m_MeaningfulSkips, Delta.m_MeaningfulSkips);
		AddCounter(m_RawStats.m_BackRecoveries, Delta.m_BackRecoveries);
		AddCounter(m_RawStats.m_HarmfulEvents, Delta.m_HarmfulEvents);
		AddCounter(m_RawStats.m_PositiveEvents, Delta.m_PositiveEvents);
		AddCounter(m_RawStats.m_Aleds, Delta.m_Aleds);
		AddCounter(m_RawStats.m_UpDowns, Delta.m_UpDowns);
		AddCounter(m_RawStats.m_Clutches, Delta.m_Clutches);
		AddCounter(m_RawStats.m_MissedCatches, Delta.m_MissedCatches);
		AddCounter(m_RawStats.m_TeamMistakes, Delta.m_TeamMistakes);
		AddCounter(m_RawStats.m_PreventedProgressLoss, Delta.m_PreventedProgressLoss);
		AddCounter(m_RawStats.m_RecoveryDifficultyIncreases, Delta.m_RecoveryDifficultyIncreases);
		if(pParticipant != nullptr)
		{
			AddCounter(pParticipant->m_RawStats.m_Falls, Delta.m_Falls);
			AddCounter(pParticipant->m_RawStats.m_Wipes, Delta.m_Wipes);
			AddCounter(pParticipant->m_RawStats.m_Saves, Delta.m_Saves);
			AddCounter(pParticipant->m_RawStats.m_UsefulSaves, Delta.m_UsefulSaves);
			AddCounter(pParticipant->m_RawStats.m_MeaningfulSkips, Delta.m_MeaningfulSkips);
			AddCounter(pParticipant->m_RawStats.m_BackRecoveries, Delta.m_BackRecoveries);
			AddCounter(pParticipant->m_RawStats.m_HarmfulEvents, Delta.m_HarmfulEvents);
			AddCounter(pParticipant->m_RawStats.m_PositiveEvents, Delta.m_PositiveEvents);
			AddCounter(pParticipant->m_RawStats.m_Aleds, Delta.m_Aleds);
			AddCounter(pParticipant->m_RawStats.m_UpDowns, Delta.m_UpDowns);
			AddCounter(pParticipant->m_RawStats.m_Clutches, Delta.m_Clutches);
			AddCounter(pParticipant->m_RawStats.m_MissedCatches, Delta.m_MissedCatches);
			AddCounter(pParticipant->m_RawStats.m_TeamMistakes, Delta.m_TeamMistakes);
			AddCounter(pParticipant->m_RawStats.m_PreventedProgressLoss, Delta.m_PreventedProgressLoss);
			AddCounter(pParticipant->m_RawStats.m_RecoveryDifficultyIncreases, Delta.m_RecoveryDifficultyIncreases);
		}
	}

	bool CImpactRun::ApplyCanonicalEvent(const SImpactEvent &Event)
	{
		if(!IsInitialized() || IsFinalized())
			return false;
		if(!Event.m_Confirmed || m_Context.m_TeamId <= 0 ||
			(Event.m_TeamId != 0 && Event.m_TeamId != m_Context.m_TeamId))
			return false;
		if(Event.m_Type == EImpactEventType::START_CROSSING &&
			(!Event.m_Actor.IsValid() || Event.m_TeamId != m_Context.m_TeamId))
			return false;
		if((Event.m_Type == EImpactEventType::PARTICIPANT_PRESENCE || Event.m_Type == EImpactEventType::PARTICIPANT_FINALIZED) && !Event.m_Actor.IsValid())
			return false;
		SParticipant *pActor = FindParticipant(Event.m_Actor);
		if(Event.m_Type == EImpactEventType::PARTICIPANT_FINALIZED && (pActor == nullptr || pActor->m_Finalized))
			return false;
		if(pActor != nullptr && pActor->m_Finalized)
			return false;
		if(Event.m_Target.IsValid())
		{
			const SParticipant *pTarget = FindParticipant(Event.m_Target);
			if(pTarget != nullptr && pTarget->m_Finalized)
				return false;
		}
		m_LastConfirmedTick = std::max(m_LastConfirmedTick, Event.m_ResolveTick);
		if(Event.m_Type != EImpactEventType::PARTICIPANT_FINALIZED)
			AddCounter(m_RawStats.m_ConfirmedEvents, 1);
		// A Fall is a victim result, while Save/UpDown/ALED ownership belongs to
		// the actor. Keep the event's causal actor intact, but apply victim-scoped
		// Fall counters to the target participant.
		SParticipant *pRawParticipant = pActor;
		if(Event.m_Type == EImpactEventType::FALL_CONFIRMED && Event.m_Target.IsValid())
			pRawParticipant = FindParticipant(Event.m_Target);
		if(Event.m_Type != EImpactEventType::PARTICIPANT_FINALIZED)
			ApplyRawDelta(pRawParticipant, Event.m_RawDelta);

		switch(Event.m_Type)
		{
		case EImpactEventType::START_CROSSING:
			if(!Event.m_Actor.IsValid())
				return false;
			if(pActor == nullptr)
			{
				if(!AddRosterMember(Event.m_Actor, Event.m_ActorName.c_str(), Event.m_ResolveTick, "canonical START participant"))
					return false;
				pActor = FindParticipant(Event.m_Actor);
			}
			else if(!Event.m_ActorName.empty())
			{
				pActor->m_Name = Event.m_ActorName.substr(0, MAX_CONTEXT_LENGTH);
			}
			if(pActor->m_FirstStartTick < 0)
			{
				pActor->m_FirstStartTick = Event.m_StartTick >= 0 ? Event.m_StartTick : Event.m_ResolveTick;
				if(m_AnalysisStartedTick >= 0)
				{
					pActor->m_PersonalAnalysisStartTick = Event.m_ResolveTick;
					pActor->m_PersonalAnalysisStartActiveTicks = ActiveAnalysisTicks(Event.m_ResolveTick);
				}
			}
			pActor->m_Present = true;
			ProveParticipantEngaged(Event.m_Actor, Event.m_ResolveTick, "canonical observed START crossing");

			if(m_RunStartedTick < 0)
				m_RunStartedTick = Event.m_StartTick;
			if(m_State == EAnalysisState::WAITING)
			{
				m_State = EAnalysisState::ANALYZING;
				m_LastStateTick = Event.m_ResolveTick;
			}
			if((m_State == EAnalysisState::ANALYZING || m_State == EAnalysisState::PAUSED_NO_TEAM) && ActiveParticipantCount() >= 2)
				Transition(EAnalysisState::ACTIVE, Event.m_ResolveTick);
			return true;

		case EImpactEventType::PARTICIPANT_PRESENCE:
			if(pActor == nullptr || pActor->m_Finalized)
				return false;
			pActor->m_Present = Event.m_Present;
			UpdateStateForRoster(Event.m_ResolveTick);
			return true;

		case EImpactEventType::PARTICIPANT_FINALIZED:
			if(pActor == nullptr)
				return false;
			{
				const EAnalysisState PreviousState = m_State;
				const bool WasEngaged = pActor->m_Engaged;
				if(WasEngaged && Event.m_CausalReason == "participant absent from authoritative server roster")
					pActor->m_TerminalStatus = EParticipantTerminalStatus::LEFT;
				else if(Event.m_CausalReason == "confirmed participant self-reset" ||
					Event.m_CausalReason == "participant absent from authoritative server roster")
					pActor->m_TerminalStatus = EParticipantTerminalStatus::OTHER;
				else
					pActor->m_TerminalStatus = EParticipantTerminalStatus::UNKNOWN;
				CloseParticipantParticipation(*pActor, Event.m_ResolveTick);
				pActor->m_Finalized = true;
			pActor->m_Present = false;
			UpdateStateForRoster(Event.m_ResolveTick);
			SImpactForensicEvent Forensic;
			Forensic.m_Type = EImpactForensicType::PARTICIPANT_FINALIZED;
			Forensic.m_EventId = Event.m_EventId;
			Forensic.m_Tick = Event.m_ResolveTick;
			Forensic.m_Actor = Event.m_Actor;
			Forensic.m_ReasonDetail = Event.m_CausalReason;
			Forensic.m_RunStateBefore = PreviousState;
			Forensic.m_RunStateAfter = m_State;
			AppendForensicEvent(std::move(Forensic));
			SImpactForensicEvent EngagementFinalized;
			EngagementFinalized.m_Type = EImpactForensicType::ENGAGED_FINALIZED;
			EngagementFinalized.m_Tick = Event.m_ResolveTick;
			EngagementFinalized.m_Actor = Event.m_Actor;
			EngagementFinalized.m_DecisionAccepted = true;
			EngagementFinalized.m_ReasonDetail = WasEngaged ? "engaged participant finalized" : "unengaged roster participant finalized";
			EngagementFinalized.m_RunStateBefore = PreviousState;
			EngagementFinalized.m_RunStateAfter = m_State;
			AppendForensicEvent(std::move(EngagementFinalized));

				// Score the participant from the exact state closed by this canonical
				// lifecycle event. The cut exists only as an immutable input snapshot;
				// the shared run remains active for teammates.
				SRunSnapshot ParticipantBoundary = Snapshot(Event.m_ResolveTick);
				ParticipantBoundary.m_State = EAnalysisState::FINALIZED;
				ParticipantBoundary.m_Result = ERunResult::TEAM_RESET;
				ParticipantBoundary.m_EndTick = Event.m_ResolveTick;
				ParticipantBoundary.m_LastStateTick = Event.m_ResolveTick;
				FreezeFinalScoreSnapshots(ParticipantBoundary, &Event.m_Actor);
				const auto FrozenParticipant = std::find_if(ParticipantBoundary.m_vParticipants.begin(), ParticipantBoundary.m_vParticipants.end(), [&Event](const SParticipant &Participant) {
					return Participant.m_Id == Event.m_Actor;
				});
				if(FrozenParticipant != ParticipantBoundary.m_vParticipants.end())
					pActor->m_Score = FrozenParticipant->m_Score;
				return true;
			}

		case EImpactEventType::SAVE_CONFIRMED:
			// This hidden metric records only the already accepted canonical Save
			// relation. It does not alter the saver-owned raw counter or event result.
			if(Event.m_Target.IsValid() && Event.m_Target != Event.m_Actor)
			{
				if(SParticipant *pSavedParticipant = FindParticipant(Event.m_Target))
					AddCounter(pSavedParticipant->m_RescuesReceived, 1);
				else
					for(SParticipant &Participant : m_vParticipants)
						Participant.m_RescuesReceivedKnown = false;
			}
			else
				for(SParticipant &Participant : m_vParticipants)
					Participant.m_RescuesReceivedKnown = false;
			return true;

		case EImpactEventType::FALL_CONFIRMED:
		case EImpactEventType::ALED_CONFIRMED:
		case EImpactEventType::UPDOWN_CONFIRMED:
		case EImpactEventType::MISSED_CATCH_CONFIRMED:
		case EImpactEventType::TEAM_MISTAKE_CONFIRMED:
		case EImpactEventType::TEAM_HARM_CONFIRMED:
		case EImpactEventType::PREVENTED_PROGRESS_LOSS_CONFIRMED:
		case EImpactEventType::RECOVERY_DIFFICULTY_INCREASE_CONFIRMED:
		case EImpactEventType::JUMP_RESET_CONFIRMED:
			return true;

		case EImpactEventType::FINISH_CONFIRMED:
		{
			const bool Finalized = Finalize(ERunResult::FINISHED, Event.m_ResolveTick);
			if(Finalized && Event.m_AuthoritativeFinishTimeMillis >= 0)
			{
				m_AuthoritativeFinishTimeMillis = Event.m_AuthoritativeFinishTimeMillis;
				// Keep the snapshot's legacy duration field aligned with the same
				// authoritative team time. The dedicated millisecond field preserves
				// the exact value for UI/history formatting.
				m_ActiveAnalysisTicks = static_cast<int64_t>(m_AuthoritativeFinishTimeMillis) * m_Context.m_TickSpeed / 1000;
			}
			return Finalized;
		}
		case EImpactEventType::WIPE_CONFIRMED:
			return Finalize(ERunResult::WIPED, Event.m_ResolveTick);
		case EImpactEventType::TEAM_RESET_CONFIRMED:
			return Finalize(ERunResult::TEAM_RESET, Event.m_ResolveTick);
		case EImpactEventType::MAP_CHANGE:
			return Finalize(ERunResult::ABORTED_MAP_CHANGE, Event.m_ResolveTick);
		case EImpactEventType::SERVER_CHANGE:
			return Finalize(ERunResult::ABORTED_SERVER_CHANGE, Event.m_ResolveTick);
		case EImpactEventType::CONNECTION_LOST:
			return Finalize(ERunResult::ABORTED_CONNECTION, Event.m_ResolveTick);
		}
		return false;
	}

	bool CImpactRun::ApplyTickEventBatch(const STickEventBatch &Batch)
	{
		if(!IsInitialized() || IsFinalized())
			return false;
		STickEventBatch ScopedBatch;
		ScopedBatch.m_ServerTick = Batch.m_ServerTick;
		ScopedBatch.m_vCandidates.reserve(Batch.m_vCandidates.size());
		for(const SImpactEvent &Candidate : Batch.m_vCandidates)
		{
			if((Candidate.m_TeamId != 0 && Candidate.m_TeamId != m_Context.m_TeamId) ||
				(Candidate.m_Type == EImpactEventType::START_CROSSING &&
					(m_Context.m_TeamId <= 0 || Candidate.m_TeamId != m_Context.m_TeamId)))
				continue;
			if(Candidate.m_Type != EImpactEventType::PARTICIPANT_FINALIZED && IsParticipantFinalized(Candidate.m_Actor))
				continue;
			if(Candidate.m_Target.IsValid() && IsParticipantFinalized(Candidate.m_Target))
				continue;
			if(Candidate.m_Type == EImpactEventType::PARTICIPANT_FINALIZED && IsParticipantFinalized(Candidate.m_Actor))
				continue;
			ScopedBatch.m_vCandidates.push_back(Candidate);
		}
		const std::vector<SImpactEvent> vPotentiallyAccepted = m_EventLedger.Resolve(ScopedBatch);
		std::unordered_set<uint64_t> SeenEventIds;
		for(const SImpactEvent &Candidate : vPotentiallyAccepted)
		{
			if(m_EventLedger.Contains(Candidate.m_EventId) || !SeenEventIds.insert(Candidate.m_EventId).second)
				continue;
			ProveEngagementFromEvent(Candidate);
		}
		int ProjectedActiveParticipantCount = ActiveParticipantCount();
		std::vector<SParticipantId> vFinalizingParticipants;
		for(const SImpactEvent &Candidate : ScopedBatch.m_vCandidates)
		{
			if(Candidate.m_Type != EImpactEventType::PARTICIPANT_FINALIZED ||
				std::find(vFinalizingParticipants.begin(), vFinalizingParticipants.end(), Candidate.m_Actor) != vFinalizingParticipants.end())
				continue;
			const SParticipant *pParticipant = FindParticipant(Candidate.m_Actor);
			if(pParticipant != nullptr && !pParticipant->m_Finalized)
			{
				vFinalizingParticipants.push_back(Candidate.m_Actor);
				--ProjectedActiveParticipantCount;
			}
		}
		const bool GameplayProcessingAllowed = IsGameplayProcessingAllowed(m_State, ProjectedActiveParticipantCount);
		if(!GameplayProcessingAllowed)
		{
			ScopedBatch.m_vCandidates.erase(std::remove_if(ScopedBatch.m_vCandidates.begin(), ScopedBatch.m_vCandidates.end(), [](const SImpactEvent &Candidate) {
				return IsGameplayImpactEvent(Candidate.m_Type);
			}), ScopedBatch.m_vCandidates.end());
			// Keep lifecycle/result transitions, but do not let their optional raw
			// deltas smuggle gameplay counters through while processing is paused.
			for(SImpactEvent &Candidate : ScopedBatch.m_vCandidates)
				Candidate.m_RawDelta = {};
		}
		if(ScopedBatch.m_vCandidates.empty())
			return false;
		std::vector<SImpactEvent> vAccepted;
		if(!m_EventLedger.Apply(ScopedBatch, &vAccepted))
			return false;
		bool Changed = false;
		for(const SImpactEvent &Event : vAccepted)
		{
			const EAnalysisState PreviousState = m_State;
			const bool Applied = ApplyCanonicalEvent(Event);
			Changed = Applied || Changed;
			if(!Applied)
				continue;
			SImpactForensicEvent Forensic;
			Forensic.m_Type = EImpactForensicType::CANONICAL_EVENT;
			Forensic.m_EventId = Event.m_EventId;
			Forensic.m_Tick = Event.m_ResolveTick;
			Forensic.m_TeamId = Event.m_TeamId;
			Forensic.m_Actor = Event.m_Actor;
			Forensic.m_Target = Event.m_Target;
			Forensic.m_RunStateBefore = PreviousState;
			Forensic.m_RunStateAfter = m_State;
			Forensic.m_EpisodeStateBefore = Event.m_EpisodeState;
			Forensic.m_EpisodeStateAfter = Event.m_EpisodeState;
			Forensic.m_ReasonDetail = ImpactEventTypeName(Event.m_Type);
			Forensic.m_RawDelta = Event.m_RawDelta;
			for(auto It = m_vForensicEvents.rbegin(); It != m_vForensicEvents.rend(); ++It)
			{
				if(It->m_EventId != Event.m_EventId)
					continue;
				Forensic.m_ProblemEpisodeId = It->m_ProblemEpisodeId;
				Forensic.m_FallEpisodeId = It->m_FallEpisodeId;
				Forensic.m_SaveCandidateId = It->m_SaveCandidateId;
				Forensic.m_ClutchEpisodeId = It->m_ClutchEpisodeId;
				Forensic.m_ActorInteractionId = It->m_ActorInteractionId;
				break;
			}
			if(m_ForensicTraceEnabled)
				AppendForensicEvent(Forensic);
			AppendScoringEvidence(Event, Forensic, vAccepted);
		}
		return Changed;
	}

	bool CImpactRun::Finalize(ERunResult Result, int64_t Tick)
	{
		if(!IsInitialized() || IsFinalized() || !IsTerminalResult(Result))
			return false;
		if(m_State == EAnalysisState::WAITING)
			return false;
		if(m_State == EAnalysisState::ACTIVE && Tick >= m_LastStateTick)
			m_ActiveAnalysisTicks += Tick - m_LastStateTick;
		const EAnalysisState PreviousState = m_State;
		const int ActiveBeforeBoundary = ActiveParticipantCount();
		m_LastStateTick = Tick;
		m_LastConfirmedTick = std::max(m_LastConfirmedTick, Tick);
		m_Result = Result;
		m_State = EAnalysisState::FINALIZED;
		m_EndTick = Tick;
		SImpactForensicEvent Boundary;
		Boundary.m_Type = EImpactForensicType::TEAM_BOUNDARY;
		Boundary.m_Tick = Tick;
		Boundary.m_TeamId = m_Context.m_TeamId;
		Boundary.m_ReasonDetail = RunResultName(Result);
		Boundary.m_RunStateBefore = PreviousState;
		Boundary.m_RunStateAfter = EAnalysisState::FINALIZED;
		Boundary.m_ActiveParticipantCount = ActiveBeforeBoundary;
		Boundary.m_HistoricalParticipantCount = static_cast<int>(m_vParticipants.size());
		AppendForensicEvent(std::move(Boundary));
		for(SParticipant &Participant : m_vParticipants)
		{
			const bool WasEngaged = Participant.m_Engaged;
			if(!Participant.m_Finalized)
				Participant.m_TerminalStatus = Result == ERunResult::FINISHED ? EParticipantTerminalStatus::FINISHED : EParticipantTerminalStatus::OTHER;
			else if(Participant.m_TerminalStatus == EParticipantTerminalStatus::NOT_FINALIZED)
				Participant.m_TerminalStatus = EParticipantTerminalStatus::UNKNOWN;
			if(Participant.m_ParticipationTerminalTick < 0)
				CloseParticipantParticipation(Participant, Tick);
			Participant.m_Finalized = true;
			Participant.m_Present = false;
			SImpactForensicEvent Finalized;
			Finalized.m_Type = EImpactForensicType::PARTICIPANT_FINALIZED;
			Finalized.m_Tick = Tick;
			Finalized.m_Actor = Participant.m_Id;
			Finalized.m_ReasonDetail = "terminal team boundary: " + std::string(RunResultName(Result));
			Finalized.m_RunStateBefore = PreviousState;
			Finalized.m_RunStateAfter = EAnalysisState::FINALIZED;
			Finalized.m_ActiveParticipantCount = 0;
			Finalized.m_HistoricalParticipantCount = static_cast<int>(m_vParticipants.size());
			AppendForensicEvent(std::move(Finalized));
			SImpactForensicEvent EngagementFinalized;
			EngagementFinalized.m_Type = EImpactForensicType::ENGAGED_FINALIZED;
			EngagementFinalized.m_Tick = Tick;
			EngagementFinalized.m_Actor = Participant.m_Id;
			EngagementFinalized.m_DecisionAccepted = true;
			EngagementFinalized.m_ReasonDetail = WasEngaged ? "engaged participant finalized at team boundary" : "unengaged roster participant finalized at team boundary";
			EngagementFinalized.m_RunStateBefore = PreviousState;
			EngagementFinalized.m_RunStateAfter = EAnalysisState::FINALIZED;
			AppendForensicEvent(std::move(EngagementFinalized));
		}
		SRunSnapshot FinalScoreSnapshot = Snapshot(Tick);
		FreezeFinalScoreSnapshots(FinalScoreSnapshot);
		CopyFrozenParticipantScores(FinalScoreSnapshot, m_vParticipants);
		return true;
	}

	bool CImpactRun::RestoreSnapshot(const SRunSnapshot &SnapshotData)
	{
		if(SnapshotData.m_SchemaVersion != IMPACT_SCHEMA_VERSION ||
			SnapshotData.m_AlgorithmVersion != IMPACT_ALGORITHM_VERSION ||
			SnapshotData.m_RunId == 0 ||
			(SnapshotData.m_HasGlobalRunNumber && SnapshotData.m_GlobalRunNumber == 0) ||
			SnapshotData.m_vParticipants.size() > MAX_PARTICIPANTS ||
			SnapshotData.m_vEvents.size() > MAX_LEDGER_EVENTS ||
			SnapshotData.m_ScoringEvidenceVersion < 0 || SnapshotData.m_ScoringEvidenceVersion > IMPACT_SCORING_EVIDENCE_VERSION ||
			SnapshotData.m_vScoringEvidence.size() > MAX_SCORING_EVIDENCE_EVENTS ||
			!IsScoringEvidenceConsistent(SnapshotData) ||
			(SnapshotData.m_ScoringEvidenceVersion == 0 && (!SnapshotData.m_vScoringEvidence.empty() || SnapshotData.m_ScoringEvidenceComplete || SnapshotData.m_ScoringEvidenceDroppedEvents != 0)) ||
			(SnapshotData.m_ScoringEvidenceDroppedEvents > 0 && SnapshotData.m_ScoringEvidenceComplete) ||
			SnapshotData.m_vPendingEpisodes.size() > MAX_PENDING_EPISODES ||
			SnapshotData.m_vForensicEvents.size() > MAX_FORENSIC_EVENTS ||
			(SnapshotData.m_ForensicVersion != 0 && SnapshotData.m_ForensicVersion != IMPACT_FORENSIC_LEGACY_VERSION && SnapshotData.m_ForensicVersion != IMPACT_FORENSIC_VERSION) ||
			(SnapshotData.m_ForensicVersion == 0 && (!SnapshotData.m_vForensicEvents.empty() || SnapshotData.m_ForensicTraceEnabled || SnapshotData.m_ForensicTraceComplete || SnapshotData.m_ForensicTraceTruncated || SnapshotData.m_ForensicDroppedEvents != 0)) ||
			(SnapshotData.m_ForensicDroppedEvents > 0 && !SnapshotData.m_ForensicTraceTruncated) ||
			SnapshotData.m_Context.m_TickSpeed <= 0 || SnapshotData.m_Context.m_TickSpeed > 1000 ||
			(SnapshotData.m_State != EAnalysisState::FINALIZED && SnapshotData.m_Context.m_TeamId <= 0) ||
			(SnapshotData.m_State == EAnalysisState::FINALIZED) != IsTerminalResult(SnapshotData.m_Result))
			return false;
		if(!SnapshotData.m_RunUid.empty())
		{
			CUuid RunUid;
			if(SnapshotData.m_RunUid.size() != UUID_MAXSTRSIZE - 1 || ParseUuid(&RunUid, SnapshotData.m_RunUid.c_str()) != 0)
				return false;
		}

		for(size_t i = 0; i < SnapshotData.m_vParticipants.size(); ++i)
		{
			if(!SnapshotData.m_vParticipants[i].m_Id.IsValid() ||
				static_cast<int>(SnapshotData.m_vParticipants[i].m_TerminalStatus) < static_cast<int>(EParticipantTerminalStatus::NOT_FINALIZED) ||
				static_cast<int>(SnapshotData.m_vParticipants[i].m_TerminalStatus) > static_cast<int>(EParticipantTerminalStatus::OTHER) ||
				SnapshotData.m_vParticipants[i].m_ParticipationTicks < 0 ||
				(SnapshotData.m_vParticipants[i].m_ParticipationStartTick >= 0 &&
					(!SnapshotData.m_vParticipants[i].m_ParticipationTicksKnown || !SnapshotData.m_vParticipants[i].m_Engaged ||
						SnapshotData.m_vParticipants[i].m_Finalized || SnapshotData.m_vParticipants[i].m_ParticipationTerminalTick >= 0)) ||
				(SnapshotData.m_vParticipants[i].m_ParticipationTerminalTick >= 0 && !SnapshotData.m_vParticipants[i].m_Finalized))
				return false;
			for(size_t j = 0; j < i; ++j)
				if(SnapshotData.m_vParticipants[i].m_Id == SnapshotData.m_vParticipants[j].m_Id)
					return false;
		}
		std::unordered_set<uint64_t> ScoringEvidenceEventIds;
		for(const SImpactScoringEvidence &Evidence : SnapshotData.m_vScoringEvidence)
		{
			if(SnapshotData.m_ScoringEvidenceVersion != IMPACT_SCORING_EVIDENCE_VERSION ||
				Evidence.m_RunId != SnapshotData.m_RunId || Evidence.m_EventId == 0 ||
				!ScoringEvidenceEventIds.insert(Evidence.m_EventId).second || Evidence.m_Tick < -1 ||
				(Evidence.m_Type != EImpactEventType::SAVE_CONFIRMED && Evidence.m_Type != EImpactEventType::FALL_CONFIRMED && Evidence.m_Type != EImpactEventType::ALED_CONFIRMED) ||
				(Evidence.m_IsCanonicalSave && Evidence.m_LinkedSaveEventId != Evidence.m_EventId) ||
				(!Evidence.m_IsCanonicalSave && Evidence.m_LinkedSaveEventId == Evidence.m_EventId) ||
				(Evidence.m_ClutchConfirmed && !Evidence.m_IsCanonicalSave) ||
				(Evidence.m_AledConfirmed && Evidence.m_Type != EImpactEventType::ALED_CONFIRMED))
				return false;
		}
		uint64_t PreviousForensicSequence = 0;
		for(const SImpactForensicEvent &Event : SnapshotData.m_vForensicEvents)
		{
			if(Event.m_Sequence == 0 || Event.m_Sequence <= PreviousForensicSequence || Event.m_RunId != SnapshotData.m_RunId ||
				static_cast<int>(Event.m_Type) < 0 || static_cast<int>(Event.m_Type) > static_cast<int>(SnapshotData.m_ForensicVersion >= IMPACT_FORENSIC_VERSION ? EImpactForensicType::ENGAGED_FINALIZED : EImpactForensicType::CLUTCH_ROSTER_EVALUATED) ||
				static_cast<int>(Event.m_Reason) < 0 || static_cast<int>(Event.m_Reason) > static_cast<int>(EImpactForensicReason::EXISTING_GENERIC_REJECT) ||
				Event.m_ReasonDetail.size() > MAX_FORENSIC_DETAIL_LENGTH || Event.m_vFunctionalRoster.size() > MAX_PARTICIPANTS)
				return false;
			PreviousForensicSequence = Event.m_Sequence;
			for(const SImpactFunctionalRosterEntry &Entry : Event.m_vFunctionalRoster)
				if(!Entry.m_Participant.IsValid() || Entry.m_Reason.size() > MAX_EVENT_REASON_LENGTH ||
					static_cast<int>(Entry.m_Classification) < 0 || static_cast<int>(Entry.m_Classification) > static_cast<int>(EImpactFunctionalClass::EXCLUDED))
					return false;
		}

		CEventLedger RestoredLedger;
		if(!RestoredLedger.Restore(SnapshotData.m_vEvents))
			return false;

		Clear();
		m_RunId = SnapshotData.m_RunId;
		m_RunUid = SnapshotData.m_RunUid.empty() ? NewRunUid() : SnapshotData.m_RunUid;
		m_HasGlobalRunNumber = SnapshotData.m_HasGlobalRunNumber;
		m_GlobalRunNumber = SnapshotData.m_HasGlobalRunNumber ? SnapshotData.m_GlobalRunNumber : 0;
		CopyContextString(m_Context.m_ServerKey, SnapshotData.m_Context.m_ServerKey);
		CopyContextString(m_Context.m_ServerName, SnapshotData.m_Context.m_ServerName);
		CopyContextString(m_Context.m_CommunityId, SnapshotData.m_Context.m_CommunityId);
		CopyContextString(m_Context.m_Map, SnapshotData.m_Context.m_Map);
		CopyContextString(m_Context.m_MapDifficulty, SnapshotData.m_Context.m_MapDifficulty);
		m_Context.m_MapStars = SnapshotData.m_Context.m_MapStars >= 0 && SnapshotData.m_Context.m_MapStars <= MAX_MAP_DIFFICULTY_STARS ? SnapshotData.m_Context.m_MapStars : -1;
		m_Context.m_TickSpeed = SnapshotData.m_Context.m_TickSpeed;
		m_Context.m_TeamId = SnapshotData.m_Context.m_TeamId;
		m_State = SnapshotData.m_State;
		m_Result = SnapshotData.m_Result;
		m_RunStartedTick = SnapshotData.m_RunStartedTick;
		m_AnalysisStartedTick = SnapshotData.m_AnalysisStartedTick;
		m_LastConfirmedTick = SnapshotData.m_LastConfirmedTick;
		m_LastStateTick = SnapshotData.m_LastStateTick;
		m_ActiveAnalysisTicks = std::max<int64_t>(0, SnapshotData.m_ActiveAnalysisTicks);
		m_EndTick = SnapshotData.m_EndTick;
		if(SnapshotData.m_AuthoritativeFinishTimeMillis < -1)
		{
			Clear();
			return false;
		}
		m_AuthoritativeFinishTimeMillis = SnapshotData.m_AuthoritativeFinishTimeMillis;
		m_RawStats = SnapshotData.m_RawStats;
		m_Score = SnapshotData.m_Score;
		m_vParticipants = SnapshotData.m_vParticipants;
		for(SParticipant &Participant : m_vParticipants)
			if(Participant.m_Finalized && Participant.m_TerminalStatus == EParticipantTerminalStatus::NOT_FINALIZED)
				Participant.m_TerminalStatus = EParticipantTerminalStatus::UNKNOWN;
		if(SnapshotData.m_ForensicVersion < IMPACT_FORENSIC_VERSION)
		{
			for(SParticipant &Participant : m_vParticipants)
			{
				Participant.m_Engaged = Participant.m_FirstStartTick >= 0;
				Participant.m_EngagedTick = Participant.m_Engaged ? Participant.m_FirstStartTick : -1;
			}
		}
		m_ForensicTraceEnabled = SnapshotData.m_ForensicVersion == 0 || SnapshotData.m_ForensicTraceEnabled;
		m_ForensicTraceComplete = SnapshotData.m_ForensicVersion != 0 && SnapshotData.m_ForensicTraceComplete;
		m_ForensicTraceTruncated = SnapshotData.m_ForensicTraceTruncated;
		m_ForensicDroppedEvents = SnapshotData.m_ForensicDroppedEvents;
		m_vForensicEvents = SnapshotData.m_vForensicEvents;
		m_ScoringEvidenceVersion = IMPACT_SCORING_EVIDENCE_VERSION;
		m_ScoringEvidenceComplete = SnapshotData.m_ScoringEvidenceVersion == IMPACT_SCORING_EVIDENCE_VERSION && SnapshotData.m_ScoringEvidenceComplete;
		m_ScoringEvidenceDroppedEvents = SnapshotData.m_ScoringEvidenceDroppedEvents;
		m_vScoringEvidence = SnapshotData.m_vScoringEvidence;
		m_NextForensicSequence = m_vForensicEvents.empty() ? 1 : m_vForensicEvents.back().m_Sequence + 1;
		for(SParticipant &Participant : m_vParticipants)
			Participant.m_Name.resize(std::min(Participant.m_Name.size(), MAX_CONTEXT_LENGTH));
		for(const SImpactPendingEpisode &Episode : SnapshotData.m_vPendingEpisodes)
		{
			if(Episode.m_EpisodeId == 0 || !Episode.m_Target.IsValid() ||
				(Episode.m_TeamId != 0 && Episode.m_TeamId != m_Context.m_TeamId))
			{
				Clear();
				return false;
			}
			for(const SImpactPendingEpisode &Existing : m_vPendingEpisodes)
				if(Existing.m_EpisodeId == Episode.m_EpisodeId)
				{
					Clear();
					return false;
				}
			SImpactPendingEpisode Normalized = Episode;
			NormalizePendingEpisode(Normalized);
			m_vPendingEpisodes.push_back(std::move(Normalized));
		}
		m_EventLedger = std::move(RestoredLedger);
		return true;
	}

	SRunSnapshot CImpactRun::Snapshot(int64_t CurrentTick, int64_t SavedAt) const
	{
		SRunSnapshot ResultSnapshot;
		ResultSnapshot.m_RunId = m_RunId;
		ResultSnapshot.m_RunUid = m_RunUid;
		ResultSnapshot.m_HasGlobalRunNumber = m_HasGlobalRunNumber;
		ResultSnapshot.m_GlobalRunNumber = m_GlobalRunNumber;
		ResultSnapshot.m_Context = m_Context;
		ResultSnapshot.m_State = m_State;
		ResultSnapshot.m_Result = m_Result;
		ResultSnapshot.m_RunStartedTick = m_RunStartedTick;
		ResultSnapshot.m_AnalysisStartedTick = m_AnalysisStartedTick;
		ResultSnapshot.m_LastConfirmedTick = m_LastConfirmedTick;
		ResultSnapshot.m_LastStateTick = m_LastStateTick;
		ResultSnapshot.m_ActiveAnalysisTicks = m_ActiveAnalysisTicks;
		ResultSnapshot.m_EndTick = m_EndTick;
		ResultSnapshot.m_AuthoritativeFinishTimeMillis = m_AuthoritativeFinishTimeMillis;
		// A checkpoint stores a self-contained time baseline. Without moving the
		// active baseline to the checkpoint tick, RestoreSnapshot would count the
		// already persisted ACTIVE interval a second time after a restart.
		if(m_State == EAnalysisState::ACTIVE && CurrentTick >= m_LastStateTick)
		{
			ResultSnapshot.m_ActiveAnalysisTicks += CurrentTick - m_LastStateTick;
			ResultSnapshot.m_LastStateTick = CurrentTick;
		}
		ResultSnapshot.m_SavedAt = SavedAt;
		ResultSnapshot.m_vParticipants = m_vParticipants;
		ResultSnapshot.m_RawStats = m_RawStats;
		ResultSnapshot.m_Score = m_Score;
		ResultSnapshot.m_vEvents = m_EventLedger.Events();
		ResultSnapshot.m_ScoringEvidenceVersion = m_ScoringEvidenceVersion;
		ResultSnapshot.m_ScoringEvidenceComplete = m_ScoringEvidenceComplete;
		ResultSnapshot.m_ScoringEvidenceDroppedEvents = m_ScoringEvidenceDroppedEvents;
		ResultSnapshot.m_vScoringEvidence = m_vScoringEvidence;
		ResultSnapshot.m_vPendingEpisodes = m_vPendingEpisodes;
		ResultSnapshot.m_ForensicVersion = IMPACT_FORENSIC_VERSION;
		ResultSnapshot.m_ForensicTraceEnabled = m_ForensicTraceEnabled;
		ResultSnapshot.m_ForensicTraceComplete = m_ForensicTraceComplete;
		ResultSnapshot.m_ForensicTraceTruncated = m_ForensicTraceTruncated;
		ResultSnapshot.m_ForensicDroppedEvents = m_ForensicDroppedEvents;
		ResultSnapshot.m_vForensicEvents = m_vForensicEvents;
		return ResultSnapshot;
	}

	bool CImpactRun::MakeParticipantResetSnapshot(const SParticipantId &Participant, uint64_t SnapshotRunId, int64_t CurrentTick, int64_t SavedAt, SRunSnapshot *pSnapshot) const
	{
		if(pSnapshot == nullptr || SnapshotRunId == 0 || IsFinalized() || m_State == EAnalysisState::WAITING || !IsParticipantFinalized(Participant))
			return false;
		SRunSnapshot SnapshotData = Snapshot(CurrentTick, SavedAt);
		SnapshotData.m_RunId = SnapshotRunId;
		// A participant-reset archive is a distinct immutable history record.
		SnapshotData.m_RunUid = NewRunUid();
		SnapshotData.m_HasGlobalRunNumber = false;
		SnapshotData.m_GlobalRunNumber = 0;
		SnapshotData.m_State = EAnalysisState::FINALIZED;
		SnapshotData.m_Result = ERunResult::TEAM_RESET;
		SnapshotData.m_LastStateTick = CurrentTick;
		SnapshotData.m_LastConfirmedTick = std::max(SnapshotData.m_LastConfirmedTick, CurrentTick);
		SnapshotData.m_EndTick = CurrentTick;
		for(SImpactForensicEvent &Event : SnapshotData.m_vForensicEvents)
			Event.m_RunId = SnapshotRunId;
		for(SImpactScoringEvidence &Evidence : SnapshotData.m_vScoringEvidence)
			Evidence.m_RunId = SnapshotRunId;
		*pSnapshot = std::move(SnapshotData);
		return true;
	}

	void CImpactRun::SetPendingEpisodes(const std::vector<SImpactPendingEpisode> &vEpisodes)
	{
		m_vPendingEpisodes.clear();
		m_vPendingEpisodes.reserve(std::min(vEpisodes.size(), MAX_PENDING_EPISODES));
		for(const SImpactPendingEpisode &Episode : vEpisodes)
		{
			if(Episode.m_EpisodeId == 0 || !Episode.m_Target.IsValid() ||
				(Episode.m_TeamId != 0 && Episode.m_TeamId != m_Context.m_TeamId) ||
				m_vPendingEpisodes.size() >= MAX_PENDING_EPISODES)
				continue;
			bool Duplicate = false;
			for(const SImpactPendingEpisode &Existing : m_vPendingEpisodes)
				Duplicate |= Existing.m_EpisodeId == Episode.m_EpisodeId;
			if(Duplicate)
				continue;
			SImpactPendingEpisode Normalized = Episode;
			NormalizePendingEpisode(Normalized);
			m_vPendingEpisodes.push_back(std::move(Normalized));
		}
	}

	bool CImpactRun::UpdateParticipantName(const SParticipantId &Id, const char *pName)
	{
		SParticipant *pParticipant = FindParticipant(Id);
		if(pParticipant == nullptr || pName == nullptr)
			return false;
		const std::string Name = std::string(pName).substr(0, MAX_CONTEXT_LENGTH);
		if(pParticipant->m_Name == Name)
			return false;
		pParticipant->m_Name = Name;
		return true;
	}

	bool CImpactRun::ParticipantPresent(const SParticipantId &Id, bool *pPresent) const
	{
		const SParticipant *pParticipant = FindParticipant(Id);
		if(pParticipant == nullptr || pPresent == nullptr)
			return false;
		*pPresent = pParticipant->m_Present;
		return true;
	}

	bool CImpactRun::HasPassedDisplayGate(const SParticipantId &Id, int64_t CurrentTick) const
	{
		const SParticipant *pParticipant = FindParticipant(Id);
		if(pParticipant == nullptr || pParticipant->m_PersonalAnalysisStartActiveTicks < 0 || m_Context.m_TickSpeed <= 0)
			return false;
		const int64_t ElapsedActiveTicks = ActiveAnalysisTicks(CurrentTick) - pParticipant->m_PersonalAnalysisStartActiveTicks;
		return ElapsedActiveTicks >= static_cast<int64_t>(SHORT_RUN_SECONDS) * m_Context.m_TickSpeed;
	}

	int64_t CImpactRun::ActiveAnalysisTicks(int64_t CurrentTick) const
	{
		int64_t Ticks = m_ActiveAnalysisTicks;
		if(m_State == EAnalysisState::ACTIVE && CurrentTick >= m_LastStateTick)
			Ticks += CurrentTick - m_LastStateTick;
		return std::max<int64_t>(0, Ticks);
	}

	int CImpactRun::ActiveAnalysisSeconds(int64_t CurrentTick) const
	{
		return static_cast<int>(ActiveAnalysisTicks(CurrentTick) / std::max(1, m_Context.m_TickSpeed));
	}

	bool CImpactRun::IsShortRun(int64_t CurrentTick) const
	{
		return ActiveAnalysisSeconds(CurrentTick) < SHORT_RUN_SECONDS;
	}

	int64_t CImpactRun::TeamRaceTicks(int64_t CurrentTick) const
	{
		if(m_AuthoritativeFinishTimeMillis >= 0)
			return std::max<int64_t>(0, static_cast<int64_t>(m_AuthoritativeFinishTimeMillis) * std::max(1, m_Context.m_TickSpeed) / 1000);
		const int64_t EndTick = IsFinalized() ? m_LastStateTick : CurrentTick;
		if(m_RunStartedTick < 0 || EndTick < m_RunStartedTick)
			return 0;
		return EndTick - m_RunStartedTick;
	}

	int CImpactRun::TeamRaceSeconds(int64_t CurrentTick) const
	{
		return static_cast<int>(TeamRaceTicks(CurrentTick) / std::max(1, m_Context.m_TickSpeed));
	}
}
