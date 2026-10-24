/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_stability.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace AmfImpact
{
	namespace
	{
		bool IsValidScoringParticipant(const SPlayerScoringFeatures &Player)
		{
			if(!Player.m_Id.IsValid() || !Player.m_Engaged || !Player.m_Finalized || !Player.m_InFinalizedScoringCohort ||
				Player.m_Saves < 0 || Player.m_Falls < 0 || Player.m_Aleds < 0 || Player.m_Clutches < 0 ||
				Player.m_RescuesReceived < 0 || Player.m_ParticipationTicks < 0 ||
				Player.m_IndependentSaveCount < 0 || Player.m_RepeatedContextSaveCount < 0 || Player.m_UnknownSavePatternCount < 0)
				return false;
			if(Player.m_IndependentSaveCount > Player.m_Saves || Player.m_RepeatedContextSaveCount > Player.m_Saves - Player.m_IndependentSaveCount)
				return false;
			return Player.m_UnknownSavePatternCount == Player.m_Saves - Player.m_IndependentSaveCount - Player.m_RepeatedContextSaveCount;
		}

		bool SameScoringParticipant(const SPlayerScoringFeatures &Left, const SPlayerScoringFeatures &Right)
		{
			return Left.m_Id == Right.m_Id && Left.m_TerminalStatus == Right.m_TerminalStatus &&
				Left.m_Engaged == Right.m_Engaged && Left.m_Finalized == Right.m_Finalized &&
				Left.m_InFinalizedScoringCohort == Right.m_InFinalizedScoringCohort &&
				Left.m_ParticipationTicks == Right.m_ParticipationTicks && Left.m_ParticipationKnown == Right.m_ParticipationKnown &&
				Left.m_Saves == Right.m_Saves && Left.m_Falls == Right.m_Falls && Left.m_Aleds == Right.m_Aleds &&
				Left.m_Clutches == Right.m_Clutches && Left.m_RescuesReceived == Right.m_RescuesReceived &&
				Left.m_RescuesReceivedKnown == Right.m_RescuesReceivedKnown &&
				Left.m_IndependentSaveCount == Right.m_IndependentSaveCount &&
				Left.m_RepeatedContextSaveCount == Right.m_RepeatedContextSaveCount &&
				Left.m_UnknownSavePatternCount == Right.m_UnknownSavePatternCount &&
				Left.m_SavePatternEvidenceCoverageComplete == Right.m_SavePatternEvidenceCoverageComplete;
		}

		double ParticipationConfidence(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context, SStabilityResult *pResult)
		{
			if(!Player.m_ParticipationKnown)
				return SStabilityV1Constants::m_ShortRunConfidenceFloor;

			pResult->m_ParticipationKnown = true;
			pResult->m_ParticipationTicks = Player.m_ParticipationTicks;
			pResult->m_ParticipationSeconds = static_cast<double>(Player.m_ParticipationTicks) / Context.m_TickSpeed;
			const double TimeFraction = std::clamp(pResult->m_ParticipationSeconds / SStabilityV1Constants::m_ParticipationFullConfidenceSeconds, 0.0, 1.0);
			return SStabilityV1Constants::m_ShortRunConfidenceFloor +
				(1.0 - SStabilityV1Constants::m_ShortRunConfidenceFloor) * std::sqrt(TimeFraction);
		}

		double EvidenceConfidence(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context)
		{
			switch(Context.m_ScoringEvidenceStatus)
			{
			case EPhase4EvidenceStatus::COMPLETE:
				if(Context.m_ScoringEvidenceDroppedEvents == 0)
					return 1.0;
				return SStabilityV1Constants::m_IncompleteEvidenceConfidence;
			case EPhase4EvidenceStatus::INCOMPLETE:
				return SStabilityV1Constants::m_IncompleteEvidenceConfidence;
			case EPhase4EvidenceStatus::UNAVAILABLE:
			case EPhase4EvidenceStatus::UNSUPPORTED_VERSION:
			case EPhase4EvidenceStatus::INVALID:
				return SStabilityV1Constants::m_UnknownEvidenceConfidence;
			}
			return SStabilityV1Constants::m_UnknownEvidenceConfidence;
		}

		double FallRatePerMinute(const SPlayerScoringFeatures &Player, int TickSpeed)
		{
			const double Minutes = static_cast<double>(Player.m_ParticipationTicks) / TickSpeed / 60.0;
			return static_cast<double>(Player.m_Falls) / Minutes;
		}

		double Median(std::vector<double> &Values)
		{
			std::sort(Values.begin(), Values.end());
			const size_t Middle = Values.size() / 2;
			if(Values.size() % 2 != 0)
				return Values[Middle];
			return Values[Middle - 1] / 2.0 + Values[Middle] / 2.0;
		}
	}

	bool CalculateStabilityV1(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context, SStabilityResult *pResult)
	{
		if(pResult == nullptr)
			return false;
		*pResult = {};
		if(!Context.m_Available || Context.m_TickSpeed <= 0 || !IsValidScoringParticipant(Player))
			return false;

		const SPlayerScoringFeatures *pContextPlayer = nullptr;
		for(const SPlayerScoringFeatures &Candidate : Context.m_vPlayers)
		{
			if(Candidate.m_Id != Player.m_Id)
				continue;
			if(pContextPlayer != nullptr)
				return false;
			pContextPlayer = &Candidate;
		}
		if(pContextPlayer == nullptr || !SameScoringParticipant(Player, *pContextPlayer) || !IsValidScoringParticipant(*pContextPlayer))
			return false;

		SStabilityResult Result;
		Result.m_Id = Player.m_Id;
		Result.m_EvidenceStatus = Context.m_ScoringEvidenceStatus;
		Result.m_EvidenceDroppedEvents = Context.m_ScoringEvidenceDroppedEvents;
		Result.m_CanonicalSaves = Player.m_Saves;
		Result.m_IndependentSaves = Player.m_IndependentSaveCount;
		Result.m_RepeatedLoopSaves = Player.m_RepeatedContextSaveCount;
		Result.m_UnknownPatternSaves = Player.m_UnknownSavePatternCount;
		Result.m_SavePatternEvidenceCoverageComplete = Player.m_SavePatternEvidenceCoverageComplete;
		Result.m_SavePatternClassificationComplete = Player.m_SavePatternClassificationComplete;
		Result.m_Aleds = Player.m_Aleds;
		Result.m_Clutches = Player.m_Clutches;
		Result.m_Falls = Player.m_Falls;
		Result.m_RescuesReceived = Player.m_RescuesReceived;
		Result.m_RescuesReceivedKnown = Player.m_RescuesReceivedKnown;

		Result.m_EffectiveSaveEvidence =
			static_cast<double>(Player.m_IndependentSaveCount) * SStabilityV1Constants::m_IndependentSaveEvidenceWeight +
			static_cast<double>(Player.m_RepeatedContextSaveCount) * SStabilityV1Constants::m_RepeatedLoopSaveEvidenceWeight +
			static_cast<double>(Player.m_UnknownSavePatternCount) * SStabilityV1Constants::m_UnknownPatternSaveEvidenceWeight;
		Result.m_PositiveEvidence = SStabilityV1Constants::m_SavePositiveWeight * Result.m_EffectiveSaveEvidence;
		Result.m_NegativeEvidence = SStabilityV1Constants::m_FallWeight * static_cast<double>(Player.m_Falls);

		const double RawDenominator = SStabilityV1Constants::m_BayesianPriorTotal + Result.m_PositiveEvidence + Result.m_NegativeEvidence;
		if(!std::isfinite(RawDenominator) || RawDenominator <= 0.0)
			return false;
		Result.m_RawStability = std::clamp(
			100.0 * (SStabilityV1Constants::m_BayesianPriorPositive + Result.m_PositiveEvidence) / RawDenominator,
			0.0, 100.0);

		Result.m_ParticipationConfidence = ParticipationConfidence(Player, Context, &Result);
		Result.m_EvidenceConfidence = EvidenceConfidence(Player, Context);
		Result.m_CombinedConfidence = std::clamp(Result.m_ParticipationConfidence * Result.m_EvidenceConfidence, 0.0, 1.0);
		Result.m_ConfidenceAdjustedStability = SStabilityV1Constants::m_Neutral +
			(Result.m_RawStability - SStabilityV1Constants::m_Neutral) * Result.m_CombinedConfidence;

		if(Player.m_ParticipationKnown && Player.m_ParticipationTicks > 0)
		{
			Result.m_PlayerFallRateKnown = true;
			Result.m_PlayerFallRatePerMinute = FallRatePerMinute(Player, Context.m_TickSpeed);
			std::vector<double> vCohortFallRates;
			vCohortFallRates.reserve(Context.m_vPlayers.size());
			for(const SPlayerScoringFeatures &CohortPlayer : Context.m_vPlayers)
			{
				if(!CohortPlayer.m_Engaged || !CohortPlayer.m_Finalized || !CohortPlayer.m_InFinalizedScoringCohort ||
					!CohortPlayer.m_ParticipationKnown || CohortPlayer.m_ParticipationTicks <= 0 || CohortPlayer.m_Falls < 0)
					continue;
				vCohortFallRates.push_back(FallRatePerMinute(CohortPlayer, Context.m_TickSpeed));
			}
			Result.m_PeerSampleCount = vCohortFallRates.size();
			if(vCohortFallRates.size() >= 2)
			{
				Result.m_CohortMedianFallRateKnown = true;
				Result.m_CohortMedianFallRatePerMinute = Median(vCohortFallRates);
				if(Result.m_CohortMedianFallRatePerMinute > 0.0)
				{
					if(Result.m_PlayerFallRatePerMinute == 0.0)
						Result.m_PeerAdjustment = SStabilityV1Constants::m_PeerAdjustmentMax;
					else
					{
						const double RatioSignal = std::log2(Result.m_CohortMedianFallRatePerMinute / Result.m_PlayerFallRatePerMinute);
					Result.m_PeerAdjustment = std::clamp(RatioSignal, -1.0, 1.0) * SStabilityV1Constants::m_PeerAdjustmentMax;
					}
				}
			}
		}

		Result.m_PeerAdjustmentApplied = Result.m_PeerAdjustment * Result.m_CombinedConfidence;
		Result.m_Value = std::clamp(Result.m_ConfidenceAdjustedStability + Result.m_PeerAdjustmentApplied, 0.0, 100.0);
		if(!std::isfinite(Result.m_Value) || !std::isfinite(Result.m_RawStability) || !std::isfinite(Result.m_CombinedConfidence))
			return false;
		Result.m_Available = true;
		*pResult = Result;
		return true;
	}

	std::string FormatStabilityV1Result(const SStabilityResult &Result)
	{
		if(!Result.m_Available)
			return "StabilityV1: UNAVAILABLE";
		std::ostringstream Output;
		Output.imbue(std::locale::classic());
		Output << std::setprecision(std::numeric_limits<double>::max_digits10)
			<< "StabilityV1 id=" << Result.m_Id.m_ClientId << ':' << Result.m_Id.m_SlotGeneration
			<< " value=" << Result.m_Value << " raw=" << Result.m_RawStability
			<< " canonical_saves=" << Result.m_CanonicalSaves << " independent_saves=" << Result.m_IndependentSaves
			<< " repeated_saves=" << Result.m_RepeatedLoopSaves << " unknown_pattern_saves=" << Result.m_UnknownPatternSaves
			<< " save_pattern_coverage=" << (Result.m_SavePatternEvidenceCoverageComplete ? "complete" : "incomplete")
			<< " save_pattern_classification=" << (Result.m_SavePatternClassificationComplete ? "complete" : "incomplete")
			<< " effective_save_evidence=" << Result.m_EffectiveSaveEvidence << " positive_evidence=" << Result.m_PositiveEvidence
			<< " falls=" << Result.m_Falls << " rescues_received=" << (Result.m_RescuesReceivedKnown ? "known:" : "UNKNOWN:") << Result.m_RescuesReceived
			<< " negative_evidence=" << Result.m_NegativeEvidence
			<< " participation=" << (Result.m_ParticipationKnown ? "known:" : "UNKNOWN:") << Result.m_ParticipationSeconds << "s"
			<< " participation_confidence=" << Result.m_ParticipationConfidence
			<< " evidence=" << Phase4EvidenceStatusName(Result.m_EvidenceStatus) << "/dropped=" << Result.m_EvidenceDroppedEvents
			<< " evidence_confidence=" << Result.m_EvidenceConfidence
			<< " combined_confidence=" << Result.m_CombinedConfidence
			<< " fall_rate=" << (Result.m_PlayerFallRateKnown ? "known:" : "UNKNOWN:") << Result.m_PlayerFallRatePerMinute
			<< " cohort_median_fall_rate=" << (Result.m_CohortMedianFallRateKnown ? "known:" : "UNKNOWN:") << Result.m_CohortMedianFallRatePerMinute
			<< " peer_samples=" << Result.m_PeerSampleCount << " peer_adjustment=" << Result.m_PeerAdjustment
			<< " peer_applied=" << Result.m_PeerAdjustmentApplied
			<< " confidence_adjusted=" << Result.m_ConfidenceAdjustedStability
			<< " aleds=" << Result.m_Aleds << " clutches=" << Result.m_Clutches;
		return Output.str();
	}
}
