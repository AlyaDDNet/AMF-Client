#include <game/client/components/tclient/amf_features/fx/effect_policy.h>
#include <game/client/components/tclient/amf_features/fx/fgf_iceward.h>
#include <game/client/components/tclient/amf_features/settings_policy.h>
#include <game/client/components/tclient/amf_features/ui_theme/style.h>
#include <game/client/components/tclient/amf_features/ui_theme/settings_animation.h>
#include <game/client/components/tclient/amf_features/weapons/weapon_vfx.h>
#include <game/client/components/asset_organizer.h>
#include <game/client/components/tclient/amf_gradient.h>
#include <game/client/smooth_ui.h>
#include <game/client/ui.h>
#include <engine/shared/config.h>

#include <string>

#include "test.h"

TEST(AmfUiThemes, FontColorOnlyOverridesOrdinaryUiText)
{
	const ColorRGBA DefaultColor(1.0f, 1.0f, 1.0f, 1.0f);
	EXPECT_TRUE(AmfUiTheme::ShouldApplyUiTextTheme(false, DefaultColor, DefaultColor));
	EXPECT_FALSE(AmfUiTheme::ShouldApplyUiTextTheme(true, DefaultColor, DefaultColor)); // explicit label palette
	EXPECT_FALSE(AmfUiTheme::ShouldApplyUiTextTheme(false, ColorRGBA(0.3f, 0.8f, 0.4f, 1.0f), DefaultColor)); // semantic TextRender color
	EXPECT_TRUE(AmfUiTheme::ShouldApplyUiTextTheme(false, ColorRGBA(1.0f, 1.0f, 1.0f, 0.4f), DefaultColor)); // alpha-only UI modulation
}

TEST(AmfFeaturePort, AirJumpParticleControlOnlyHidesEnabledLocalEffect)
{
	EXPECT_TRUE(AmfEffectPolicy::HideAirJumpParticles(true, true, false));
	EXPECT_FALSE(AmfEffectPolicy::HideAirJumpParticles(false, true, false));
	EXPECT_FALSE(AmfEffectPolicy::HideAirJumpParticles(true, false, false));
	EXPECT_FALSE(AmfEffectPolicy::HideAirJumpParticles(true, true, true));
}

TEST(AmfFeaturePort, EnhancedWeaponsKeepExactDonorModeValues)
{
	EXPECT_EQ(CWeaponVfx::MODE_CLASSIC, 0);
	EXPECT_EQ(CWeaponVfx::MODE_PRISM, 1);
	EXPECT_EQ(CWeaponVfx::MODE_PULSE, 2);
	EXPECT_EQ(CWeaponVfx::MODE_CRYSTAL, 3);
	EXPECT_EQ(CWeaponVfx::NUM_MODES, 4);
}

TEST(AmfFeaturePort, EnhancedWeaponsKeepIndependentThreeWeaponDefaults)
{
	EXPECT_EQ(DefaultConfig::AmfLaserGlow, 0);
	EXPECT_EQ(DefaultConfig::AmfShotgunGlow, 0);
	EXPECT_EQ(DefaultConfig::AmfRocketGlow, 0);
	EXPECT_EQ(DefaultConfig::AmfLaserGlowPower, 50);
	EXPECT_EQ(DefaultConfig::AmfShotgunGlowPower, 50);
	EXPECT_EQ(DefaultConfig::AmfRocketGlowPower, 50);
	EXPECT_EQ(DefaultConfig::AmfLaserGlowMode, 0);
	EXPECT_EQ(DefaultConfig::AmfShotgunGlowMode, 0);
	EXPECT_EQ(DefaultConfig::AmfRocketGlowMode, 0);
	EXPECT_EQ(DefaultConfig::AmfLaserGlowColor, 5635840U);
	EXPECT_EQ(DefaultConfig::AmfShotgunGlowColor, 5635840U);
	EXPECT_EQ(DefaultConfig::AmfRocketGlowColor, 2555648U);
	EXPECT_EQ(DefaultConfig::AmfMemeNukeExplosion, 0);
	EXPECT_EQ(DefaultConfig::AmfMemeNukeShake, 100);
	EXPECT_EQ(DefaultConfig::AmfMemeNukeScale, 100);
}

TEST(AmfGradient, SkinNicknameColorDoesNotInheritNameplateTeamTint)
{
	const ColorRGBA DefaultTextColor(0.92f, 0.88f, 0.76f, 1.0f);
	const ColorRGBA TeamColors[] = {
		DefaultTextColor.WithAlpha(0.7f), // Team 0.
		ColorRGBA(1.0f, 0.5f, 0.5f, 0.7f), // Nonzero team, same as local player.
		ColorRGBA(0.7f, 0.7f, 1.0f, 0.7f), // Nonzero team, different from local player.
		ColorRGBA(0.2f, 0.9f, 0.4f, 0.7f), // War/team indicator tint.
	};
	for(const ColorRGBA &TeamColor : TeamColors)
	{
		const ColorRGBA Resolved = AmfGradient::ResolveNameplateNicknameBaseColor(
			TeamColor, DefaultTextColor, true, 0);
		EXPECT_FLOAT_EQ(Resolved.r, DefaultTextColor.r);
		EXPECT_FLOAT_EQ(Resolved.g, DefaultTextColor.g);
		EXPECT_FLOAT_EQ(Resolved.b, DefaultTextColor.b);
		EXPECT_FLOAT_EQ(Resolved.a, TeamColor.a);
	}
}

TEST(AmfGradient, OtherNicknameSourcesAndDisabledGradientKeepNameplateColor)
{
	const ColorRGBA DefaultTextColor(1.0f, 1.0f, 1.0f, 1.0f);
	const ColorRGBA TeamColor(0.7f, 0.7f, 1.0f, 0.6f);
	EXPECT_EQ(AmfGradient::ResolveNameplateNicknameBaseColor(TeamColor, DefaultTextColor, true, 1), TeamColor);
	EXPECT_EQ(AmfGradient::ResolveNameplateNicknameBaseColor(TeamColor, DefaultTextColor, false, 0), TeamColor);
}

TEST(AmfFeaturePort, HammerParticleControlOnlyHidesEnabledLocalEffect)
{
	EXPECT_TRUE(AmfEffectPolicy::HideHammerHitParticles(true, true, false));
	EXPECT_FALSE(AmfEffectPolicy::HideHammerHitParticles(false, true, false));
	EXPECT_FALSE(AmfEffectPolicy::HideHammerHitParticles(true, false, false));
	EXPECT_FALSE(AmfEffectPolicy::HideHammerHitParticles(true, true, true));
}

TEST(AmfFeaturePort, FreezeLifestyleMatchesDonorDefaultsAndIsAClientComponent)
{
	EXPECT_EQ(DefaultConfig::AmfFreezePull, 0);
	EXPECT_EQ(DefaultConfig::AmfFreezePullColor, 9240520U);
	EXPECT_EQ(DefaultConfig::AmfFreezePullRange, 140);
	EXPECT_EQ(DefaultConfig::AmfFreezePullStrands, 4);
	EXPECT_EQ(DefaultConfig::AmfFreezePullShield, 1);
	EXPECT_EQ(DefaultConfig::AmfFreezePullSparks, 100);
	EXPECT_EQ(DefaultConfig::AmfFreezePullHookColor, 0);
	EXPECT_GT(sizeof(CFgfIceWard), 0U);
}

TEST(AmfFeaturePort, SubpageDestinationDoesNotSelectSibling)
{
	const AmfSettingsPolicy::SDestination Visual{11, 1, "Visual"};
	const AmfSettingsPolicy::SDestination General{11, 0, "General"};
	EXPECT_TRUE(Visual.IsActive(11, 1));
	EXPECT_FALSE(Visual.IsActive(11, 0));
	EXPECT_TRUE(General.IsActive(11, 0));
	EXPECT_FALSE(General.IsActive(11, 1));
	EXPECT_FALSE(Visual.IsActive(1, 1));
}

TEST(AmfFeaturePort, CompactBallActionsKeepDonorDimensions)
{
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::BallPlayerWidth, 110.0f);
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::BallCursorWidth, 130.0f);
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::BallButtonHeight, 25.0f);
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::SectionGap, 8.0f);
}

TEST(AmfFeaturePort, RgbEffectSwatchIsOpaqueWithoutAlphaByte)
{
	for(unsigned Value : {255U, 10063569U, 3791782U, 1981660U, 9371558U})
		EXPECT_FLOAT_EQ(AmfSettingsPolicy::ColorPreview(Value, false).a, 1.0f);
}

TEST(AmfFeaturePort, ThemeSwatchPreservesAlphaAndCurrentColor)
{
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::ColorPreview(0x00FFFFFFU, true).a, 0.0f);
	EXPECT_FLOAT_EQ(AmfSettingsPolicy::ColorPreview(0xFFFFFFFFU, true).a, 1.0f);
	EXPECT_NE(AmfSettingsPolicy::ColorPreview(0x00000000U, false).r,
		AmfSettingsPolicy::ColorPreview(0x000000FFU, false).r);
}

TEST(AmfUiThemes, ConditionalSectionsUseSmoothHudAndSnapWhenDisabled)
{
	CSmoothUiSectionAnimation Style, Gradient, Accent, Opacity, Background;
	for(CSmoothUiSectionAnimation *pSection : {&Style, &Gradient, &Accent, &Opacity, &Background})
		EXPECT_FLOAT_EQ(pSection->Update(false, 0.0f, 180, true), 0.0f);

	const float StyleOpen = Style.Update(true, 0.09f, 180, true);
	const float GradientOpen = Gradient.Update(true, 0.09f, 180, true);
	const float AccentOpen = Accent.Update(true, 0.09f, 180, true);
	const float OpacityOpen = Opacity.Update(true, 0.09f, 180, true);
	const float BackgroundOpen = Background.Update(true, 0.09f, 180, true);
	for(float Progress : {StyleOpen, GradientOpen, AccentOpen, OpacityOpen, BackgroundOpen})
	{
		EXPECT_GT(Progress, 0.0f);
		EXPECT_LT(Progress, 1.0f);
	}

	for(CSmoothUiSectionAnimation *pSection : {&Style, &Gradient, &Accent, &Opacity, &Background})
	{
		const float ClosingProgress = pSection->Update(false, 0.045f, 180, true);
		EXPECT_GT(ClosingProgress, 0.0f);
		EXPECT_LT(ClosingProgress, 1.0f);
		EXPECT_TRUE(pSection->IsVisible());
	}

	EXPECT_FLOAT_EQ(Style.Update(false, 0.09f, 180, false), 0.0f);
	EXPECT_FLOAT_EQ(Gradient.Update(false, 0.09f, 180, false), 0.0f);
	EXPECT_FLOAT_EQ(Accent.Update(false, 0.09f, 180, false), 0.0f);
	EXPECT_FLOAT_EQ(Opacity.Update(false, 0.09f, 180, false), 0.0f);
	EXPECT_FLOAT_EQ(Background.Update(false, 0.09f, 180, false), 0.0f);
	EXPECT_FALSE(Style.IsVisible());
	EXPECT_FALSE(Gradient.IsVisible());
	EXPECT_FALSE(Accent.IsVisible());
	EXPECT_FALSE(Opacity.IsVisible());
	EXPECT_FALSE(Background.IsVisible());

	CSmoothUiSectionAnimation Instant;
	EXPECT_FLOAT_EQ(Instant.Update(true, 0.016f, 180, false), 1.0f);
	EXPECT_TRUE(Instant.AcceptsInput());
	EXPECT_FLOAT_EQ(Instant.Update(false, 0.016f, 180, false), 0.0f);
}

TEST(AmfUiThemes, ConditionalSectionHeightsStayBoundedAndReserveFollowingRows)
{
	const auto Closed = AmfUiTheme::MakeSettingsAnimationLayout(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 25.0f, 30.0f, 20.0f, 5.0f);
	const auto MidTransition = AmfUiTheme::MakeSettingsAnimationLayout(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 25.0f, 30.0f, 20.0f, 5.0f);
	const auto Open = AmfUiTheme::MakeSettingsAnimationLayout(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 25.0f, 30.0f, 20.0f, 5.0f);
	const auto OpenWithoutOpacity = AmfUiTheme::MakeSettingsAnimationLayout(1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 25.0f, 30.0f, 20.0f, 5.0f);

	EXPECT_FLOAT_EQ(Closed.m_TotalDetailsHeight, 105.0f);
	EXPECT_FLOAT_EQ(MidTransition.m_StyleVisibleHeight, 67.5f);
	EXPECT_FLOAT_EQ(MidTransition.m_GradientVisibleHeight, 27.5f);
	EXPECT_FLOAT_EQ(MidTransition.m_AccentVisibleHeight, 15.0f);
	EXPECT_FLOAT_EQ(MidTransition.m_OpacityVisibleHeight, 12.5f);
	EXPECT_FLOAT_EQ(MidTransition.m_BackgroundColorVisibleHeight, 15.0f);
	EXPECT_FLOAT_EQ(Open.m_TotalDetailsHeight, 325.0f);
	EXPECT_FLOAT_EQ(OpenWithoutOpacity.m_TotalDetailsHeight, 300.0f);
	EXPECT_GT(MidTransition.m_GradientTargetHeight, 0.0f);
	EXPECT_GT(MidTransition.m_AccentTargetHeight, 0.0f);
	EXPECT_GT(MidTransition.m_OpacityTargetHeight, 0.0f);
	EXPECT_GE(MidTransition.m_StyleVisibleHeight, 0.0f);
	EXPECT_GE(MidTransition.m_GradientVisibleHeight, 0.0f);
	EXPECT_GE(MidTransition.m_AccentVisibleHeight, 0.0f);
	EXPECT_GE(MidTransition.m_BackgroundColorVisibleHeight, 0.0f);
	EXPECT_GT(MidTransition.m_BackgroundToggleOffset, 0.0f);
	EXPECT_GE(MidTransition.m_BackgroundColorOffset, MidTransition.m_BackgroundToggleOffset + 25.0f);
	EXPECT_GE(MidTransition.m_ExportOffset, MidTransition.m_BackgroundColorOffset + MidTransition.m_BackgroundColorVisibleHeight);
	EXPECT_GE(MidTransition.m_TotalDetailsHeight, MidTransition.m_ExportOffset + 25.0f);
	EXPECT_GT(MidTransition.m_TotalDetailsHeight, Closed.m_TotalDetailsHeight);
	EXPECT_LT(MidTransition.m_TotalDetailsHeight, Open.m_TotalDetailsHeight);
}

namespace
{
struct SLayoutRect
{
	float x, y, w, h;
};
}

TEST(AmfFeaturePort, VisualColumnsStartTogetherAndAdvanceIndependently)
{
	SLayoutRect Left{0, 40, 300, 600}, Right{320, 40, 300, 600};
	const auto ImprovedUi = AmfSettingsPolicy::TakeCard(Left, 120);
	const auto FlyingNameplates = AmfSettingsPolicy::TakeCard(Right, 50);
	EXPECT_FLOAT_EQ(ImprovedUi.y, FlyingNameplates.y);
	const auto Themes = AmfSettingsPolicy::TakeCard(Left, 50);
	EXPECT_FLOAT_EQ(Themes.y, ImprovedUi.y + ImprovedUi.h + AmfSettingsPolicy::SectionGap);
	EXPECT_FLOAT_EQ(Left.y, 226);
	EXPECT_FLOAT_EQ(Right.y, 98);
	const auto SmoothHud = AmfSettingsPolicy::TakeCard(Left, 160);
	const auto Weather = AmfSettingsPolicy::TakeCard(Left, 90);
	const auto Hook = AmfSettingsPolicy::TakeCard(Left, 200);
	EXPECT_GE(Weather.y, SmoothHud.y + SmoothHud.h + AmfSettingsPolicy::SectionGap);
	EXPECT_GE(Hook.y, Weather.y + Weather.h + AmfSettingsPolicy::SectionGap);
	EXPECT_FLOAT_EQ(Right.y, 98);
}

TEST(AmfFeaturePort, VisualRightColumnKeepsExactRequestedOrderAndGaps)
{
	for(float StatisticsHeight : {70.0f, 80.5f, 110.0f})
	{
		SLayoutRect Right{320, 40, 300, 600};
		const auto Aspect = AmfSettingsPolicy::TakeCard(Right, 170);
		const auto Statistics = AmfSettingsPolicy::TakeCard(Right, StatisticsHeight);
		const auto EnhancedWeapons = AmfSettingsPolicy::TakeCard(Right, 420);
		const auto PhysicBalls = AmfSettingsPolicy::TakeCard(Right, 150);
		const auto FreezeLifestyle = AmfSettingsPolicy::TakeCard(Right, 190);
		const auto FlyingNameplates = AmfSettingsPolicy::TakeCard(Right, 145);
		const auto TuneZoneColors = AmfSettingsPolicy::TakeCard(Right, 60);
		const auto MusicPlayer = AmfSettingsPolicy::TakeCard(Right, 220);
		const auto KeyIndicator = AmfSettingsPolicy::TakeCard(Right, 90);
		const auto GradientNames = AmfSettingsPolicy::TakeCard(Right, 360);
		const auto MotionBlur = AmfSettingsPolicy::TakeCard(Right, 80);

		const SLayoutRect aCards[] = {
			Aspect,
			Statistics,
			EnhancedWeapons,
			PhysicBalls,
			FreezeLifestyle,
			FlyingNameplates,
			TuneZoneColors,
			MusicPlayer,
			KeyIndicator,
			GradientNames,
			MotionBlur};
		int OverlapCount = 0;
		int TouchingCount = 0;
		for(size_t i = 1; i < std::size(aCards); ++i)
		{
			const float PreviousBottom = aCards[i - 1].y + aCards[i - 1].h;
			if(aCards[i].y < PreviousBottom)
				++OverlapCount;
			if(aCards[i].y == PreviousBottom)
				++TouchingCount;
			EXPECT_GE(aCards[i].y, PreviousBottom + AmfSettingsPolicy::SectionGap);
		}
		EXPECT_EQ(OverlapCount, 0);
		EXPECT_EQ(TouchingCount, 0);
	}
}

TEST(AmfFeaturePort, TuneZonePresetSelectorAnimatesAndSnapsWithSmoothHud)
{
	CSmoothUiSectionAnimation Selector;
	EXPECT_FLOAT_EQ(Selector.Update(false, 0.0f, 180, true), 0.0f);
	EXPECT_FALSE(Selector.IsVisible());

	const float OpeningProgress = Selector.Update(true, 0.09f, 180, true);
	EXPECT_GT(OpeningProgress, 0.0f);
	EXPECT_LT(OpeningProgress, 1.0f);
	EXPECT_TRUE(Selector.IsVisible());
	const float AnimatedHeight = std::round(28.0f * OpeningProgress);
	EXPECT_GT(AnimatedHeight, 0.0f);
	EXPECT_LT(AnimatedHeight, 28.0f);

	const float ClosingProgress = Selector.Update(false, 0.045f, 180, true);
	EXPECT_GT(ClosingProgress, 0.0f);
	EXPECT_LT(ClosingProgress, OpeningProgress);
	EXPECT_TRUE(Selector.IsVisible());
	EXPECT_FLOAT_EQ(Selector.Update(false, 0.016f, 180, false), 0.0f);
	EXPECT_FALSE(Selector.IsVisible());

	EXPECT_FLOAT_EQ(Selector.Update(true, 0.016f, 180, false), 1.0f);
	EXPECT_TRUE(Selector.AcceptsInput());
	EXPECT_FLOAT_EQ(Selector.Update(false, 0.016f, 180, false), 0.0f);
}

TEST(AmfFeaturePort, EveryGeneralAndVisualCardHasPositiveSectionGap)
{
	// Covers variable/animated heights and content taller than the viewport;
	// scroll extent, not viewport clamping, determines the next card position.
	SLayoutRect Column{0, 40, 300, 100};
	float PreviousBottom = Column.y - AmfSettingsPolicy::SectionGap;
	for(float Height : {120.0f, 50.0f, 82.5f, 200.0f, 35.0f})
	{
		const auto Card = AmfSettingsPolicy::TakeCard(Column, Height);
		EXPECT_FLOAT_EQ(Card.y, PreviousBottom + AmfSettingsPolicy::SectionGap);
		EXPECT_GT(Card.y, PreviousBottom);
		PreviousBottom = Card.y + Card.h;
	}
}

TEST(AssetOrganizer, ItemContextPopupOpenRetargetCloseAndReopenLifecycle)
{
	AssetOrganizer::SItemContextPopupState Popup;
	int VisiblePopupCount = 0;
	CUIRect Screen{0.0f, 0.0f, 800.0f, 600.0f};
	CUIRect VisibleRect;
	auto RightClick = [&](int Tab, const char *pAsset, float AnchorX, float AnchorY) {
		const bool CreatePopup = Popup.Retarget(Tab, pAsset, AnchorX, AnchorY);
		if(CreatePopup)
			++VisiblePopupCount;
		VisibleRect = CUi::CalculatePopupMenuRect(Screen, Popup.m_AnchorX, Popup.m_AnchorY, 178.0f, 112.0f);
		EXPECT_TRUE(Popup.m_Visible);
		EXPECT_LE(VisiblePopupCount, 1);
	};

	EXPECT_FALSE(Popup.m_Visible);
	RightClick(AssetOrganizer::ASSET_ENTITIES, "tiles_a.png", 70.0f, 80.0f);
	EXPECT_EQ(VisiblePopupCount, 1);
	EXPECT_EQ(Popup.m_Tab, AssetOrganizer::ASSET_ENTITIES);
	EXPECT_EQ(Popup.m_AssetId, "tiles_a.png");
	const CUIRect PositionA = VisibleRect;

	RightClick(AssetOrganizer::ASSET_ENTITIES, "tiles_a.png", 70.0f, 80.0f);
	EXPECT_EQ(VisiblePopupCount, 1);
	for(int Click = 0; Click < 10; ++Click)
		RightClick(AssetOrganizer::ASSET_ENTITIES, "tiles_a.png", 70.0f, 80.0f);
	EXPECT_EQ(VisiblePopupCount, 1);
	EXPECT_TRUE(Popup.m_Visible);

	RightClick(AssetOrganizer::ASSET_GAME, "texture_b.png", 680.0f, 510.0f);
	EXPECT_EQ(Popup.m_Tab, AssetOrganizer::ASSET_GAME);
	EXPECT_EQ(Popup.m_AssetId, "texture_b.png");
	EXPECT_EQ(VisiblePopupCount, 1);
	const CUIRect PositionB = VisibleRect;
	EXPECT_NE(PositionA.x, PositionB.x);
	EXPECT_NE(PositionA.y, PositionB.y);
	EXPECT_LE(PositionB.x + PositionB.w, Screen.w - 5.0f);
	EXPECT_LE(PositionB.y + PositionB.h, Screen.h - 5.0f);

	// A scrolled card reports its current screen-space mouse anchor, not its
	// pre-scroll content coordinate.
	RightClick(AssetOrganizer::ASSET_GAME, "texture_c.png", 260.0f, 420.0f);
	EXPECT_EQ(Popup.m_AssetId, "texture_c.png");
	EXPECT_NE(VisibleRect.y, PositionB.y);
	const CUIRect PositionC = VisibleRect;

	// Synchronize against the actual UI popup registry after outside-click,
	// selection, Escape or page exit closes the item popup.
	Popup.SyncVisibility(false);
	VisiblePopupCount = 0;
	EXPECT_FALSE(Popup.m_Visible);
	EXPECT_EQ(Popup.m_Tab, -1);
	EXPECT_TRUE(Popup.m_AssetId.empty());
	EXPECT_FLOAT_EQ(Popup.m_AnchorX, 0.0f);
	EXPECT_FLOAT_EQ(Popup.m_AnchorY, 0.0f);
	EXPECT_EQ(VisiblePopupCount, 0);

	RightClick(AssetOrganizer::ASSET_ENTITIES, "tiles_d.png", 400.0f, 200.0f);
	EXPECT_EQ(VisiblePopupCount, 1);
	EXPECT_EQ(Popup.m_AssetId, "tiles_d.png");
	EXPECT_NE(VisibleRect.x, PositionC.x);
	EXPECT_NE(VisibleRect.y, PositionC.y);
}
