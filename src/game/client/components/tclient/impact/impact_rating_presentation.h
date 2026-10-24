#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RATING_PRESENTATION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_RATING_PRESENTATION_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_rating.h"
#include "impact_presentation.h"

#include <base/color.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace AmfImpact
{
	inline constexpr std::array<const char *, 7> gs_aRatingTooltipText = {
		"Рейтинг",
		"0.00-0.59 - Низкий",
		"0.60-1.00 - Средний",
		"1.01-1.39 - Хороший",
		"1.40-1.60 - Отличный",
		"1.61-1.99 - Превосходный",
		"2.00+ - Сильное воздействие",
	};

	inline constexpr std::array<const char *, 5> gs_aStabilityTooltipText = {
		"Стабильность",
		"0-40% - Плохая",
		"41-59% - Хорошая",
		"60-75% - Отличная",
		"76-100% - Выдающаяся",
	};

	inline CUIRect MakeStatisticTooltipHitRect(const CUIRect &TextArea, float MeasuredTextWidth, float FontSize)
	{
		constexpr float Padding = 1.0f;
		const float Width = std::min(TextArea.w, std::max(0.0f, MeasuredTextWidth) + 2.0f * Padding);
		const float Height = std::min(TextArea.h, std::max(0.0f, FontSize) + 2.0f * Padding);
		const vec2 Center = TextArea.Center();
		return {Center.x - Width * 0.5f, Center.y - Height * 0.5f, Width, Height};
	}

	// Keep the table tooltip hit target around the actual rendered glyphs,
	// rather than the full font line (which also reaches toward the stars below).
	inline CUIRect MakeRatingTooltipHitRect(const CUIRect &TextArea, float MeasuredTextWidth, float MeasuredCharacterHeight)
	{
		constexpr float Padding = 1.0f;
		const float Width = std::min(TextArea.w, std::max(0.0f, MeasuredTextWidth) + 2.0f * Padding);
		const float Height = std::min(TextArea.h, std::max(0.0f, MeasuredCharacterHeight) + 2.0f * Padding);
		const vec2 Center = TextArea.Center();
		return {Center.x - Width * 0.5f, Center.y - Height * 0.5f, Width, Height};
	}

	// Centralized cosmetic frame palette for full-precision Rating classes.
	// These colors are presentation-only and never enter score/order decisions.
	struct SRatingFrameStyle
	{
		ERatingClass m_Class = ERatingClass::UNAVAILABLE;
		ColorRGBA m_Background = ColorRGBA(0.045f, 0.085f, 0.14f, 0.78f);
		ColorRGBA m_Border = ColorRGBA(0.36f, 0.52f, 0.70f, 0.60f);
		ColorRGBA m_Highlight = ColorRGBA(0.48f, 0.66f, 0.88f, 0.18f);
		ColorRGBA m_Text = ColorRGBA(0.70f, 0.81f, 0.94f, 1.0f);
	};

	inline SRatingFrameStyle GetRatingFrameStyle(bool Available, double FullPrecisionRating)
	{
		SRatingFrameStyle Style;
		if(!Available)
			return Style;

		Style.m_Class = ClassifyRatingV1(FullPrecisionRating);
		switch(Style.m_Class)
		{
		case ERatingClass::LOW:
			Style.m_Background = ColorRGBA(0.035f, 0.075f, 0.12f, 1.0f);
			Style.m_Border = ColorRGBA(0.48f, 0.66f, 0.83f, 0.90f);
			Style.m_Highlight = ColorRGBA(0.44f, 0.66f, 0.91f, 0.20f);
			Style.m_Text = GetImpactDisplayColor(EImpactDisplayColor::RED);
			break;
		case ERatingClass::MEDIUM:
			Style.m_Background = ColorRGBA(0.040f, 0.054f, 0.078f, 1.0f);
			Style.m_Border = ColorRGBA(0.74f, 0.83f, 0.94f, 0.92f);
			Style.m_Highlight = ColorRGBA(0.82f, 0.91f, 1.0f, 0.20f);
			Style.m_Text = GetImpactDisplayColor(EImpactDisplayColor::WHITE);
			break;
		case ERatingClass::GOOD:
			Style.m_Background = ColorRGBA(0.025f, 0.048f, 0.055f, 1.0f);
			Style.m_Border = ColorRGBA(0.08f, 0.82f, 0.66f, 0.94f);
			Style.m_Highlight = ColorRGBA(0.13f, 0.98f, 0.79f, 0.20f);
			Style.m_Text = ColorRGBA(0.30f, 0.90f, 0.30f, 1.0f);
			break;
		case ERatingClass::EXCELLENT:
			Style.m_Background = ColorRGBA(0.075f, 0.035f, 0.13f, 1.0f);
			Style.m_Border = ColorRGBA(0.68f, 0.32f, 0.94f, 0.94f);
			Style.m_Highlight = ColorRGBA(0.83f, 0.45f, 1.0f, 0.22f);
			Style.m_Text = ColorRGBA(0.82f, 0.52f, 1.0f, 1.0f);
			break;
		case ERatingClass::OUTSTANDING:
			Style.m_Background = ColorRGBA(0.12f, 0.085f, 0.025f, 1.0f);
			Style.m_Border = ColorRGBA(0.97f, 0.75f, 0.19f, 0.96f);
			Style.m_Highlight = ColorRGBA(1.0f, 0.86f, 0.40f, 0.23f);
			Style.m_Text = ColorRGBA(0.80f, 0.86f, 0.94f, 1.0f);
			break;
		case ERatingClass::STRONG_IMPACT:
			Style.m_Background = ColorRGBA(0.13f, 0.09f, 0.025f, 1.0f);
			Style.m_Border = ColorRGBA(1.0f, 0.82f, 0.27f, 0.98f);
			Style.m_Highlight = ColorRGBA(1.0f, 0.90f, 0.46f, 0.27f);
			Style.m_Text = GetImpactDisplayColor(EImpactDisplayColor::GOLD);
			break;
		case ERatingClass::UNAVAILABLE:
			break;
		}
		return Style;
	}

	inline int RatingStarCount(bool Available, double FullPrecisionRating)
	{
		if(!Available || !std::isfinite(FullPrecisionRating))
			return 0;
		switch(ClassifyRatingV1(FullPrecisionRating))
		{
		case ERatingClass::LOW: return 0;
		case ERatingClass::MEDIUM: return 1;
		case ERatingClass::GOOD: return 2;
		case ERatingClass::EXCELLENT: return 3;
		case ERatingClass::OUTSTANDING: return 4;
		case ERatingClass::STRONG_IMPACT: return 5;
		case ERatingClass::UNAVAILABLE: return 0;
		}
		return 0;
	}

	inline ColorRGBA RatingStarTextColor(bool Filled)
	{
		return Filled ? ColorRGBA(0.780392f, 0.823529f, 0.886275f, 1.0f) : ColorRGBA(0.203922f, 0.254902f, 0.313725f, 1.0f);
	}

	// The exact finalized value selects the presentation tier; the truncated
	// integer label is never read back for classification.
	inline ColorRGBA GetStabilityTextColor(bool Available, double FullPrecisionStability)
	{
		if(!Available || !std::isfinite(FullPrecisionStability))
			return ColorRGBA(0.58f, 0.67f, 0.79f, 1.0f);
		if(FullPrecisionStability < 41.0)
			return GetImpactDisplayColor(EImpactDisplayColor::RED);
		if(FullPrecisionStability < 60.0)
			return GetImpactDisplayColor(EImpactDisplayColor::WHITE);
		if(FullPrecisionStability < 76.0)
			return ColorRGBA(0.52f, 0.91f, 0.42f, 1.0f);
		return ColorRGBA(0.98f, 0.78f, 0.32f, 1.0f);
	}

	inline ColorRGBA GetTableStabilityTextColor(bool Available, double FullPrecisionStability)
	{
		return GetStabilityTextColor(Available, FullPrecisionStability);
	}
}

#endif
