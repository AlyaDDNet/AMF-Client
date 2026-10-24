/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */
#include "impact_presentation.h"

#include <cmath>

namespace AmfImpact
{
	EImpactDisplayColor ClassifyImpactDisplayColor(double FinalImpact)
	{
		if(!std::isfinite(FinalImpact))
			return EImpactDisplayColor::WHITE;
		if(FinalImpact < 0.0)
			return EImpactDisplayColor::RED;
		if(FinalImpact < SImpactPresentationConstants::m_NormalUpperExclusive)
			return EImpactDisplayColor::WHITE;
		if(FinalImpact < SImpactPresentationConstants::m_GoodUpperExclusive)
			return EImpactDisplayColor::GREEN;
		if(FinalImpact <= SImpactPresentationConstants::m_ExcellentUpperInclusive)
			return EImpactDisplayColor::PURPLE;
		return EImpactDisplayColor::GOLD;
	}

	ColorRGBA GetImpactDisplayColor(EImpactDisplayColor Color)
	{
		switch(Color)
		{
		case EImpactDisplayColor::RED: return SImpactPresentationConstants::m_RedColor;
		case EImpactDisplayColor::WHITE: return SImpactPresentationConstants::m_WhiteColor;
		case EImpactDisplayColor::GREEN: return SImpactPresentationConstants::m_GreenColor;
		case EImpactDisplayColor::PURPLE: return SImpactPresentationConstants::m_PurpleColor;
		case EImpactDisplayColor::GOLD: return SImpactPresentationConstants::m_GoldColor;
		}
		return SImpactPresentationConstants::m_WhiteColor;
	}

	ColorRGBA GetImpactDisplayColor(double FinalImpact)
	{
		return GetImpactDisplayColor(ClassifyImpactDisplayColor(FinalImpact));
	}
}
