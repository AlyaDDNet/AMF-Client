#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_RUN_STATISTICS_BADGE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_RUN_STATISTICS_BADGE_H

#include <base/color.h>

namespace AmfRunStatisticsBadge
{
inline constexpr char LABEL[] = "BETA";
inline constexpr float FONT_SIZE = 12.0f;
inline constexpr float HORIZONTAL_PADDING = 5.0f;
inline constexpr float VERTICAL_INSET = 2.0f;
inline constexpr float ROUNDING = 5.0f;
inline constexpr ColorRGBA LEFT(0.95f, 0.25f, 0.25f, 1.0f);
inline constexpr ColorRGBA RIGHT(0.75f, 0.08f, 0.08f, 1.0f);

struct SPlacement
{
	float m_TitleWidth;
	float m_BadgeX;
	float m_BadgeY;
	float m_BadgeWidth;
	float m_BadgeHeight;
};

constexpr SPlacement Place(float RowX, float RowY, float RowWidth, float RowHeight, float TextWidth, float Gap)
{
	const float BadgeWidth = TextWidth + 2.0f * HORIZONTAL_PADDING;
	return {
		RowWidth - BadgeWidth - Gap,
		RowX + RowWidth - BadgeWidth,
		RowY + VERTICAL_INSET,
		BadgeWidth,
		RowHeight - 2.0f * VERTICAL_INSET,
	};
}
} // namespace AmfRunStatisticsBadge

#endif
