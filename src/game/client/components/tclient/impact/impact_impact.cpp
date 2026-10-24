/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_impact.h"

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
		bool IsValidImpactParticipant(const SPlayerScoringFeatures &Player)
		{
			if(!Player.m_Id.IsValid() || !Player.m_Engaged || !Player.m_Finalized || !Player.m_InFinalizedScoringCohort ||
				Player.m_Saves < 0 || Player.m_Falls < 0 || Player.m_Aleds < 0 || Player.m_Clutches < 0 ||
				Player.m_RescuesReceived < 0 || Player.m_ParticipationTicks < 0 || Player.m_IndependentSaveCount < 0 ||
				Player.m_RepeatedContextSaveCount < 0 || Player.m_UnknownSavePatternCount < 0)
				return false;
			if(Player.m_IndependentSaveCount > Player.m_Saves ||
				Player.m_RepeatedContextSaveCount > Player.m_Saves - Player.m_IndependentSaveCount)
				return false;
			return Player.m_UnknownSavePatternCount == Player.m_Saves - Player.m_IndependentSaveCount - Player.m_RepeatedContextSaveCount;
		}

		bool SameImpactParticipant(const SPlayerScoringFeatures &Left, const SPlayerScoringFeatures &Right)
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
				Left.m_SavePatternEvidenceCoverageComplete == Right.m_SavePatternEvidenceCoverageComplete &&
				Left.m_SavePatternClassificationComplete == Right.m_SavePatternClassificationComplete;
		}

		double ParticipationConfidence(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context, SImpactResult *pResult)
		{
			if(!Player.m_ParticipationKnown)
				return SImpactV1Constants::m_ShortRunConfidenceFloor;

			pResult->m_ParticipationKnown = true;
			pResult->m_ParticipationTicks = Player.m_ParticipationTicks;
			pResult->m_ParticipationSeconds = static_cast<double>(Player.m_ParticipationTicks) / Context.m_TickSpeed;
			const double TimeFraction = std::clamp(
				pResult->m_ParticipationSeconds / SImpactV1Constants::m_ParticipationFullConfidenceSeconds,
				0.0, 1.0);
			return SImpactV1Constants::m_ShortRunConfidenceFloor +
				(1.0 - SImpactV1Constants::m_ShortRunConfidenceFloor) * std::sqrt(TimeFraction);
		}

		double EvidenceConfidence(const CRunScoringContext &Context)
		{
			switch(Context.m_ScoringEvidenceStatus)
			{
			case EPhase4EvidenceStatus::COMPLETE:
				return Context.m_ScoringEvidenceDroppedEvents == 0 ?
					SImpactV1Constants::m_CompleteEvidenceConfidence : SImpactV1Constants::m_IncompleteEvidenceConfidence;
			case EPhase4EvidenceStatus::INCOMPLETE:
				return SImpactV1Constants::m_IncompleteEvidenceConfidence;
			case EPhase4EvidenceStatus::UNAVAILABLE:
			case EPhase4EvidenceStatus::UNSUPPORTED_VERSION:
			case EPhase4EvidenceStatus::INVALID:
				return SImpactV1Constants::m_UnknownEvidenceConfidence;
			}
			return SImpactV1Constants::m_UnknownEvidenceConfidence;
		}

		size_t ProvenFinalizedEngagedCohortSize(const CRunScoringContext &Context)
		{
			size_t Count = 0;
			for(const SPlayerScoringFeatures &Candidate : Context.m_vPlayers)
			{
				if(Candidate.m_Engaged && Candidate.m_Finalized && Candidate.m_InFinalizedScoringCohort)
					++Count;
			}
			// A missing or internally inconsistent roster summary is not proof of
			// a 3+ participant cohort, so the conservative Clutch sidecar is zero.
			return Count == Context.m_FinalizedEngagedParticipantCount ? Count : 0;
		}
	}

	double TransformImpactEvidenceV1(double PositiveEvidence, double NegativeEvidence, double *pHarmSeverity)
	{
		if(pHarmSeverity != nullptr)
			*pHarmSeverity = 0.0;
		if(!std::isfinite(PositiveEvidence) || !std::isfinite(NegativeEvidence) || PositiveEvidence < 0.0 || NegativeEvidence < 0.0)
			return std::numeric_limits<double>::quiet_NaN();

		const double NetEvidence = PositiveEvidence - NegativeEvidence;
		if(NetEvidence == 0.0)
			return 0.0;
		if(NetEvidence > 0.0)
			return SImpactV1Constants::m_PositiveImpactScale *
				std::log1p(NetEvidence / SImpactV1Constants::m_PositiveImpactSoftness);

		const double Deficit = NegativeEvidence - PositiveEvidence;
		// Here NegativeEvidence is strictly positive. This equivalent form of
		// Deficit^2 / NegativeEvidence avoids squaring a large intermediate while
		// keeping zero-positive cases equal to the full negative evidence.
		const double HarmSeverity = Deficit * (Deficit / NegativeEvidence);
		if(pHarmSeverity != nullptr)
			*pHarmSeverity = HarmSeverity;
		if(!std::isfinite(HarmSeverity))
			return std::numeric_limits<double>::quiet_NaN();
		return -SImpactV1Constants::m_NegativeHarmScale *
			std::log1p(HarmSeverity / SImpactV1Constants::m_NegativeHarmSoftness);
	}

	bool CalculateImpactV1(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context,
		SImpactResult *pResult, const SStabilityResult *pOptionalStabilityDiagnostic)
	{
		if(pResult == nullptr)
			return false;
		*pResult = {};
		if(!Context.m_Available || Context.m_TickSpeed <= 0 || Context.m_TickSpeed > 1000 || !IsValidImpactParticipant(Player))
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
		if(pContextPlayer == nullptr || !SameImpactParticipant(Player, *pContextPlayer) || !IsValidImpactParticipant(*pContextPlayer))
			return false;

		SImpactResult Result;
		Result.m_Id = Player.m_Id;
		Result.m_EvidenceStatus = Context.m_ScoringEvidenceStatus;
		Result.m_EvidenceDroppedEvents = Context.m_ScoringEvidenceDroppedEvents;
		Result.m_RawCanonicalSaves = Player.m_Saves;
		Result.m_IndependentSaves = Player.m_IndependentSaveCount;
		Result.m_RepeatedSaves = Player.m_RepeatedContextSaveCount;
		Result.m_UnknownPatternSaves = Player.m_UnknownSavePatternCount;
		Result.m_SavePatternEvidenceCoverageComplete = Player.m_SavePatternEvidenceCoverageComplete;
		Result.m_SavePatternClassificationComplete = Player.m_SavePatternClassificationComplete;
		Result.m_Clutches = Player.m_Clutches;
		Result.m_Aleds = Player.m_Aleds;
		Result.m_FinalizedEngagedScoringCohortSize = ProvenFinalizedEngagedCohortSize(Context);
		Result.m_Falls = Player.m_Falls;
		Result.m_RescuesReceived = Player.m_RescuesReceived;
		Result.m_RescuesReceivedKnown = Player.m_RescuesReceivedKnown;
		Result.m_ProvenRescueAssistSupport = Player.m_ProvenRescueAssistSupport;
		Result.m_ProvenRescueAssistsKnown = Player.m_ProvenRescueAssistsKnown;
		if(pOptionalStabilityDiagnostic != nullptr && pOptionalStabilityDiagnostic->m_Available &&
			pOptionalStabilityDiagnostic->m_Id == Player.m_Id && std::isfinite(pOptionalStabilityDiagnostic->m_Value))
		{
			Result.m_StabilityDiagnosticKnown = true;
			Result.m_StabilityDiagnosticValue = pOptionalStabilityDiagnostic->m_Value;
		}

		Result.m_EffectiveSaveEvidence =
			static_cast<double>(Player.m_IndependentSaveCount) * SImpactV1Constants::m_IndependentSaveWeight +
			static_cast<double>(Player.m_RepeatedContextSaveCount) * SImpactV1Constants::m_RepeatedSaveWeight +
			static_cast<double>(Player.m_UnknownSavePatternCount) * SImpactV1Constants::m_UnknownPatternSaveWeight;
		const double ClutchCount = Result.m_FinalizedEngagedScoringCohortSize >= 3 ? static_cast<double>(Player.m_Clutches) : 0.0;
		Result.m_ClutchEvidence = SImpactV1Constants::m_ClutchEvidenceScale * std::log1p(ClutchCount);
		Result.m_AledEvidence = SImpactV1Constants::m_AledEvidenceScale * std::log1p(static_cast<double>(Player.m_Aleds));
		Result.m_PositiveEvidence = Result.m_EffectiveSaveEvidence + Result.m_ClutchEvidence + Result.m_AledEvidence;
		Result.m_NegativeEvidence = static_cast<double>(Player.m_Falls) * SImpactV1Constants::m_FallWeight;
		Result.m_NetEvidence = Result.m_PositiveEvidence - Result.m_NegativeEvidence;
		Result.m_RawImpact = TransformImpactEvidenceV1(Result.m_PositiveEvidence, Result.m_NegativeEvidence, &Result.m_HarmSeverity);
		if(!std::isfinite(Result.m_EffectiveSaveEvidence) || !std::isfinite(Result.m_ClutchEvidence) ||
			!std::isfinite(Result.m_AledEvidence) || !std::isfinite(Result.m_PositiveEvidence) ||
			!std::isfinite(Result.m_NegativeEvidence) || !std::isfinite(Result.m_NetEvidence) ||
			!std::isfinite(Result.m_HarmSeverity) || !std::isfinite(Result.m_RawImpact))
			return false;

		Result.m_ParticipationConfidence = ParticipationConfidence(Player, Context, &Result);
		Result.m_EvidenceConfidence = EvidenceConfidence(Context);
		Result.m_CombinedConfidence = std::clamp(Result.m_ParticipationConfidence * Result.m_EvidenceConfidence, 0.0, 1.0);
		Result.m_FinalImpact = Result.m_RawImpact * Result.m_CombinedConfidence;
		if(!std::isfinite(Result.m_ParticipationConfidence) || !std::isfinite(Result.m_EvidenceConfidence) ||
			!std::isfinite(Result.m_CombinedConfidence) || !std::isfinite(Result.m_FinalImpact))
			return false;

		Result.m_Available = true;
		*pResult = Result;
		return true;
	}

	std::string FormatImpactV1Result(const SImpactResult &Result)
	{
		if(!Result.m_Available)
			return "ImpactV1: UNAVAILABLE";
		std::ostringstream Output;
		Output.imbue(std::locale::classic());
		Output << std::setprecision(std::numeric_limits<double>::max_digits10)
			<< "ImpactV1 id=" << Result.m_Id.m_ClientId << ':' << Result.m_Id.m_SlotGeneration
			<< " final=" << Result.m_FinalImpact << " raw=" << Result.m_RawImpact
			<< " canonical_saves=" << Result.m_RawCanonicalSaves
			<< " independent_saves=" << Result.m_IndependentSaves << " repeated_saves=" << Result.m_RepeatedSaves
			<< " unknown_pattern_saves=" << Result.m_UnknownPatternSaves
			<< " save_pattern_coverage=" << (Result.m_SavePatternEvidenceCoverageComplete ? "complete" : "incomplete")
			<< " save_pattern_classification=" << (Result.m_SavePatternClassificationComplete ? "complete" : "incomplete")
			<< " effective_save_evidence=" << Result.m_EffectiveSaveEvidence
			<< " clutches=" << Result.m_Clutches << " aleds=" << Result.m_Aleds
			<< " finalized_engaged_cohort=" << Result.m_FinalizedEngagedScoringCohortSize
			<< " clutch_evidence=" << Result.m_ClutchEvidence << " aled_evidence=" << Result.m_AledEvidence
			<< " positive_evidence=" << Result.m_PositiveEvidence
			<< " falls=" << Result.m_Falls
			<< " rescues_received=" << (Result.m_RescuesReceivedKnown ? "known:" : "UNKNOWN:") << Result.m_RescuesReceived
			<< " rescues_received_scoring=ignored"
			<< " negative_evidence=" << Result.m_NegativeEvidence << " net_evidence=" << Result.m_NetEvidence
			<< " harm_severity=" << Result.m_HarmSeverity
			<< " participation=" << (Result.m_ParticipationKnown ? "known:" : "UNKNOWN:") << Result.m_ParticipationSeconds << "s"
			<< " participation_confidence=" << Result.m_ParticipationConfidence
			<< " evidence=" << Phase4EvidenceStatusName(Result.m_EvidenceStatus) << "/dropped=" << Result.m_EvidenceDroppedEvents
			<< " evidence_confidence=" << Result.m_EvidenceConfidence << " combined_confidence=" << Result.m_CombinedConfidence
			<< " proven_rescue_assists=" << Phase4FeatureSupportName(Result.m_ProvenRescueAssistSupport)
			<< (Result.m_ProvenRescueAssistsKnown ? ":known" : ":UNKNOWN")
			<< " stability_context=" << (Result.m_StabilityDiagnosticKnown ? "known:" : "UNKNOWN:") << Result.m_StabilityDiagnosticValue;
		return Output.str();
	}
}
