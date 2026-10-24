#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_SETTINGS_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_SETTINGS_POLICY_H

#include <base/color.h>

namespace AmfSettingsPolicy
{
constexpr float SectionGap = 8.0f;
constexpr float BallButtonHeight = 25.0f;
constexpr float BallPlayerWidth = 110.0f;
constexpr float BallCursorWidth = 130.0f;

// Consume the card and its trailing gap from this column only. Inner content
// rectangles must never become the cursor for a following outer card.
template<typename TRect>
TRect TakeCard(TRect &Column, float Height)
{
	TRect Card = Column;
	Card.h = Height;
	Column.y += Height + SectionGap;
	Column.h -= Height + SectionGap;
	return Card;
}

struct SDestination
{
	int m_Page;
	int m_Subpage;
	const char *m_pLabel;
	bool IsActive(int Page, int Subpage) const
	{
		return m_Page == Page && (m_Subpage < 0 || m_Subpage == Subpage);
	}
};

inline ColorRGBA ColorPreview(unsigned Value, bool Alpha)
{
	return color_cast<ColorRGBA>(ColorHSLA(Value, Alpha));
}
} // namespace AmfSettingsPolicy
#endif
