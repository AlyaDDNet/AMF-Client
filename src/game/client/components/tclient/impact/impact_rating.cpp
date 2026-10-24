/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_rating.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace AmfImpact
{
	SFinalizedScoringDimensions MakeFinalizedScoringDimensions(const SImpactResult &Impact, const SStabilityResult &Stability)
	{
		SFinalizedScoringDimensions Dimensions;
		Dimensions.m_ImpactAvailable = Impact.m_Available;
		Dimensions.m_StabilityAvailable = Stability.m_Available;
		Dimensions.m_ImpactId = Impact.m_Id;
		Dimensions.m_StabilityId = Stability.m_Id;
		Dimensions.m_FinalizedEngagedScoringCohortSize = Impact.m_FinalizedEngagedScoringCohortSize;
		Dimensions.m_FinalImpact = Impact.m_FinalImpact;
		Dimensions.m_FinalStability = Stability.m_Value;
		return Dimensions;
	}

	ERatingClass ClassifyRatingV1(double Rating)
	{
		if(!std::isfinite(Rating) || Rating < SRatingV1Constants::m_MinimumRating)
			return ERatingClass::UNAVAILABLE;
		if(Rating < SRatingV1Constants::m_LowUpperExclusive)
			return ERatingClass::LOW;
		if(Rating < SRatingV1Constants::m_MediumUpperExclusive)
			return ERatingClass::MEDIUM;
		if(Rating < SRatingV1Constants::m_GoodUpperExclusive)
			return ERatingClass::GOOD;
		if(Rating < SRatingV1Constants::m_ExcellentUpperExclusive)
			return ERatingClass::EXCELLENT;
		if(Rating < SRatingV1Constants::m_OutstandingUpperExclusive)
			return ERatingClass::OUTSTANDING;
		return ERatingClass::STRONG_IMPACT;
	}

	const char *RatingClassName(ERatingClass Class)
	{
		switch(Class)
		{
		case ERatingClass::LOW: return "LOW";
		case ERatingClass::MEDIUM: return "MEDIUM";
		case ERatingClass::GOOD: return "GOOD";
		case ERatingClass::EXCELLENT: return "EXCELLENT";
		case ERatingClass::OUTSTANDING: return "OUTSTANDING";
		case ERatingClass::STRONG_IMPACT: return "STRONG_IMPACT";
		case ERatingClass::UNAVAILABLE: return "UNAVAILABLE";
		}
		return "UNAVAILABLE";
	}

	bool CalculateRatingV1(const SFinalizedScoringDimensions &Dimensions, SRatingResult *pResult)
	{
		if(pResult == nullptr)
			return false;
		*pResult = {};
		if(!Dimensions.m_ImpactAvailable || !Dimensions.m_StabilityAvailable ||
			!Dimensions.m_ImpactId.IsValid() || Dimensions.m_ImpactId != Dimensions.m_StabilityId ||
			Dimensions.m_FinalizedEngagedScoringCohortSize == 0 ||
			!std::isfinite(Dimensions.m_FinalImpact) || !std::isfinite(Dimensions.m_FinalStability))
			return false;

		SRatingResult Result;
		Result.m_Id = Dimensions.m_ImpactId;
		Result.m_Impact = Dimensions.m_FinalImpact;
		Result.m_Stability = Dimensions.m_FinalStability;

		if(Result.m_Impact >= 0.0)
		{
			Result.m_PositiveContribution = SRatingV1Constants::m_PositiveLinearScale * Result.m_Impact +
				SRatingV1Constants::m_PositiveLogScale * std::log1p(Result.m_Impact);
			Result.m_ImpactContribution = Result.m_PositiveContribution;
		}
		else
		{
			Result.m_NegativePenalty = SRatingV1Constants::m_NegativePenaltyScale *
				std::log1p(std::abs(Result.m_Impact) / SRatingV1Constants::m_NegativeSoftness);
			Result.m_ImpactContribution = -Result.m_NegativePenalty;
		}

		Result.m_RawRating = SRatingV1Constants::m_NeutralBase + Result.m_ImpactContribution;
		if(!std::isfinite(Result.m_PositiveContribution) ||
			!std::isfinite(Result.m_NegativePenalty) || !std::isfinite(Result.m_ImpactContribution) ||
			!std::isfinite(Result.m_RawRating))
			return false;

		Result.m_FinalRating = std::max(SRatingV1Constants::m_MinimumRating, Result.m_RawRating);
		Result.m_Class = ClassifyRatingV1(Result.m_FinalRating);
		if(!std::isfinite(Result.m_FinalRating) || Result.m_Class == ERatingClass::UNAVAILABLE)
			return false;
		Result.m_Available = true;
		*pResult = Result;
		return true;
	}

	std::string FormatRatingV1Result(const SRatingResult &Result)
	{
		if(!Result.m_Available)
			return "RatingV1: UNAVAILABLE";
		std::ostringstream Output;
		Output.imbue(std::locale::classic());
		Output << std::setprecision(std::numeric_limits<double>::max_digits10)
			<< "RatingV1 id=" << Result.m_Id.m_ClientId << ':' << Result.m_Id.m_SlotGeneration
			<< " rating=" << Result.m_FinalRating << " class=" << RatingClassName(Result.m_Class)
			<< " impact=" << Result.m_Impact << " stability_provenance=" << Result.m_Stability
			<< " positive_contribution=" << Result.m_PositiveContribution
			<< " negative_penalty=" << Result.m_NegativePenalty
			<< " impact_contribution=" << Result.m_ImpactContribution << " raw=" << Result.m_RawRating;
		return Output.str();
	}
}
