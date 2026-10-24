#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_UI_THEME_SETTINGS_ANIMATION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_UI_THEME_SETTINGS_ANIMATION_H

#include <algorithm>

namespace AmfUiTheme
{
struct SSettingsAnimationLayout
{
	float m_GradientTargetHeight;
	float m_AccentTargetHeight;
	float m_OpacityTargetHeight;
	float m_StyleTargetHeight;
	float m_BackgroundColorTargetHeight;
	float m_StyleVisibleHeight;
	float m_GradientVisibleHeight;
	float m_AccentVisibleHeight;
	float m_OpacityVisibleHeight;
	float m_BackgroundColorVisibleHeight;
	float m_BackgroundToggleOffset;
	float m_BackgroundColorOffset;
	float m_ExportOffset;
	float m_TotalDetailsHeight;
};

inline SSettingsAnimationLayout MakeSettingsAnimationLayout(
	float StyleProgress,
	float GradientProgress,
	float AccentProgress,
	float OpacityProgress,
	float BackgroundColorProgress,
	float RowHeight,
	float ColorRowHeight,
	float LineHeight,
	float Gap)
{
	StyleProgress = std::clamp(StyleProgress, 0.0f, 1.0f);
	GradientProgress = std::clamp(GradientProgress, 0.0f, 1.0f);
	AccentProgress = std::clamp(AccentProgress, 0.0f, 1.0f);
	OpacityProgress = std::clamp(OpacityProgress, 0.0f, 1.0f);
	BackgroundColorProgress = std::clamp(BackgroundColorProgress, 0.0f, 1.0f);

	SSettingsAnimationLayout Layout{};
	Layout.m_GradientTargetHeight = ColorRowHeight + RowHeight;
	Layout.m_AccentTargetHeight = ColorRowHeight;
	Layout.m_OpacityTargetHeight = RowHeight;
	Layout.m_StyleTargetHeight = ColorRowHeight + RowHeight +
		GradientProgress * Layout.m_GradientTargetHeight + RowHeight +
		AccentProgress * Layout.m_AccentTargetHeight + OpacityProgress * Layout.m_OpacityTargetHeight;
	Layout.m_BackgroundColorTargetHeight = ColorRowHeight;
	Layout.m_StyleVisibleHeight = StyleProgress * Layout.m_StyleTargetHeight;
	Layout.m_GradientVisibleHeight = GradientProgress * Layout.m_GradientTargetHeight;
	Layout.m_AccentVisibleHeight = AccentProgress * Layout.m_AccentTargetHeight;
	Layout.m_OpacityVisibleHeight = OpacityProgress * Layout.m_OpacityTargetHeight;
	Layout.m_BackgroundColorVisibleHeight = BackgroundColorProgress * Layout.m_BackgroundColorTargetHeight;

	// Leading gap, element selector, custom-style toggle, background toggle,
	// export row, and trailing gap. Conditional content is added between them.
	Layout.m_BackgroundToggleOffset = Gap + 2.0f * RowHeight + Layout.m_StyleVisibleHeight;
	Layout.m_BackgroundColorOffset = Layout.m_BackgroundToggleOffset + RowHeight;
	Layout.m_ExportOffset = Layout.m_BackgroundColorOffset + Layout.m_BackgroundColorVisibleHeight;
	Layout.m_TotalDetailsHeight = Layout.m_ExportOffset + LineHeight + Gap;
	return Layout;
}
} // namespace AmfUiTheme

#endif
