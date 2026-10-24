#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RATING_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RATING_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_impact.h"

namespace AmfImpact
{
	struct SRatingV1Constants
	{
		static constexpr double m_NeutralBase = 0.76;
		static constexpr double m_PositiveLinearScale = 0.09;
		static constexpr double m_PositiveLogScale = 0.10;
		static constexpr double m_NegativePenaltyScale = 0.58;
		static constexpr double m_NegativeSoftness = 6.0;
		static constexpr double m_MinimumRating = 0.0;

		static constexpr double m_LowUpperExclusive = 0.60;
		static constexpr double m_MediumUpperExclusive = 1.01;
		static constexpr double m_GoodUpperExclusive = 1.40;
		static constexpr double m_ExcellentUpperExclusive = 1.61;
		static constexpr double m_OutstandingUpperExclusive = 2.00;
	};

	// This deliberately contains only final dimensions and validity provenance.
	// Raw gameplay counters and scoring evidence cannot enter the Rating formula.
	struct SFinalizedScoringDimensions
	{
		bool m_ImpactAvailable = false;
		bool m_StabilityAvailable = false;
		SParticipantId m_ImpactId;
		SParticipantId m_StabilityId;
		size_t m_FinalizedEngagedScoringCohortSize = 0;
		double m_FinalImpact = 0.0;
		double m_FinalStability = 0.0;
	};

	enum class ERatingClass
	{
		UNAVAILABLE,
		LOW,
		MEDIUM,
		GOOD,
		EXCELLENT,
		OUTSTANDING,
		STRONG_IMPACT,
	};

	struct SRatingResult
	{
		bool m_Available = false;
		SParticipantId m_Id;
		double m_Impact = 0.0;
		// Retained for finalized-dimension provenance/diagnostics only; not used in Rating arithmetic.
		double m_Stability = 0.0;
		double m_PositiveContribution = 0.0;
		double m_NegativePenalty = 0.0;
		double m_ImpactContribution = 0.0;
		double m_RawRating = 0.0;
		double m_FinalRating = 0.0;
		ERatingClass m_Class = ERatingClass::UNAVAILABLE;
	};

	SFinalizedScoringDimensions MakeFinalizedScoringDimensions(const SImpactResult &Impact, const SStabilityResult &Stability);
	ERatingClass ClassifyRatingV1(double Rating);
	const char *RatingClassName(ERatingClass Class);
	bool CalculateRatingV1(const SFinalizedScoringDimensions &Dimensions, SRatingResult *pResult);
	std::string FormatRatingV1Result(const SRatingResult &Result);
}

#endif
