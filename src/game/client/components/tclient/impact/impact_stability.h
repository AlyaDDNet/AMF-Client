#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_STABILITY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_STABILITY_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_scoring_features.h"

namespace AmfImpact
{
	struct SStabilityV1Constants
	{
		static constexpr double m_Neutral = 50.0;
		static constexpr double m_IndependentSaveEvidenceWeight = 1.0;
		static constexpr double m_RepeatedLoopSaveEvidenceWeight = 0.35;
		static constexpr double m_UnknownPatternSaveEvidenceWeight = 1.0;
		static constexpr double m_SavePositiveWeight = 0.85;
		static constexpr double m_FallWeight = 1.0;
		static constexpr double m_RescueReceivedWeight = 0.0;
		static constexpr double m_BayesianPriorPositive = 1.0;
		static constexpr double m_BayesianPriorTotal = 2.0;
		static constexpr double m_PeerAdjustmentMax = 4.0;
		static constexpr double m_ParticipationFullConfidenceSeconds = 300.0;
		static constexpr double m_ShortRunConfidenceFloor = 0.65;
		static constexpr double m_IncompleteEvidenceConfidence = 0.75;
		static constexpr double m_UnknownEvidenceConfidence = 0.65;
	};

	struct SStabilityResult
	{
		bool m_Available = false;
		SParticipantId m_Id;
		EPhase4EvidenceStatus m_EvidenceStatus = EPhase4EvidenceStatus::UNAVAILABLE;
		uint64_t m_EvidenceDroppedEvents = 0;

		int64_t m_CanonicalSaves = 0;
		int64_t m_IndependentSaves = 0;
		int64_t m_RepeatedLoopSaves = 0;
		int64_t m_UnknownPatternSaves = 0;
		bool m_SavePatternEvidenceCoverageComplete = false;
		bool m_SavePatternClassificationComplete = false;
		int64_t m_Aleds = 0;
		int64_t m_Clutches = 0;
		int64_t m_Falls = 0;
		int64_t m_RescuesReceived = 0;
		bool m_RescuesReceivedKnown = false;

		double m_EffectiveSaveEvidence = 0.0;
		double m_PositiveEvidence = 0.0;
		double m_NegativeEvidence = 0.0;
		double m_RawStability = SStabilityV1Constants::m_Neutral;

		bool m_ParticipationKnown = false;
		int64_t m_ParticipationTicks = 0;
		double m_ParticipationSeconds = 0.0;
		double m_ParticipationConfidence = SStabilityV1Constants::m_ShortRunConfidenceFloor;
		double m_EvidenceConfidence = SStabilityV1Constants::m_UnknownEvidenceConfidence;
		double m_CombinedConfidence = 0.0;

		bool m_PlayerFallRateKnown = false;
		double m_PlayerFallRatePerMinute = 0.0;
		bool m_CohortMedianFallRateKnown = false;
		double m_CohortMedianFallRatePerMinute = 0.0;
		size_t m_PeerSampleCount = 0;
		double m_PeerAdjustment = 0.0;
		double m_PeerAdjustmentApplied = 0.0;
		double m_ConfidenceAdjustedStability = SStabilityV1Constants::m_Neutral;
		double m_Value = SStabilityV1Constants::m_Neutral;
	};

	// Calibration-stage scorer only. It is not connected to persistence, ranking,
	// normal UI, Rating, Impact, or MVP.
	bool CalculateStabilityV1(const SPlayerScoringFeatures &Player, const CRunScoringContext &Context, SStabilityResult *pResult);
	std::string FormatStabilityV1Result(const SStabilityResult &Result);
}

#endif
