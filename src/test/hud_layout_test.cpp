#include "test.h"

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <game/client/components/hud_layout.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{
	class CTestConfigManager final : public IConfigManager
	{
	public:
		struct SLine
		{
			ConfigDomain m_Domain;
			std::string m_Line;
		};

	private:
		struct SCallback
		{
			SAVECALLBACKFUNC m_pfnCallback;
			void *m_pUserData;
			ConfigDomain m_Domain;
		};

		std::vector<SCallback> m_vCallbacks;

	public:
		std::vector<SLine> m_vLines;

		void Init() override {}
		void Reset(const char *pScriptName) override { (void)pScriptName; }
		void ResetGameSettings() override {}
		void SetReadOnly(const char *pScriptName, bool ReadOnly) override
		{
			(void)pScriptName;
			(void)ReadOnly;
		}
		void SetGameSettingsReadOnly(bool ReadOnly) override { (void)ReadOnly; }
		bool Save() override { return true; }
		bool SaveDomain(ConfigDomain Domain) override
		{
			for(const SCallback &Callback : m_vCallbacks)
			{
				if(Callback.m_Domain == Domain)
					Callback.m_pfnCallback(this, Callback.m_pUserData);
			}
			return true;
		}
		unsigned long long DomainHash(ConfigDomain Domain) const override
		{
			(void)Domain;
			return 0;
		}
		CConfig *Values() override { return &g_Config; }
		void RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData, ConfigDomain Domain) override
		{
			m_vCallbacks.push_back({pfnFunc, pUserData, Domain});
		}
		void WriteLine(const char *pLine, ConfigDomain Domain) override
		{
			m_vLines.push_back({Domain, pLine});
		}
		void StoreUnknownCommand(const char *pCommand) override { (void)pCommand; }
		void PossibleConfigVariables(const char *pStr, int FlagMask, POSSIBLECFGFUNC pfnCallback, void *pUserData) override
		{
			(void)pStr;
			(void)FlagMask;
			(void)pfnCallback;
			(void)pUserData;
		}
	};

	struct SHudLayoutTestContext
	{
		std::unique_ptr<IConsole> m_pConsole = CreateConsole(CFGFLAG_CLIENT);
		CTestConfigManager m_ConfigManager;

		SHudLayoutTestContext()
		{
			HudLayout::OnConsoleInit(m_pConsole.get(), &m_ConfigManager);
		}

		void Execute(const std::string &Line)
		{
			m_pConsole->ExecuteLine(Line.c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
		}
	};

	SHudLayoutTestContext &HudLayoutTestContext()
	{
		// Keep the console and config callback alive for the test process. HudLayout
		// registers its production callback once, matching the real client lifetime.
		static SHudLayoutTestContext s_Context;
		return s_Context;
	}

	const std::string *FindLayoutLine(const CTestConfigManager &ConfigManager, HudLayout::EModule Module)
	{
		const std::string Prefix = "hud_layout_set " + std::to_string(Module) + " ";
		const auto It = std::find_if(ConfigManager.m_vLines.begin(), ConfigManager.m_vLines.end(), [&](const CTestConfigManager::SLine &Line) {
			return Line.m_Domain == ConfigDomain::AMFHUDLAYOUT && Line.m_Line.compare(0, Prefix.size(), Prefix) == 0;
		});
		return It == ConfigManager.m_vLines.end() ? nullptr : &It->m_Line;
	}

	struct SRestoreHudConfig
	{
		int m_ClShowhudTimer = g_Config.m_ClShowhudTimer;
		int m_ClShowKillMessages = g_Config.m_ClShowKillMessages;
		int m_ClShowFinishMessages = g_Config.m_ClShowFinishMessages;
		int m_AmfFinishPrediction = g_Config.m_AmfFinishPrediction;

		~SRestoreHudConfig()
		{
			g_Config.m_ClShowhudTimer = m_ClShowhudTimer;
			g_Config.m_ClShowKillMessages = m_ClShowKillMessages;
			g_Config.m_ClShowFinishMessages = m_ClShowFinishMessages;
			g_Config.m_AmfFinishPrediction = m_AmfFinishPrediction;
		}
	};
} // namespace

TEST(HudLayout, CanonicalDefaultsAndPersistenceRoundTrips)
{
	auto &Context = HudLayoutTestContext();
	SRestoreHudConfig RestoreConfig;

	// An unconfigured layout starts from the source defaults.
	const auto FreshKillfeed = HudLayout::Get(HudLayout::MODULE_KILLFEED, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(FreshKillfeed.m_X, 500.0f);
	EXPECT_FLOAT_EQ(FreshKillfeed.m_Y, 25.0f);
	EXPECT_FALSE(HudLayout::HasPositionOverride(HudLayout::MODULE_KILLFEED));

	const auto PredictionTimeDefault = HudLayout::GetDefault(HudLayout::MODULE_PING, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(PredictionTimeDefault.m_Y, 20.0f);

	// These coordinates were read from the active user's module-18 config row
	// before changing the source default. Position is the only promoted property.
	constexpr float FinishPredictionX = 105.158f;
	constexpr float FinishPredictionY = 0.0f;
	const auto FreshFinishPrediction = HudLayout::Get(HudLayout::MODULE_FINISH_PREDICTION, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(FreshFinishPrediction.m_X, FinishPredictionX);
	EXPECT_FLOAT_EQ(FreshFinishPrediction.m_Y, FinishPredictionY);
	EXPECT_FALSE(HudLayout::HasPositionOverride(HudLayout::MODULE_FINISH_PREDICTION));
	const auto FinishPredictionDefault = HudLayout::GetDefault(HudLayout::MODULE_FINISH_PREDICTION, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(FinishPredictionDefault.m_X, FinishPredictionX);
	EXPECT_FLOAT_EQ(FinishPredictionDefault.m_Y, FinishPredictionY);
	EXPECT_EQ(FinishPredictionDefault.m_Scale, 100);
	EXPECT_EQ(FinishPredictionDefault.m_Mode, HudLayout::POSITION_MODE_TOP_LEFT);
	EXPECT_FALSE(FinishPredictionDefault.m_Enabled);
	EXPECT_TRUE(FinishPredictionDefault.m_BackgroundEnabled);
	EXPECT_EQ(FinishPredictionDefault.m_BackgroundColor, 0x66000000U);
	EXPECT_EQ(FinishPredictionDefault.m_Style, HudLayout::STYLE_CLASSIC);

	// Exercise the production save callback, then reload its emitted console line
	// through the same hud_layout_set command used by the config loader.
	constexpr float TimerX = 177.25f;
	constexpr float TimerY = 41.5f;
	HudLayout::SetPosition(HudLayout::MODULE_GAME_TIMER, TimerX, TimerY, HudLayout::POSITION_MODE_TOP_LEFT);
	Context.m_ConfigManager.m_vLines.clear();
	ASSERT_TRUE(Context.m_ConfigManager.SaveDomain(ConfigDomain::AMFHUDLAYOUT));
	const std::string *pTimerLine = FindLayoutLine(Context.m_ConfigManager, HudLayout::MODULE_GAME_TIMER);
	ASSERT_NE(pTimerLine, nullptr);
	const std::string TimerLine = *pTimerLine;
	EXPECT_NE(TimerLine.find("hud_layout_set 6 177.250 41.500"), std::string::npos);

	HudLayout::ResetSettings(HudLayout::MODULE_GAME_TIMER);
	Context.Execute(TimerLine);
	const auto ReloadedTimer = HudLayout::Get(HudLayout::MODULE_GAME_TIMER, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ReloadedTimer.m_X, TimerX);
	EXPECT_FLOAT_EQ(ReloadedTimer.m_Y, TimerY);
	EXPECT_TRUE(HudLayout::HasPositionOverride(HudLayout::MODULE_GAME_TIMER));

	// The Reset All action dispatches RESET_KIND_ALL to ResetSettings for every
	// collected module; verify the Game Timer and Killfeed reset targets here.
	HudLayout::SetPosition(HudLayout::MODULE_GAME_TIMER, 12.0f, 34.0f);
	HudLayout::ResetSettings(HudLayout::MODULE_GAME_TIMER);
	const auto TimerDefault = HudLayout::GetDefault(HudLayout::MODULE_GAME_TIMER, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	const auto ResetTimer = HudLayout::Get(HudLayout::MODULE_GAME_TIMER, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ResetTimer.m_X, TimerDefault.m_X);
	EXPECT_FLOAT_EQ(ResetTimer.m_Y, TimerDefault.m_Y);
	EXPECT_FALSE(HudLayout::HasPositionOverride(HudLayout::MODULE_GAME_TIMER));

	HudLayout::SetPosition(HudLayout::MODULE_KILLFEED, 320.0f, 80.0f);
	HudLayout::ResetSettings(HudLayout::MODULE_KILLFEED);
	const auto ResetKillfeed = HudLayout::Get(HudLayout::MODULE_KILLFEED, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ResetKillfeed.m_X, 500.0f);
	EXPECT_FLOAT_EQ(ResetKillfeed.m_Y, 25.0f);
	EXPECT_FALSE(HudLayout::HasPositionOverride(HudLayout::MODULE_KILLFEED));

	// ResetSettings is the operation reached by Reset All for this editor module.
	HudLayout::SetPosition(HudLayout::MODULE_FINISH_PREDICTION, 321.0f, 88.0f);
	HudLayout::ResetSettings(HudLayout::MODULE_FINISH_PREDICTION);
	const auto ResetFinishPrediction = HudLayout::Get(HudLayout::MODULE_FINISH_PREDICTION, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ResetFinishPrediction.m_X, FinishPredictionX);
	EXPECT_FLOAT_EQ(ResetFinishPrediction.m_Y, FinishPredictionY);
	EXPECT_FALSE(HudLayout::HasPositionOverride(HudLayout::MODULE_FINISH_PREDICTION));

	// Loading a pre-existing custom layout must keep its literal coordinates;
	// changing the canonical default does not rewrite it at startup.
	const std::string ExistingKillfeedLayout = "hud_layout_set 17 420.000 71.000 100 0 0 1711276032 1 0";
	Context.Execute(ExistingKillfeedLayout);
	const auto ExistingKillfeed = HudLayout::Get(HudLayout::MODULE_KILLFEED, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ExistingKillfeed.m_X, 420.0f);
	EXPECT_FLOAT_EQ(ExistingKillfeed.m_Y, 71.0f);
	EXPECT_TRUE(HudLayout::HasPositionOverride(HudLayout::MODULE_KILLFEED));
	Context.m_ConfigManager.m_vLines.clear();
	ASSERT_TRUE(Context.m_ConfigManager.SaveDomain(ConfigDomain::AMFHUDLAYOUT));
	const std::string *pExistingKillfeedLine = FindLayoutLine(Context.m_ConfigManager, HudLayout::MODULE_KILLFEED);
	ASSERT_NE(pExistingKillfeedLine, nullptr);
	EXPECT_NE(pExistingKillfeedLine->find("hud_layout_set 17 420.000 71.000"), std::string::npos);

	// A previously saved source-default row is also kept verbatim on ordinary
	// load/save; adopting a new default is not a startup migration.
	HudLayout::ResetSettings(HudLayout::MODULE_KILLFEED);
	Context.Execute("hud_layout_set 17 490.000 5.000 100 0 0 1711276032 1 0");
	const auto LegacyKillfeed = HudLayout::Get(HudLayout::MODULE_KILLFEED, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(LegacyKillfeed.m_X, 490.0f);
	EXPECT_FLOAT_EQ(LegacyKillfeed.m_Y, 5.0f);
	Context.m_ConfigManager.m_vLines.clear();
	ASSERT_TRUE(Context.m_ConfigManager.SaveDomain(ConfigDomain::AMFHUDLAYOUT));
	const std::string *pLegacyKillfeedLine = FindLayoutLine(Context.m_ConfigManager, HudLayout::MODULE_KILLFEED);
	ASSERT_NE(pLegacyKillfeedLine, nullptr);
	EXPECT_NE(pLegacyKillfeedLine->find("hud_layout_set 17 490.000 5.000"), std::string::npos);

	// A separately saved Finish Prediction override remains literal on load/save.
	const std::string ExistingFinishPredictionLayout = "hud_layout_set 18 321.000 88.000 100 0 1 1711276032 0 0";
	Context.Execute(ExistingFinishPredictionLayout);
	const auto ExistingFinishPrediction = HudLayout::Get(HudLayout::MODULE_FINISH_PREDICTION, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT);
	EXPECT_FLOAT_EQ(ExistingFinishPrediction.m_X, 321.0f);
	EXPECT_FLOAT_EQ(ExistingFinishPrediction.m_Y, 88.0f);
	EXPECT_TRUE(HudLayout::HasPositionOverride(HudLayout::MODULE_FINISH_PREDICTION));
	Context.m_ConfigManager.m_vLines.clear();
	ASSERT_TRUE(Context.m_ConfigManager.SaveDomain(ConfigDomain::AMFHUDLAYOUT));
	const std::string *pExistingFinishPredictionLine = FindLayoutLine(Context.m_ConfigManager, HudLayout::MODULE_FINISH_PREDICTION);
	ASSERT_NE(pExistingFinishPredictionLine, nullptr);
	EXPECT_NE(pExistingFinishPredictionLine->find("hud_layout_set 18 321.000 88.000"), std::string::npos);

	HudLayout::ResetSettings(HudLayout::MODULE_GAME_TIMER);
	HudLayout::ResetSettings(HudLayout::MODULE_KILLFEED);
	HudLayout::ResetSettings(HudLayout::MODULE_FINISH_PREDICTION);
}
