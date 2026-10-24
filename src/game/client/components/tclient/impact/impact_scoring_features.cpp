/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_scoring_features.h"

#include <algorithm>
#include <locale>
#include <sstream>

namespace AmfImpact
{
	namespace
	{
		const char *TerminalStatusName(EParticipantTerminalStatus Status)
		{
			switch(Status)
			{
			case EParticipantTerminalStatus::NOT_FINALIZED: return "NOT_FINALIZED";
			case EParticipantTerminalStatus::FINISHED: return "FINISHED";
			case EParticipantTerminalStatus::SELF_RESET: return "SELF_RESET";
			case EParticipantTerminalStatus::LEFT: return "LEFT";
			case EParticipantTerminalStatus::RUN_ENDED: return "RUN_ENDED";
			case EParticipantTerminalStatus::UNKNOWN: return "UNKNOWN";
			case EParticipantTerminalStatus::OTHER: return "OTHER";
			}
			return "UNKNOWN";
		}

		EParticipantTerminalStatus EffectiveTerminalStatus(const SParticipant &Participant)
		{
			if(!Participant.m_Finalized)
				return EParticipantTerminalStatus::NOT_FINALIZED;
			return Participant.m_TerminalStatus == EParticipantTerminalStatus::NOT_FINALIZED ?
				EParticipantTerminalStatus::UNKNOWN : Participant.m_TerminalStatus;
		}

		bool IsNonNegativeCanonicalStat(const SParticipant &Participant)
		{
			return Participant.m_RawStats.m_Saves >= 0 && Participant.m_RawStats.m_Falls >= 0 &&
				Participant.m_RawStats.m_Aleds >= 0 && Participant.m_RawStats.m_Clutches >= 0 &&
				Participant.m_RescuesReceived >= 0 && Participant.m_ParticipationTicks >= 0;
		}

		EPhase4EvidenceStatus GetEvidenceStatus(const SRunSnapshot &Snapshot)
		{
			if(Snapshot.m_ScoringEvidenceVersion == 0)
			{
				return Snapshot.m_vScoringEvidence.empty() && !Snapshot.m_ScoringEvidenceComplete &&
					Snapshot.m_ScoringEvidenceDroppedEvents == 0 ?
					EPhase4EvidenceStatus::UNAVAILABLE : EPhase4EvidenceStatus::INVALID;
			}
			if(Snapshot.m_ScoringEvidenceVersion < 0)
				return EPhase4EvidenceStatus::INVALID;
			if(Snapshot.m_ScoringEvidenceVersion > IMPACT_SCORING_EVIDENCE_VERSION)
				return EPhase4EvidenceStatus::UNSUPPORTED_VERSION;
			if(!IsScoringEvidenceConsistent(Snapshot) ||
				(Snapshot.m_ScoringEvidenceDroppedEvents > 0 && Snapshot.m_ScoringEvidenceComplete))
				return EPhase4EvidenceStatus::INVALID;
			if(Snapshot.m_ScoringEvidenceVersion != IMPACT_SCORING_EVIDENCE_VERSION)
				return EPhase4EvidenceStatus::UNSUPPORTED_VERSION;
			return Snapshot.m_ScoringEvidenceComplete && Snapshot.m_ScoringEvidenceDroppedEvents == 0 ?
				EPhase4EvidenceStatus::COMPLETE : EPhase4EvidenceStatus::INCOMPLETE;
		}

		void AddCount(int64_t &Total, int64_t Value)
		{
			Total += Value;
		}

		std::string SafeDiagnosticName(const std::string &Name)
		{
			std::string Result = Name;
			for(char &Character : Result)
				if(Character == '\r' || Character == '\n' || Character == '\t')
					Character = ' ';
			return Result;
		}
	}

	const char *Phase4EvidenceStatusName(EPhase4EvidenceStatus Status)
	{
		switch(Status)
		{
		case EPhase4EvidenceStatus::UNAVAILABLE: return "UNAVAILABLE";
		case EPhase4EvidenceStatus::COMPLETE: return "COMPLETE";
		case EPhase4EvidenceStatus::INCOMPLETE: return "INCOMPLETE";
		case EPhase4EvidenceStatus::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
		case EPhase4EvidenceStatus::INVALID: return "INVALID";
		}
		return "INVALID";
	}

	const char *Phase4FeatureSupportName(EPhase4FeatureSupport Support)
	{
		switch(Support)
		{
		case EPhase4FeatureSupport::SUPPORTED: return "SUPPORTED";
		case EPhase4FeatureSupport::UNSUPPORTED: return "UNSUPPORTED";
		}
		return "UNSUPPORTED";
	}

	bool BuildPhase4ScoringFeatures(const SRunSnapshot &Snapshot, const SPhase4ScoringAggregationOptions &Options, CRunScoringContext *pFeatures)
	{
		if(pFeatures == nullptr)
			return false;
		*pFeatures = {};
		if(Snapshot.m_State != EAnalysisState::FINALIZED || !IsTerminalResult(Snapshot.m_Result) || Snapshot.m_RunId == 0 ||
			Snapshot.m_Context.m_TickSpeed <= 0 || Snapshot.m_Context.m_TickSpeed > 1000 ||
			Snapshot.m_vParticipants.size() > MAX_PARTICIPANTS)
			return false;

		CRunScoringContext Result;
		Result.m_RunId = Snapshot.m_RunId;
		Result.m_RunUid = Snapshot.m_RunUid;
		Result.m_HasGlobalRunNumber = Snapshot.m_HasGlobalRunNumber;
		Result.m_GlobalRunNumber = Snapshot.m_HasGlobalRunNumber ? Snapshot.m_GlobalRunNumber : 0;
		Result.m_TeamId = Snapshot.m_Context.m_TeamId;
		Result.m_ServerKey = Snapshot.m_Context.m_ServerKey;
		Result.m_Map = Snapshot.m_Context.m_Map;
		Result.m_Result = Snapshot.m_Result;
		Result.m_EndTick = Snapshot.m_EndTick;
		Result.m_TickSpeed = Snapshot.m_Context.m_TickSpeed;
		Result.m_MaxRepeatedSaveSeparationTicks = Options.m_MaxRepeatedSaveSeparationTicks;
		Result.m_RosterParticipantCount = Snapshot.m_vParticipants.size();
		Result.m_ScoringEvidenceVersion = Snapshot.m_ScoringEvidenceVersion;
		Result.m_ScoringEvidenceDroppedEvents = Snapshot.m_ScoringEvidenceDroppedEvents;
		Result.m_ScoringEvidenceStatus = GetEvidenceStatus(Snapshot);
		Result.m_ProvenRescueAssistSupport = EPhase4FeatureSupport::UNSUPPORTED;

		for(size_t i = 0; i < Snapshot.m_vParticipants.size(); ++i)
		{
			const SParticipant &Participant = Snapshot.m_vParticipants[i];
			if(!Participant.m_Id.IsValid() || !IsNonNegativeCanonicalStat(Participant))
				return false;
			for(size_t j = 0; j < i; ++j)
				if(Snapshot.m_vParticipants[j].m_Id == Participant.m_Id)
					return false;

			if(!Participant.m_Engaged)
			{
				++Result.m_NotEngagedRosterParticipantCount;
				continue;
			}

			SPlayerScoringFeatures Player;
			Player.m_Id = Participant.m_Id;
			Player.m_Name = Participant.m_Name;
			Player.m_TerminalStatus = EffectiveTerminalStatus(Participant);
			Player.m_Engaged = true;
			Player.m_Finalized = Participant.m_Finalized;
			Player.m_InFinalizedScoringCohort = Participant.m_Finalized;
			Player.m_Saves = Participant.m_RawStats.m_Saves;
			Player.m_Falls = Participant.m_RawStats.m_Falls;
			Player.m_Aleds = Participant.m_RawStats.m_Aleds;
			Player.m_Clutches = Participant.m_RawStats.m_Clutches;
			Player.m_RescuesReceived = Participant.m_RescuesReceived;
			Player.m_RescuesReceivedKnown = Participant.m_RescuesReceivedKnown;
			Player.m_ProvenRescueAssistSupport = EPhase4FeatureSupport::UNSUPPORTED;
			Player.m_ProvenRescueAssistsKnown = false;

			Player.m_ParticipationKnown = Participant.m_Finalized && Participant.m_ParticipationTicksKnown &&
				Participant.m_ParticipationStartTick < 0 && Participant.m_ParticipationTerminalTick >= 0;
			if(Player.m_ParticipationKnown)
				Player.m_ParticipationTicks = Participant.m_ParticipationTicks;

			Result.m_vPlayers.push_back(std::move(Player));
		}

		std::sort(Result.m_vPlayers.begin(), Result.m_vPlayers.end(), [](const SPlayerScoringFeatures &Left, const SPlayerScoringFeatures &Right) {
			return ParticipantIdLess(Left.m_Id, Right.m_Id);
		});

		for(SPlayerScoringFeatures &Player : Result.m_vPlayers)
		{
			if(!Player.m_InFinalizedScoringCohort)
				continue;
			++Result.m_FinalizedEngagedParticipantCount;
			AddCount(Result.m_TotalSaves, Player.m_Saves);
			AddCount(Result.m_TotalFalls, Player.m_Falls);
			AddCount(Result.m_TotalAleds, Player.m_Aleds);
			AddCount(Result.m_TotalClutches, Player.m_Clutches);
			if(Player.m_RescuesReceivedKnown)
			{
				AddCount(Result.m_TotalKnownRescuesReceived, Player.m_RescuesReceived);
				++Result.m_RescuesReceivedKnownParticipantCount;
			}
			else
				++Result.m_RescuesReceivedUnknownParticipantCount;
			if(Player.m_ParticipationKnown)
			{
				AddCount(Result.m_TotalKnownParticipationTicks, Player.m_ParticipationTicks);
				++Result.m_ParticipationKnownParticipantCount;
			}
			else
				++Result.m_ParticipationUnknownParticipantCount;
		}

		Result.m_TotalRescuesReceivedKnown = Result.m_FinalizedEngagedParticipantCount > 0 && Result.m_RescuesReceivedUnknownParticipantCount == 0;
		Result.m_ParticipationSampleComplete = Result.m_FinalizedEngagedParticipantCount > 0 && Result.m_ParticipationUnknownParticipantCount == 0;

		std::vector<std::vector<const SImpactScoringEvidence *>> vSavesByPlayer(Result.m_vPlayers.size());
		bool SaveEvidenceCoverageComplete = Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::COMPLETE;
		if(Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::COMPLETE || Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::INCOMPLETE)
		{
			for(const SImpactScoringEvidence &Evidence : Snapshot.m_vScoringEvidence)
			{
				if(!Evidence.m_IsCanonicalSave || Evidence.m_LinkedSaveEventId != Evidence.m_EventId)
					continue;
				auto PlayerIt = std::find_if(Result.m_vPlayers.begin(), Result.m_vPlayers.end(), [&Evidence](const SPlayerScoringFeatures &Player) {
					return Player.m_Id == Evidence.m_Actor;
				});
				if(PlayerIt == Result.m_vPlayers.end())
				{
					SaveEvidenceCoverageComplete = false;
					continue;
				}
				const size_t PlayerIndex = static_cast<size_t>(PlayerIt - Result.m_vPlayers.begin());
				vSavesByPlayer[PlayerIndex].push_back(&Evidence);
			}
		}
		else
			SaveEvidenceCoverageComplete = false;

		for(size_t i = 0; i < Result.m_vPlayers.size(); ++i)
		{
			SPlayerScoringFeatures &Player = Result.m_vPlayers[i];
			if(!Player.m_InFinalizedScoringCohort)
			{
				Player.m_UnknownSavePatternCount = Player.m_Saves;
				SaveEvidenceCoverageComplete &= Player.m_Saves == 0;
				continue;
			}

			std::vector<const SImpactScoringEvidence *> &vSaveEvidence = vSavesByPlayer[i];
			std::sort(vSaveEvidence.begin(), vSaveEvidence.end(), [](const SImpactScoringEvidence *pLeft, const SImpactScoringEvidence *pRight) {
				if(pLeft->m_Tick != pRight->m_Tick)
					return pLeft->m_Tick < pRight->m_Tick;
				return pLeft->m_EventId < pRight->m_EventId;
			});
			const bool PlayerEvidenceCountMatches = static_cast<int64_t>(vSaveEvidence.size()) == Player.m_Saves;
			SaveEvidenceCoverageComplete &= PlayerEvidenceCountMatches;
			Player.m_UnknownSavePatternCount = Player.m_Saves;
			const bool CompleteSaveSequence = Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::COMPLETE && PlayerEvidenceCountMatches;
			const SImpactScoringEvidence *pPrevious = nullptr;
			for(const SImpactScoringEvidence *pEvidence : vSaveEvidence)
			{
				if(pEvidence == nullptr || !pEvidence->m_Actor.IsValid() || !pEvidence->m_Target.IsValid() || pEvidence->m_Tick < 0)
				{
					pPrevious = pEvidence;
					continue;
				}
				ERepeatedContextClass Classification = ERepeatedContextClass::UNKNOWN;
				if(pPrevious == nullptr)
					Classification = CompleteSaveSequence ? ERepeatedContextClass::INDEPENDENT : ERepeatedContextClass::UNKNOWN;
				else
					Classification = ClassifyRepeatedSaveContext(*pPrevious, *pEvidence, Options.m_MaxRepeatedSaveSeparationTicks);

				// Missing/dropped scoring records can hide an intervening accepted Save,
				// so even otherwise matching IDs are not enough to classify this partial
				// sequence as repeated.
				if(!CompleteSaveSequence)
					Classification = ERepeatedContextClass::UNKNOWN;
				if(Classification == ERepeatedContextClass::INDEPENDENT)
				{
					++Player.m_IndependentSaveCount;
					--Player.m_UnknownSavePatternCount;
				}
				else if(Classification == ERepeatedContextClass::POSSIBLE_REPEATED_CONTEXT)
				{
					++Player.m_RepeatedContextSaveCount;
					--Player.m_UnknownSavePatternCount;
				}
				pPrevious = pEvidence;
			}
			if(Player.m_UnknownSavePatternCount < 0)
				return false;
			Player.m_SavePatternEvidenceCoverageComplete = PlayerEvidenceCountMatches && Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::COMPLETE;
			Player.m_SavePatternClassificationComplete = Player.m_SavePatternEvidenceCoverageComplete && Player.m_UnknownSavePatternCount == 0;
			if(Player.m_InFinalizedScoringCohort)
			{
				AddCount(Result.m_IndependentSaveCount, Player.m_IndependentSaveCount);
				AddCount(Result.m_RepeatedContextSaveCount, Player.m_RepeatedContextSaveCount);
				AddCount(Result.m_UnknownSavePatternCount, Player.m_UnknownSavePatternCount);
			}
		}
		Result.m_SavePatternEvidenceCoverageComplete = SaveEvidenceCoverageComplete &&
			Result.m_ScoringEvidenceStatus == EPhase4EvidenceStatus::COMPLETE;
		Result.m_SavePatternClassificationComplete = Result.m_SavePatternEvidenceCoverageComplete &&
			Result.m_UnknownSavePatternCount == 0;

		for(SPlayerScoringFeatures &Player : Result.m_vPlayers)
		{
			if(!Player.m_InFinalizedScoringCohort)
				continue;
			if(Result.m_TotalSaves > 0)
				Player.m_SavesToTeamSaves = {Player.m_Saves, Result.m_TotalSaves, true};
			if(Result.m_TotalFalls > 0)
				Player.m_FallsToTeamFalls = {Player.m_Falls, Result.m_TotalFalls, true};
		}

		Result.m_Available = true;
		*pFeatures = std::move(Result);
		return true;
	}

	std::string FormatPhase4ScoringFeatures(const CRunScoringContext &Features)
	{
		if(!Features.m_Available)
			return "Phase4 scoring features: UNAVAILABLE";
		std::ostringstream Output;
		Output.imbue(std::locale::classic());
		Output << "Phase4 run=" << Features.m_RunId << " uid=" << (Features.m_RunUid.empty() ? "UNKNOWN" : Features.m_RunUid)
			<< " team=" << Features.m_TeamId << " result=" << RunResultName(Features.m_Result)
			<< " map=" << Features.m_Map << " cohort=" << Features.m_FinalizedEngagedParticipantCount
			<< " roster=" << Features.m_RosterParticipantCount << " not_engaged=" << Features.m_NotEngagedRosterParticipantCount
			<< " totals[saves=" << Features.m_TotalSaves << ",falls=" << Features.m_TotalFalls
			<< ",aled=" << Features.m_TotalAleds << ",clutch=" << Features.m_TotalClutches << "]"
			<< " rescues_received=" << (Features.m_TotalRescuesReceivedKnown ? "known:" : "partial:") << Features.m_TotalKnownRescuesReceived
			<< " participation_ticks=" << (Features.m_ParticipationSampleComplete ? "complete:" : "partial:") << Features.m_TotalKnownParticipationTicks
			<< " evidence=" << Phase4EvidenceStatusName(Features.m_ScoringEvidenceStatus)
			<< " evidence_dropped=" << Features.m_ScoringEvidenceDroppedEvents
			<< " save_patterns[independent=" << Features.m_IndependentSaveCount << ",repeated=" << Features.m_RepeatedContextSaveCount
			<< ",unknown=" << Features.m_UnknownSavePatternCount << "]"
			<< " rescue_assists=" << Phase4FeatureSupportName(Features.m_ProvenRescueAssistSupport) << '\n';
		for(const SPlayerScoringFeatures &Player : Features.m_vPlayers)
		{
			Output << "player id=" << Player.m_Id.m_ClientId << ':' << Player.m_Id.m_SlotGeneration
				<< " name=\"" << SafeDiagnosticName(Player.m_Name) << "\" terminal=" << TerminalStatusName(Player.m_TerminalStatus)
				<< " engaged=" << (Player.m_Engaged ? "yes" : "no") << " finalized=" << (Player.m_Finalized ? "yes" : "no")
				<< " cohort=" << (Player.m_InFinalizedScoringCohort ? "yes" : "no")
				<< " participation=" << (Player.m_ParticipationKnown ? "known:" : "UNKNOWN:") << Player.m_ParticipationTicks
				<< " saves=" << Player.m_Saves << " saves_independent=" << Player.m_IndependentSaveCount
				<< " saves_repeated=" << Player.m_RepeatedContextSaveCount << " saves_unknown=" << Player.m_UnknownSavePatternCount
				<< " aleds=" << Player.m_Aleds << " clutches=" << Player.m_Clutches << " falls=" << Player.m_Falls
				<< " evidence=" << Phase4EvidenceStatusName(Features.m_ScoringEvidenceStatus)
				<< "/dropped=" << Features.m_ScoringEvidenceDroppedEvents
				<< " save_evidence_coverage=" << (Player.m_SavePatternEvidenceCoverageComplete ? "complete" : "incomplete")
				<< " rescues_received=" << (Player.m_RescuesReceivedKnown ? "known:" : "UNKNOWN:") << Player.m_RescuesReceived
				<< " proven_rescue_assists=" << Phase4FeatureSupportName(Player.m_ProvenRescueAssistSupport) << ":"
				<< (Player.m_ProvenRescueAssistsKnown ? std::to_string(Player.m_ProvenRescueAssists) : "UNKNOWN")
				<< " save_share=";
			if(Player.m_SavesToTeamSaves.m_Known)
				Output << Player.m_SavesToTeamSaves.m_Numerator << '/' << Player.m_SavesToTeamSaves.m_Denominator;
			else
				Output << "UNKNOWN";
			Output << " fall_share=";
			if(Player.m_FallsToTeamFalls.m_Known)
				Output << Player.m_FallsToTeamFalls.m_Numerator << '/' << Player.m_FallsToTeamFalls.m_Denominator;
			else
				Output << "UNKNOWN";
			Output << '\n';
		}
		return Output.str();
	}
}
