#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PRESENTATION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PRESENTATION_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include <base/color.h>

#include <array>

namespace AmfImpact
{
	enum class EImpactDisplayColor
	{
		RED,
		WHITE,
		GREEN,
		PURPLE,
		GOLD,
	};

	struct SImpactPresentationConstants
	{
		static constexpr double m_NormalUpperExclusive = 4.0;
		static constexpr double m_GoodUpperExclusive = 6.0;
		static constexpr double m_ExcellentUpperInclusive = 10.0;

		// Shared presentation colors: red/white/gold match existing client states;
		// green and purple match the established Rating text colors.
		static constexpr ColorRGBA m_RedColor{0.95f, 0.25f, 0.25f, 1.0f};
		static constexpr ColorRGBA m_WhiteColor{1.0f, 1.0f, 1.0f, 1.0f};
		static constexpr ColorRGBA m_GreenColor{0.30f, 0.90f, 0.30f, 1.0f};
		static constexpr ColorRGBA m_PurpleColor{0.82f, 0.52f, 1.0f, 1.0f};
		static constexpr ColorRGBA m_GoldColor{1.0f, 0.80f, 0.30f, 1.0f};
	};

	inline constexpr std::array<const char *, 6> gs_aImpactTooltipText = {
		"Воздействие",
		"< 0.00% - Ужасное",
		"0.00-3.99% - Нормальное",
		"4.00-5.99% - Хорошее",
		"6.00-10.00% - Отличное",
		"> 10.00% - Выдающееся",
	};

	EImpactDisplayColor ClassifyImpactDisplayColor(double FinalImpact);
	ColorRGBA GetImpactDisplayColor(EImpactDisplayColor Color);
	ColorRGBA GetImpactDisplayColor(double FinalImpact);
}

#endif
