#include "test.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/client/amf_config_compat.h>
#include <engine/client/config_migrations.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/kernel.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/tune_zone_colors.h>
#include <game/client/components/asset_organizer.h>
#include <game/client/components/bind_migrations.h>
#include <game/client/components/tclient/amf_settings_tab_policy.h>
#include <game/client/components/tclient/profile_assets.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
struct SExpectedConfig
{
	const char *m_pScriptName;
	SConfigVariable::EVariableType m_Type;
	int m_Flags;
	int m_IntDefault;
	int m_Min;
	int m_Max;
	unsigned m_ColorDefault;
	const char *m_pStringDefault;
	size_t m_StringSize;
	const char *m_pDescription;
};

#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) \
	{#ScriptName, SConfigVariable::VAR_INT, Flags, Def, Min, Max, 0, nullptr, 0, Desc},
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) \
	{#ScriptName, SConfigVariable::VAR_COLOR, Flags, 0, 0, 0, Def, nullptr, 0, Desc},
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) \
	{#ScriptName, SConfigVariable::VAR_STRING, Flags, 0, 0, 0, 0, Def, Len, Desc},
constexpr SExpectedConfig s_aExpectedTClient[] = {
#include <engine/shared/config_variables_tclient.h>
};
constexpr SExpectedConfig s_aExpectedAmf[] = {
#include <engine/shared/config_variables_amf.h>
};
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR

#if defined(CONF_DEBUG)
static_assert(std::size(s_aExpectedAmf) == 310); // Four obsolete nameplate-workaround settings are not part of the AMF config.
#else
static_assert(std::size(s_aExpectedAmf) == 309); // cl_impact_history_preview remains debug-only.
#endif
static_assert(std::size(s_aExpectedTClient) == 185);

void CollectConfigVariable(const SConfigVariable *pVariable, void *pUserData)
{
	auto *pvVariables = static_cast<std::vector<const SConfigVariable *> *>(pUserData);
	pvVariables->push_back(pVariable);
}

const SConfigVariable *FindConfigVariable(const std::vector<const SConfigVariable *> &vpVariables, const char *pScriptName)
{
	for(const SConfigVariable *pVariable : vpVariables)
		if(str_comp(pVariable->m_pScriptName, pScriptName) == 0)
			return pVariable;
	return nullptr;
}

bool WriteStorageFile(IStorage *pStorage, const char *pPath, const std::string &Content)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const bool Written = io_write(File, Content.data(), Content.size()) == Content.size();
	const bool Closed = io_close(File) == 0;
	return Written && Closed;
}

std::string ReadStorageFile(IStorage *pStorage, const char *pPath)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return {};
	const int Length = io_length(File);
	if(Length < 0)
	{
		io_close(File);
		return {};
	}
	std::string Content(static_cast<size_t>(Length), '\0');
	const bool Read = io_read(File, Content.data(), Content.size()) == Content.size();
	const bool Closed = io_close(File) == 0;
	if(!Read || !Closed)
		return {};
	return Content;
}

bool StoreUnknownConfigCommand(const char *pCommand, void *pUserData)
{
	static_cast<IConfigManager *>(pUserData)->StoreUnknownCommand(pCommand);
	return true;
}

void CountConfigCallback(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	++*static_cast<int *>(pUserData);
	pfnCallback(pResult, pCallbackUserData);
}

class CConfigTestRig
{
	CTestInfo m_TestInfo;
	CConfig m_OriginalConfig;
	std::unique_ptr<IKernel> m_pKernel;
	std::unique_ptr<IStorage> m_pStorage;
	std::unique_ptr<IConsole> m_pConsole;
	std::unique_ptr<IConfigManager> m_pConfigManager;
	ClientConfigMigrations::SPersistedPredictionMargins m_PersistedPredictionMargins;

	void InitializeRuntime()
	{
		m_pKernel = std::unique_ptr<IKernel>(IKernel::Create());
		m_pKernel->RegisterInterface(m_pStorage.get(), false);
		m_pConsole = CreateConsole(CFGFLAG_CLIENT | CFGFLAG_SERVER);
		m_pKernel->RegisterInterface(m_pConsole.get(), false);
		m_pConfigManager.reset(CreateConfigManager());
		m_pKernel->RegisterInterface(m_pConfigManager.get(), false);
		m_pConsole->Init();
		m_pConfigManager->Init();
	}

public:
	CConfigTestRig() :
		m_OriginalConfig(g_Config)
	{
		m_pStorage = m_TestInfo.CreateTestStorage();
		if(m_pStorage)
			InitializeRuntime();
	}

	~CConfigTestRig()
	{
		g_Config = m_OriginalConfig;
	}

	bool Ready() const { return m_pStorage && m_pConsole && m_pConfigManager; }
	IStorage *Storage() const { return m_pStorage.get(); }
	IConsole *Console() const { return m_pConsole.get(); }
	IConfigManager *ConfigManager() const { return m_pConfigManager.get(); }
	const ClientConfigMigrations::SPersistedPredictionMargins &PersistedPredictionMargins() const { return m_PersistedPredictionMargins; }

	void Restart()
	{
		m_pConfigManager.reset();
		m_pConsole.reset();
		m_pKernel.reset();
		InitializeRuntime();
	}

	bool LoadAllConfigDomains()
	{
		m_PersistedPredictionMargins = ClientConfigMigrations::ReadPersistedPredictionMargins(m_pStorage.get());
		m_pConsole->SetUnknownCommandCallback(StoreUnknownConfigCommand, m_pConfigManager.get());
		bool Success = true;
		for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
		{
			const char *pPath = s_aConfigDomains[Domain].m_aConfigPath;
			if(m_pStorage->FileExists(pPath, IStorage::TYPE_ALL))
				Success = m_pConsole->ExecuteFile(pPath, IConsole::CLIENT_ID_UNSPECIFIED) && Success;
		}
		m_pConsole->SetUnknownCommandCallback(IConsole::EmptyUnknownCommandCallback, nullptr);
		return Success;
	}

	void CleanupTestFilesOnSuccess()
	{
		m_TestInfo.m_DeleteTestStorageFilesOnSuccess = true;
	}

	const char *TestDirectory() const { return m_TestInfo.m_aFilename; }
};

void ExpectMetadataMatches(const SExpectedConfig &Expected, ConfigDomain Domain, const std::vector<const SConfigVariable *> &vpVariables)
{
	const SConfigVariable *pActual = FindConfigVariable(vpVariables, Expected.m_pScriptName);
	ASSERT_NE(pActual, nullptr) << "Missing config command " << Expected.m_pScriptName;
	EXPECT_EQ(pActual->m_ConfigDomain, Domain) << Expected.m_pScriptName;
	EXPECT_EQ(pActual->m_Type, Expected.m_Type) << Expected.m_pScriptName;
	EXPECT_EQ(pActual->m_Flags, Expected.m_Flags) << Expected.m_pScriptName;
	EXPECT_TRUE(str_startswith(pActual->m_pHelp, Expected.m_pDescription)) << Expected.m_pScriptName;

	switch(Expected.m_Type)
	{
	case SConfigVariable::VAR_INT:
	{
		const auto *pInt = static_cast<const SIntConfigVariable *>(pActual);
		EXPECT_EQ(pInt->m_Default, Expected.m_IntDefault) << Expected.m_pScriptName;
		EXPECT_EQ(pInt->m_Min, Expected.m_Min) << Expected.m_pScriptName;
		EXPECT_EQ(pInt->m_Max, Expected.m_Max) << Expected.m_pScriptName;
		break;
	}
	case SConfigVariable::VAR_COLOR:
	{
		const auto *pColor = static_cast<const SColorConfigVariable *>(pActual);
		EXPECT_EQ(pColor->m_Default, Expected.m_ColorDefault) << Expected.m_pScriptName;
		break;
	}
	case SConfigVariable::VAR_STRING:
	{
		const auto *pString = static_cast<const SStringConfigVariable *>(pActual);
		EXPECT_STREQ(pString->m_pDefault, Expected.m_pStringDefault) << Expected.m_pScriptName;
		EXPECT_EQ(pString->m_MaxSize, Expected.m_StringSize) << Expected.m_pScriptName;
		break;
	}
	}
}

size_t CountOccurrences(const std::string &Text, const std::string &Needle)
{
	size_t Count = 0;
	for(size_t Position = 0; (Position = Text.find(Needle, Position)) != std::string::npos; Position += Needle.size())
		++Count;
	return Count;
}
} // namespace

TEST(ConfigCompatibility, AmfAndTClientDeclarationsKeepExactMetadataAndDomains)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());

	const std::array<const char *, ConfigDomain::NUM> aExpectedPaths = {
		"settings_ddnet.cfg",
		"settings_tclient.cfg",
		"settings_amfclient.cfg",
		"amf_hud_layout.cfg",
		"amf_asset_layout.cfg",
		"tclient_profiles.cfg",
		"tclient_chatbinds.cfg",
		"tclient_warlist.cfg",
	};
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
		EXPECT_STREQ(s_aConfigDomains[Domain].m_aConfigPath, aExpectedPaths[Domain]);

	std::vector<const SConfigVariable *> vpVariables;
	Rig.ConfigManager()->PossibleConfigVariables("", -1, CollectConfigVariable, &vpVariables);
	std::unordered_map<std::string, const SConfigVariable *> VariablesByName;
	for(const SConfigVariable *pVariable : vpVariables)
		EXPECT_TRUE(VariablesByName.emplace(pVariable->m_pScriptName, pVariable).second) << pVariable->m_pScriptName;

	for(const SExpectedConfig &Expected : s_aExpectedTClient)
		ExpectMetadataMatches(Expected, ConfigDomain::TCLIENT, vpVariables);
	for(const SExpectedConfig &Expected : s_aExpectedAmf)
		ExpectMetadataMatches(Expected, ConfigDomain::AMFCLIENT, vpVariables);
	const SConfigVariable *pExpandedNavigation = FindConfigVariable(vpVariables, "amf_settings_nav_expanded");
	ASSERT_NE(pExpandedNavigation, nullptr);
	ASSERT_EQ(pExpandedNavigation->m_Type, SConfigVariable::VAR_INT);
	const auto *pExpandedNavigationInt = static_cast<const SIntConfigVariable *>(pExpandedNavigation);
	EXPECT_EQ(pExpandedNavigationInt->m_Default, 0);
	EXPECT_EQ(pExpandedNavigationInt->m_Min, 0);
	EXPECT_EQ(pExpandedNavigationInt->m_Max, 7);
	EXPECT_EQ(FindConfigVariable(vpVariables, "amf_quick_statistics"), nullptr);
	for(const char *pFreezeLifestyleName : {"amf_freeze_pull", "amf_freeze_pull_color", "amf_freeze_pull_range", "amf_freeze_pull_strands", "amf_freeze_pull_shield", "amf_freeze_pull_sparks", "amf_freeze_pull_hook_color"})
		EXPECT_NE(FindConfigVariable(vpVariables, pFreezeLifestyleName), nullptr) << pFreezeLifestyleName;

	EXPECT_EQ(std::count_if(vpVariables.begin(), vpVariables.end(), [](const SConfigVariable *pVariable) { return pVariable->m_ConfigDomain == ConfigDomain::TCLIENT; }), std::size(s_aExpectedTClient));
	EXPECT_EQ(std::count_if(vpVariables.begin(), vpVariables.end(), [](const SConfigVariable *pVariable) { return pVariable->m_ConfigDomain == ConfigDomain::AMFCLIENT; }), std::size(s_aExpectedAmf));
}

TEST(ConfigCompatibility, PredictionMarginUsesVanillaConfigUnitsAndRemovesWorkaroundSettings)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	std::vector<const SConfigVariable *> vpVariables;
	Rig.ConfigManager()->PossibleConfigVariables("", -1, CollectConfigVariable, &vpVariables);

	const SConfigVariable *pMargin = FindConfigVariable(vpVariables, "cl_prediction_margin");
	ASSERT_NE(pMargin, nullptr);
	EXPECT_EQ(pMargin->m_ConfigDomain, ConfigDomain::DDNET);
	const auto *pMarginInt = static_cast<const SIntConfigVariable *>(pMargin);
	EXPECT_EQ(pMarginInt->m_Default, 10);
	EXPECT_EQ(pMarginInt->m_Min, 1);
	EXPECT_EQ(pMarginInt->m_Max, 300);

	const SConfigVariable *pManualBackup = FindConfigVariable(vpVariables, "amf_auto_margin_manual_value");
	ASSERT_NE(pManualBackup, nullptr);
	const auto *pManualBackupInt = static_cast<const SIntConfigVariable *>(pManualBackup);
	EXPECT_EQ(pManualBackupInt->m_Min, 0);
	EXPECT_EQ(pManualBackupInt->m_Max, 300);

	for(const char *pRemovedName : {"amf_name_plates_self_offset_enabled", "amf_name_plates_self_offset", "amf_name_plates_others_offset_enabled", "amf_name_plates_others_offset"})
		EXPECT_EQ(FindConfigVariable(vpVariables, pRemovedName), nullptr) << pRemovedName;
}

TEST(ConfigCompatibility, AmfSettingsTabHidingSharesPersistentMaskAndKeepsInformationVisible)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	constexpr int ExistingTClientMask = (1 << 1) | (1 << 4);
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_tclient.cfg", "tc_tclient_settings_tabs 18\n"));
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_TcTClientSettingsTabs, ExistingTClientMask);

	int Mask = g_Config.m_TcTClientSettingsTabs;
	Mask = AmfSettingsTabPolicy::SetHidden(Mask, AmfSettingsTabPolicy::PAGE_GENERAL, true);
	Mask = AmfSettingsTabPolicy::SetHidden(Mask, AmfSettingsTabPolicy::PAGE_VISUAL, true);
	EXPECT_EQ(Mask & ExistingTClientMask, ExistingTClientMask);
	EXPECT_TRUE(AmfSettingsTabPolicy::IsHidden(Mask, AmfSettingsTabPolicy::PAGE_GENERAL));
	EXPECT_TRUE(AmfSettingsTabPolicy::IsHidden(Mask, AmfSettingsTabPolicy::PAGE_VISUAL));
	EXPECT_FALSE(AmfSettingsTabPolicy::IsHidden(Mask, AmfSettingsTabPolicy::PAGE_INFORMATION));
	EXPECT_EQ(AmfSettingsTabPolicy::SetHidden(Mask, AmfSettingsTabPolicy::PAGE_INFORMATION, true), Mask);
	EXPECT_EQ(AmfSettingsTabPolicy::ResolvePage(Mask, AmfSettingsTabPolicy::PAGE_GENERAL), AmfSettingsTabPolicy::PAGE_INFORMATION);
	EXPECT_EQ(AmfSettingsTabPolicy::ResolvePage(Mask, AmfSettingsTabPolicy::PAGE_VISUAL), AmfSettingsTabPolicy::PAGE_INFORMATION);
	EXPECT_EQ(AmfSettingsTabPolicy::ResolvePage(Mask, AmfSettingsTabPolicy::PAGE_INFORMATION), AmfSettingsTabPolicy::PAGE_INFORMATION);
	EXPECT_EQ(AmfSettingsTabPolicy::SetHidden(Mask, AmfSettingsTabPolicy::PAGE_GENERAL, true), Mask);

	g_Config.m_TcTClientSettingsTabs = Mask;
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_tclient.cfg").find("tc_tclient_settings_tabs 210"), std::string::npos);

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_TcTClientSettingsTabs, Mask);
	EXPECT_EQ(g_Config.m_TcTClientSettingsTabs & ExistingTClientMask, ExistingTClientMask);
	EXPECT_TRUE(AmfSettingsTabPolicy::IsHidden(g_Config.m_TcTClientSettingsTabs, AmfSettingsTabPolicy::PAGE_GENERAL));
	EXPECT_TRUE(AmfSettingsTabPolicy::IsHidden(g_Config.m_TcTClientSettingsTabs, AmfSettingsTabPolicy::PAGE_VISUAL));
	EXPECT_FALSE(AmfSettingsTabPolicy::IsHidden(g_Config.m_TcTClientSettingsTabs, AmfSettingsTabPolicy::PAGE_INFORMATION));
}

TEST(ConfigCompatibility, ProfileAssetSelectionKeepsLegacySettingsAndSupportsIndependentAssets)
{
	using namespace TClientProfileAssets;
	EXPECT_EQ(EffectiveFlags(-1, 0, 0), 0);
	EXPECT_EQ(EffectiveFlags(-1, 1, 0), ENTITIES);
	EXPECT_EQ(EffectiveFlags(-1, 0, 1), GAME | PARTICLES | HUD | EXTRAS | CURSOR | ARROW);
	EXPECT_EQ(EffectiveFlags(-1, 1, 1), ENTITIES | GAME | PARTICLES | HUD | EXTRAS | CURSOR | ARROW);
	EXPECT_EQ(EffectiveFlags(EMOTICONS | AUDIO, 1, 1), EMOTICONS | AUDIO);
	EXPECT_EQ(EffectiveFlags(ALL | (1 << 15), 0, 0), ALL);
	EXPECT_EQ(EffectiveFlags(0, 1, 1), 0);
}

TEST(ConfigCompatibility, ProcessPrioritySettingsKeepDiscordAndMediaIndependentAcrossRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_discord_low_priority 1\namf_spotify_soundcloud_low_priority 0\n"));
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfDiscordLowPriority, 1);
	EXPECT_EQ(g_Config.m_AmfSpotifySoundCloudLowPriority, 0);

	Rig.Console()->ExecuteLine("amf_spotify_soundcloud_low_priority 1", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfDiscordLowPriority, 1);
	EXPECT_EQ(g_Config.m_AmfSpotifySoundCloudLowPriority, 1);
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_discord_low_priority 1"), std::string::npos);
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_spotify_soundcloud_low_priority 1"), std::string::npos);
}

TEST(ConfigCompatibility, LegacyFilesLoadSaveReloadAndRemainRoutedIdempotently)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	ASSERT_NE(Rig.Storage(), nullptr);
	Rig.CleanupTestFilesOnSuccess();
	const std::string Utf8Scheme = std::string("ac pf ") + "\xD1\x84";
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_save_settings 1\ncl_showfps 1\nlegacy_future_ddnet \"keep with spaces\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_tclient.cfg",
		"tc_statusbar_height 12\ntc_statusbar_height 13\ntc_statusbar_scheme \"ac pf \xD1\x84\"\ntc_translate_endpoint \"\"\ntc_show_center_color $12345678\nlegacy_future_tclient \"keep this too\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_anim_duration 287\namf_presence_client_id \"fixture client id\"\namf_gradient_color_1 $A1B2C3D4\namf_settings_nav_expanded 5\nlegacy_future_amf \"quoted value\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "amf_hud_layout.cfg", "legacy_hud_layout \"hud data\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "amf_asset_layout.cfg", "legacy_asset_layout \"asset data\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "tclient_profiles.cfg", "legacy_profile_data \"profile data\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "tclient_chatbinds.cfg", "legacy_chatbind_data \"chat data\"\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "tclient_warlist.cfg", "legacy_warlist_data \"war data\"\n"));

	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_ClShowfps, 1);
	EXPECT_EQ(g_Config.m_TcStatusBarHeight, 13); // Duplicate command semantics: last value wins.
	EXPECT_STREQ(g_Config.m_TcStatusBarScheme, Utf8Scheme.c_str());
	EXPECT_STREQ(g_Config.m_TcTranslateEndpoint, "");
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 287);
	EXPECT_STREQ(g_Config.m_AmfPresenceClientId, "fixture client id");
	EXPECT_EQ(g_Config.m_AmfSettingsNavExpanded, 5);
	const unsigned TClientColor = g_Config.m_TcShowCenterColor;
	const unsigned AmfColor = g_Config.m_AmfGradientColor1;
	EXPECT_NE(TClientColor, 0U);
	EXPECT_NE(AmfColor, 0U);

	ASSERT_TRUE(Rig.ConfigManager()->Save());
	const std::string DdnetSaved = ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg");
	const std::string TClientSaved = ReadStorageFile(Rig.Storage(), "settings_tclient.cfg");
	const std::string AmfSaved = ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg");
	EXPECT_NE(DdnetSaved.find("cl_showfps 1"), std::string::npos);
	EXPECT_EQ(DdnetSaved.find("tc_statusbar_height"), std::string::npos);
	EXPECT_NE(TClientSaved.find("tc_statusbar_height 13"), std::string::npos);
	EXPECT_NE(TClientSaved.find("tc_statusbar_scheme \"ac pf "), std::string::npos);
	EXPECT_EQ(TClientSaved.find("tc_translate_endpoint"), std::string::npos); // Empty strings equal to the default remain omitted, as before.
	EXPECT_NE(TClientSaved.find("tc_show_center_color "), std::string::npos);
	EXPECT_NE(AmfSaved.find("amf_anim_duration 287"), std::string::npos);
	EXPECT_NE(AmfSaved.find("amf_settings_nav_expanded 5"), std::string::npos);
	EXPECT_NE(AmfSaved.find("amf_presence_client_id \"fixture client id\""), std::string::npos);
	EXPECT_NE(AmfSaved.find("amf_gradient_color_1 "), std::string::npos);
	EXPECT_NE(DdnetSaved.find("legacy_future_ddnet \"keep with spaces\""), std::string::npos);
	EXPECT_NE(DdnetSaved.find("legacy_hud_layout \"hud data\""), std::string::npos);
	for(ConfigDomain Domain = ConfigDomain::AMFHUDLAYOUT; Domain < ConfigDomain::NUM; ++Domain)
		EXPECT_TRUE(Rig.Storage()->FileExists(s_aConfigDomains[Domain].m_aConfigPath, IStorage::TYPE_ALL));

	// User changes must survive another start; reloading legacy config must not
	// overwrite them with stale values or multiply unknown-command lines.
	Rig.Console()->ExecuteLine("tc_statusbar_height 15", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_anim_duration 301", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_settings_nav_expanded 6", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_TcStatusBarHeight, 15);
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 301);
	EXPECT_EQ(g_Config.m_AmfSettingsNavExpanded, 6);
	EXPECT_EQ(g_Config.m_TcShowCenterColor, TClientColor);
	EXPECT_EQ(g_Config.m_AmfGradientColor1, AmfColor);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	const std::string DdnetSavedAgain = ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg");
	EXPECT_EQ(CountOccurrences(DdnetSavedAgain, "legacy_future_ddnet \"keep with spaces\""), 1U);
	EXPECT_EQ(CountOccurrences(DdnetSavedAgain, "legacy_hud_layout \"hud data\""), 1U);
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_tclient.cfg").find("tc_statusbar_height 15"), std::string::npos);
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_anim_duration 301"), std::string::npos);
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_settings_nav_expanded 6"), std::string::npos);

	char aResolvedPath[IO_MAX_PATH_LENGTH];
	Rig.Storage()->GetCompletePath(IStorage::TYPE_SAVE, "settings_ddnet.cfg", aResolvedPath, sizeof(aResolvedPath));
	EXPECT_NE(std::string(aResolvedPath).find(Rig.TestDirectory()), std::string::npos) << "Config compatibility tests must use only CTestInfo temporary storage";
}

TEST(ConfigCompatibility, IntegerBoundsAndInvalidInputKeepNativeConsoleSemantics)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	Rig.Console()->ExecuteLine("cl_save_settings 1", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_anim_duration -1", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 40);
	Rig.Console()->ExecuteLine("amf_anim_duration 9999", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 400);
	Rig.Console()->ExecuteLine("tc_statusbar_height 0", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_TcStatusBarHeight, 1);
	Rig.Console()->ExecuteLine("tc_statusbar_height 99", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_TcStatusBarHeight, 16);
	int CallbackCalls = 0;
	Rig.Console()->Chain("amf_anim_duration", CountConfigCallback, &CallbackCalls);
	Rig.Console()->ExecuteLine("amf_anim_duration not-a-number", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 400); // Invalid input is rejected by the console and leaves the old value intact.
	EXPECT_EQ(CallbackCalls, 0); // The parser rejects the invalid integer before the config callback chain.
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_anim_duration 400"), std::string::npos);

	// Invalid lines in a legacy config file also leave the current value intact.
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg", "amf_anim_duration not-a-number\n"));
	Rig.Restart();
	Rig.Console()->ExecuteLine("amf_anim_duration 400", IConsole::CLIENT_ID_UNSPECIFIED);
	CallbackCalls = 0;
	Rig.Console()->Chain("amf_anim_duration", CountConfigCallback, &CallbackCalls);
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 400);
	EXPECT_EQ(CallbackCalls, 0); // The invalid config line also never reaches the config callback.
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_amfclient.cfg").find("amf_anim_duration 400"), std::string::npos);

	Rig.Restart();
	CallbackCalls = 0;
	Rig.Console()->Chain("amf_anim_duration", CountConfigCallback, &CallbackCalls);
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfAnimDuration, 400);
	EXPECT_EQ(CallbackCalls, 1); // The saved valid line reloads through the callback exactly once.
}

TEST(ConfigCompatibility, PredictionMarginTenthsConvertToVanillaMillisecondsOnce)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 3\ncl_prediction_margin 12\n"));

	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 1); // 1.2 ms rounds to vanilla's whole-millisecond representation.
	EXPECT_EQ(g_Config.m_ClConfigVersion, 5);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg").find("cl_prediction_margin 1"), std::string::npos);

	for(const auto [MarginTenths, ExpectedMilliseconds] : {std::pair{24, 2}, std::pair{31, 3}})
	{
		char aOldConfig[96];
		str_format(aOldConfig, sizeof(aOldConfig), "cl_config_version 3\ncl_prediction_margin %d\n", MarginTenths);
		ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg", aOldConfig));
		Rig.Restart();
		ASSERT_TRUE(Rig.LoadAllConfigDomains());
		ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
		EXPECT_EQ(g_Config.m_ClPredictionMargin, ExpectedMilliseconds);
		ASSERT_TRUE(Rig.ConfigManager()->Save());
		char aExpected[64];
		str_format(aExpected, sizeof(aExpected), "cl_prediction_margin %d", ExpectedMilliseconds);
		EXPECT_NE(ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg").find(aExpected), std::string::npos);
	}
}

TEST(ConfigCompatibility, FreshBrowserFiltersDoNotHideServersAndSavedChoicesSurvive)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ClientConfigMigrations::ApplyFreshServerBrowserDefaults(g_Config, false);
	EXPECT_STREQ(g_Config.m_BrFilterString, "");
	EXPECT_EQ(g_Config.m_BrFilterLogin, 0);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Console()->ExecuteLine("br_filter_string Friends", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("br_filter_login 1", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_BrFilterString[0] = '\0';
	g_Config.m_BrFilterLogin = 0;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::ApplyFreshServerBrowserDefaults(g_Config, true);
	EXPECT_STREQ(g_Config.m_BrFilterString, "Friends");
	EXPECT_EQ(g_Config.m_BrFilterLogin, 1);

	Rig.Console()->ExecuteLine("br_filter_string \"\"", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("br_filter_login 0", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_BrFilterLogin = 1;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_STREQ(g_Config.m_BrFilterString, "");
	EXPECT_EQ(g_Config.m_BrFilterLogin, 0);
}

TEST(ConfigCompatibility, TranslatorDefaultsOffAndPersistsUserChoice)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	EXPECT_EQ(g_Config.m_TcTranslateAuto, 0);
	Rig.Console()->ExecuteLine("tc_translate_auto 1", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_TcTranslateAuto = 0;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_TcTranslateAuto, 1);
	Rig.Console()->ExecuteLine("tc_translate_auto 0", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_TcTranslateAuto = 1;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_TcTranslateAuto, 0);
}

TEST(ConfigCompatibility, SelectedAudioPackSurvivesSaveAndRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	Rig.Console()->ExecuteLine("cl_save_settings 1", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_STREQ(g_Config.m_SndPack, "default");
	Rig.Console()->ExecuteLine("snd_pack MyPack", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_STREQ(g_Config.m_SndPack, "MyPack");
	ASSERT_TRUE(Rig.ConfigManager()->SaveDomain(ConfigDomain::DDNET));
	const std::string SavedSelection = ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg");
	EXPECT_NE(SavedSelection.find("snd_pack \"MyPack\""), std::string::npos) << SavedSelection;

	str_copy(g_Config.m_SndPack, "default", sizeof(g_Config.m_SndPack));
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_STREQ(g_Config.m_SndPack, "MyPack");
	ASSERT_TRUE(Rig.ConfigManager()->SaveDomain(ConfigDomain::DDNET));
	const std::string ReloadedSelection = ReadStorageFile(Rig.Storage(), "settings_ddnet.cfg");
	EXPECT_NE(ReloadedSelection.find("snd_pack \"MyPack\""), std::string::npos) << ReloadedSelection;
}

TEST(ConfigCompatibility, RunStatisticsDefaultsOffAndExplicitChoiceSurvivesRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	EXPECT_EQ(g_Config.m_AmfImpactStatisticsAccess, 0);

	Rig.Console()->ExecuteLine("amf_impact_statistics_access 1", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_AmfImpactStatisticsAccess = 0;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfImpactStatisticsAccess, 1);

	Rig.Console()->ExecuteLine("amf_impact_statistics_access 0", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	g_Config.m_AmfImpactStatisticsAccess = 1;
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfImpactStatisticsAccess, 0);
}

TEST(ConfigCompatibility, LegacyFavoriteLoadDoesNotOverwriteSavedAssetFolders)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	AssetOrganizer::OnConsoleInit(Rig.Console(), Rig.ConfigManager());
	const std::string SavedLayout =
		"amf_asset_layout_folder \"game\" \"folder_1\" \"My Gunpacks\" 0 1\n"
		"amf_asset_layout_item \"game\" \"preset-a\" \"folder_1\" 0 0 0\n"
		"amf_asset_layout_view \"game\" \"folder_1\" 0\n";
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "amf_asset_layout.cfg", SavedLayout));

	// The legacy settings_tclient.cfg command executes before the layout file.
	AssetOrganizer::SetFavorite(AssetOrganizer::ASSET_GAME, "legacy-gunpack", true, false);
	EXPECT_EQ(ReadStorageFile(Rig.Storage(), "amf_asset_layout.cfg"), SavedLayout);
	ASSERT_TRUE(Rig.Console()->ExecuteFile("amf_asset_layout.cfg", IConsole::CLIENT_ID_UNSPECIFIED));
	std::set<std::string> LegacyFavorites = {"legacy-gunpack"};
	AssetOrganizer::EnsureItems(AssetOrganizer::ASSET_GAME, {"preset-a", "legacy-gunpack"}, LegacyFavorites);
	EXPECT_TRUE(AssetOrganizer::IsFavorite(AssetOrganizer::ASSET_GAME, "legacy-gunpack"));
	EXPECT_STREQ(AssetOrganizer::ItemFolder(AssetOrganizer::ASSET_GAME, "preset-a"), "folder_1");
	ASSERT_TRUE(Rig.ConfigManager()->SaveDomain(ConfigDomain::AMFASSETLAYOUT));
	const std::string RoundTrip = ReadStorageFile(Rig.Storage(), "amf_asset_layout.cfg");
	EXPECT_NE(RoundTrip.find("My Gunpacks"), std::string::npos);
	EXPECT_NE(RoundTrip.find("legacy-gunpack"), std::string::npos);
}

TEST(ConfigCompatibility, AutoMarginV3RestoresManualValueOnce)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 3\ncl_prediction_margin 575\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin 1\namf_auto_margin_manual_value 120\n"));
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 12);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 12);
	EXPECT_EQ(g_Config.m_ClConfigVersion, ClientConfigMigrations::CURRENT_CONFIG_VERSION);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 12);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 12);
}

TEST(ConfigCompatibility, RuntimeAutoMarginDoesNotReplaceSavedManualMargin)
{
	constexpr int SavedManual = 125;
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, SavedManual, false, 575), SavedManual);
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, SavedManual, true, 575), 300);
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, SavedManual, true, -1), SavedManual);
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(false, SavedManual, true, 575), 10);
}

TEST(ConfigCompatibility, UnversionedPredictionMarginPreservesWholeMillisecondsAndIsIdempotent)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 0\ncl_prediction_margin 50\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin_manual_value 50\n"));

	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 50);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 50);
	EXPECT_EQ(g_Config.m_ClConfigVersion, ClientConfigMigrations::CURRENT_CONFIG_VERSION);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 50);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 50);
}

TEST(ConfigCompatibility, KnownLegacyPredictionMarginsConvertOnceAndRoundTrip)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 1\ncl_prediction_margin 12\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin_manual_value 12\n"));

	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 12);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 12);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 12);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 12);
	EXPECT_EQ(g_Config.m_ClConfigVersion, ClientConfigMigrations::CURRENT_CONFIG_VERSION);
}

TEST(ConfigCompatibility, VersionTwoAndFourMarginsRecoverRawValuesBeforeConfigClamp)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();

	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 2\ncl_prediction_margin 1250\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin_manual_value 120\n")); // Version 2 backup used whole milliseconds.
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 300); // The parser clamps to the current maximum before migration.
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 125);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 120);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 4\ncl_prediction_margin 575\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin_manual_value 1250\n"));
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 300);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 300);
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 58);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 125);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 58);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 125);
}

TEST(ConfigCompatibility, ManualMarginAndAutoMarginRuntimeKeepCanonicalValueAcrossRestartAndToggle)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg",
		"cl_config_version 5\ncl_prediction_margin 42\n"));
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_amfclient.cfg",
		"amf_auto_margin 0\namf_auto_margin_manual_value 42\n"));
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 42);
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, g_Config.m_ClPredictionMargin, false, 200), 42);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 42);
	EXPECT_EQ(g_Config.m_AmfAutoMarginManualValue, 42);
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, g_Config.m_ClPredictionMargin, true, 200), 200);
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 42); // Auto runtime target never overwrites the manual setting.
	EXPECT_EQ(ClientConfigMigrations::EffectivePredictionMargin(true, g_Config.m_ClPredictionMargin, false, -1), 42);
}

TEST(ConfigCompatibility, FreshPredictionMarginDefaultIsNotScaled)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());

	EXPECT_EQ(g_Config.m_ClPredictionMargin, 10);
	ClientConfigMigrations::MigratePredictionMargins(g_Config, Rig.PersistedPredictionMargins());
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 10);
	EXPECT_EQ(g_Config.m_ClConfigVersion, ClientConfigMigrations::CURRENT_CONFIG_VERSION);

	// A legacy config version with no explicitly saved margin also keeps the
	// current vanilla default instead of converting it as if it were old data.
	g_Config.m_ClConfigVersion = 2;
	g_Config.m_ClPredictionMargin = 10;
	ClientConfigMigrations::MigratePredictionMargins(g_Config);
	EXPECT_EQ(g_Config.m_ClPredictionMargin, 10);
}

TEST(ConfigCompatibility, ExistingUnboundJumpSurvivesRepeatedDdraceBindUpgrade)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_ddnet.cfg", "cl_race_binds_set 2\n"));
	ASSERT_TRUE(Rig.LoadAllConfigDomains());

	std::array<std::string, KEY_LAST> aBinds{};
	aBinds[KEY_A] = "+custom-left";
	const auto ApplyPostLoadUpgrade = [&]() {
		if(BindMigrations::ShouldAssignJumpBind(g_Config.m_ClDDRaceBindsSet, true) && aBinds[KEY_W].empty())
			aBinds[KEY_W] = "+jump";
		g_Config.m_ClDDRaceBindsSet = BindMigrations::DDRACE_BIND_VERSION_WITH_JUMP_DEFAULT;
	};
	EXPECT_TRUE(aBinds[KEY_W].empty()); // Existing config explicitly leaves W unbound.
	ApplyPostLoadUpgrade();
	EXPECT_TRUE(aBinds[KEY_W].empty());
	EXPECT_EQ(aBinds[KEY_A], "+custom-left");
	EXPECT_EQ(g_Config.m_ClDDRaceBindsSet, 3);
	ASSERT_TRUE(Rig.ConfigManager()->Save());

	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	ApplyPostLoadUpgrade();
	EXPECT_TRUE(aBinds[KEY_W].empty());
	EXPECT_EQ(aBinds[KEY_A], "+custom-left");
}

TEST(ConfigCompatibility, FreshAndResetJumpDefaultsStillIncludeW)
{
	EXPECT_TRUE(BindMigrations::ShouldAssignJumpBind(0, false)); // SetDefaults uses the explicit non-FreeOnly pass.
	EXPECT_FALSE(BindMigrations::ShouldAssignJumpBind(2, true));
}

TEST(ConfigCompatibility, TuneZoneColorPresetsMapToRuntimeColors)
{
	static_assert(TuneZoneColor::PRESET_BLUE == 0);
	static_assert(TuneZoneColor::PRESET_RED == 1);
	static_assert(TuneZoneColor::PRESET_GREEN == 2);
	static_assert(TuneZoneColor::PRESET_PURPLE == 3);
	static_assert(TuneZoneColor::PRESET_YELLOW == 4);
	static_assert(TuneZoneColor::PRESET_WHITE == 5);
	static_assert(TuneZoneColor::PRESET_BLACK == 6);

	struct SRestoreTuneZoneColors
	{
		int m_Enabled;
		int m_Preset;
		~SRestoreTuneZoneColors()
		{
			g_Config.m_AmfTuneZoneColors = m_Enabled;
			g_Config.m_AmfTuneZoneColorPreset = m_Preset;
		}
	} Restore{g_Config.m_AmfTuneZoneColors, g_Config.m_AmfTuneZoneColorPreset};

	g_Config.m_AmfTuneZoneColors = 1;
	const std::array<ColorRGBA, TuneZoneColor::PRESET_COUNT> aExpectedColors = {
		TuneZoneColor::BLUE,
		TuneZoneColor::RED,
		TuneZoneColor::GREEN,
		TuneZoneColor::PURPLE,
		TuneZoneColor::YELLOW,
		TuneZoneColor::WHITE,
		TuneZoneColor::BLACK};
	const std::array<const char *, TuneZoneColor::PRESET_COUNT> aExpectedNames = {
		"standard", "red", "green", "purple", "yellow", "white", "black"};

	for(int Preset = TuneZoneColor::PRESET_BLUE; Preset < TuneZoneColor::PRESET_COUNT; ++Preset)
	{
		g_Config.m_AmfTuneZoneColorPreset = Preset;
		const ColorRGBA Expected = aExpectedColors[Preset];
		EXPECT_STREQ(TuneZoneColor::PresetName(Preset), aExpectedNames[Preset]);
		const ColorRGBA Current = TuneZoneColor::CurrentCustom();
		EXPECT_EQ(Current, Expected);
		for(const int TuneColorIndex : {1, 2, 127, 255})
		{
			const ColorRGBA SliceColor = TuneZoneColor::ColorForIndex(TuneColorIndex);
			EXPECT_EQ(SliceColor, Expected);
		}
	}

	EXPECT_EQ(TuneZoneColor::BLUE, ColorRGBA(0.25f, 0.50f, 0.95f, 1.0f)); // Standard retains the previous Blue value.
	EXPECT_EQ(TuneZoneColor::YELLOW, ColorRGBA(0.875f, 0.875f, 0.125f, 1.0f)); // Red's canonical HSL base rotated 60 degrees.
	EXPECT_EQ(TuneZoneColor::WHITE, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	EXPECT_EQ(TuneZoneColor::BLACK, ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_EQ(TuneZoneColor::RENDER_MULTIPLIER, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	g_Config.m_AmfTuneZoneColors = 0;
	for(const int TuneColorIndex : {0, 1, 127, 255})
		EXPECT_EQ(TuneZoneColor::ColorForIndex(TuneColorIndex), TuneZoneColor::WHITE);
}

TEST(ConfigCompatibility, TuneZoneColorTexturePresetsUseOneColorPipeline)
{
	const auto ColorizeGrayPixel = [](int Preset, uint8_t Gray) {
		CImageInfo Image;
		Image.m_Width = 1;
		Image.m_Height = 1;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.Allocate();
		Image.m_pData[0] = Gray;
		Image.m_pData[1] = Gray;
		Image.m_pData[2] = Gray;
		Image.m_pData[3] = 137;
		ColorizeWithPresetRect(Image, TuneZoneColor::PresetColor(Preset), 0, 0, 1, 1);
		return std::array<uint8_t, 4>{Image.m_pData[0], Image.m_pData[1], Image.m_pData[2], Image.m_pData[3]};
	};

	// Red/Green/Purple retain the established source-lightness HSL transform,
	// followed by the same preset RGB modulation that used to happen at draw time.
	for(const int Preset : {TuneZoneColor::PRESET_RED, TuneZoneColor::PRESET_GREEN, TuneZoneColor::PRESET_PURPLE})
	{
		const ColorRGBA PresetColor = TuneZoneColor::PresetColor(Preset);
		const ColorHSLA PresetHsla = color_cast<ColorHSLA>(PresetColor);
		ColorHSLA ExpectedHsla = color_cast<ColorHSLA>(ColorRGBA(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 137.0f / 255.0f));
		ExpectedHsla.h = PresetHsla.h;
		ExpectedHsla.s = PresetHsla.s;
		const ColorRGBA Expected = color_cast<ColorRGBA>(ExpectedHsla).Multiply(PresetColor);
		const auto Actual = ColorizeGrayPixel(Preset, 128);
		EXPECT_EQ(Actual[0], static_cast<uint8_t>(Expected.r * 255.0f));
		EXPECT_EQ(Actual[1], static_cast<uint8_t>(Expected.g * 255.0f));
		EXPECT_EQ(Actual[2], static_cast<uint8_t>(Expected.b * 255.0f));
		EXPECT_EQ(Actual[3], 137);
	}

	const ColorHSLA RedHsla = color_cast<ColorHSLA>(TuneZoneColor::RED);
	const ColorHSLA YellowHsla = color_cast<ColorHSLA>(TuneZoneColor::YELLOW);
	EXPECT_FLOAT_EQ(YellowHsla.h, 1.0f / 6.0f);
	EXPECT_FLOAT_EQ(YellowHsla.s, RedHsla.s);
	EXPECT_FLOAT_EQ(YellowHsla.l, RedHsla.l);
	for(const uint8_t Gray : {64, 128, 224})
	{
		const auto Yellow = ColorizeGrayPixel(TuneZoneColor::PRESET_YELLOW, Gray);
		EXPECT_EQ(Yellow[0], Yellow[1]);
		EXPECT_LT(Yellow[2], Yellow[1]);
		EXPECT_EQ(Yellow[3], 137);
		const ColorHSLA YellowResult = color_cast<ColorHSLA>(ColorRGBA(Yellow[0] / 255.0f, Yellow[1] / 255.0f, Yellow[2] / 255.0f, 1.0f));
		EXPECT_NEAR(YellowResult.h, 1.0f / 6.0f, 0.001f);
	}

	for(const uint8_t Gray : {64, 128, 224})
	{
		const auto White = ColorizeGrayPixel(TuneZoneColor::PRESET_WHITE, Gray);
		EXPECT_EQ(White[0], 255);
		EXPECT_EQ(White[1], 255);
		EXPECT_EQ(White[2], 255);
		EXPECT_EQ(White[3], 137);

		const auto Black = ColorizeGrayPixel(TuneZoneColor::PRESET_BLACK, Gray);
		EXPECT_EQ(Black[0], 0);
		EXPECT_EQ(Black[1], 0);
		EXPECT_EQ(Black[2], 0);
		EXPECT_EQ(Black[3], 137);
	}
}

TEST(ConfigCompatibility, TuneZoneColorsAndPresetPersistThroughRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	Rig.Console()->ExecuteLine("amf_tune_zone_colors 0", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_tune_zone_color_preset 6", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfTuneZoneColors, 0);
	EXPECT_EQ(g_Config.m_AmfTuneZoneColorPreset, TuneZoneColor::PRESET_BLACK);
}

TEST(ConfigCompatibility, EnhancedWeaponsPersistThroughCanonicalConfigRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	const char *apCommands[] = {
		"amf_laser_glow 1", "amf_laser_glow_power 31", "amf_laser_glow_mode 1",
		"amf_shotgun_glow 1", "amf_shotgun_glow_power 52", "amf_shotgun_glow_mode 2",
		"amf_rocket_glow 1", "amf_rocket_glow_power 73", "amf_rocket_glow_mode 3",
		"amf_meme_nuke_explosion 1", "amf_meme_nuke_shake 123", "amf_meme_nuke_scale 145"};
	for(const char *pCommand : apCommands)
		Rig.Console()->ExecuteLine(pCommand, IConsole::CLIENT_ID_UNSPECIFIED);
	g_Config.m_AmfLaserGlowColor = 12345678;
	g_Config.m_AmfShotgunGlowColor = 2345678;
	g_Config.m_AmfRocketGlowColor = 3456789;
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	Rig.Restart();
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	EXPECT_EQ(g_Config.m_AmfLaserGlow, 1);
	EXPECT_EQ(g_Config.m_AmfLaserGlowPower, 31);
	EXPECT_EQ(g_Config.m_AmfLaserGlowMode, 1);
	EXPECT_EQ(g_Config.m_AmfLaserGlowColor, 12345678U);
	EXPECT_EQ(g_Config.m_AmfShotgunGlow, 1);
	EXPECT_EQ(g_Config.m_AmfShotgunGlowPower, 52);
	EXPECT_EQ(g_Config.m_AmfShotgunGlowMode, 2);
	EXPECT_EQ(g_Config.m_AmfShotgunGlowColor, 2345678U);
	EXPECT_EQ(g_Config.m_AmfRocketGlow, 1);
	EXPECT_EQ(g_Config.m_AmfRocketGlowPower, 73);
	EXPECT_EQ(g_Config.m_AmfRocketGlowMode, 3);
	EXPECT_EQ(g_Config.m_AmfRocketGlowColor, 3456789U);
	EXPECT_EQ(g_Config.m_AmfMemeNukeExplosion, 1);
	EXPECT_EQ(g_Config.m_AmfMemeNukeShake, 123);
	EXPECT_EQ(g_Config.m_AmfMemeNukeScale, 145);
	Rig.Console()->ExecuteLine("amf_laser_glow_mode 99", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_rocket_glow_power -1", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_meme_nuke_scale 0", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_meme_nuke_shake 999", IConsole::CLIENT_ID_UNSPECIFIED);
	EXPECT_EQ(g_Config.m_AmfLaserGlowMode, 3);
	EXPECT_EQ(g_Config.m_AmfRocketGlowPower, 0);
	EXPECT_EQ(g_Config.m_AmfMemeNukeScale, 20);
	EXPECT_EQ(g_Config.m_AmfMemeNukeShake, 200);
}

TEST(ConfigCompatibility, LegacyAliasesImportOnlyOnceAndCanonicalValuesWinAfterRestart)
{
	CConfigTestRig Rig;
	ASSERT_TRUE(Rig.Ready());
	Rig.CleanupTestFilesOnSuccess();
	ASSERT_TRUE(WriteStorageFile(Rig.Storage(), "settings_tclient.cfg",
		"tc_input_mode 4\ntc_fast_input_amount 37\ntc_fast_input_others 1\ntc_discord_rpc 0\namf_smooth_hud_plus 1\n"));

	CAmfConfigCompatibility Compatibility;
	Compatibility.RegisterCommands(Rig.Console());
	Compatibility.BeginLoad(Rig.Storage());
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	Compatibility.FinishLoad(Rig.ConfigManager());
	EXPECT_EQ(g_Config.m_AmfInputMode, 4);
	EXPECT_EQ(g_Config.m_AmfFastInputAmount, 37);
	EXPECT_EQ(g_Config.m_AmfFastInputOthers, 1);
	EXPECT_EQ(g_Config.m_AmfDiscordRPC, 0);
	EXPECT_EQ(g_Config.m_AmfSmoothHud, 1);

	Rig.Console()->ExecuteLine("amf_input_mode 6", IConsole::CLIENT_ID_UNSPECIFIED);
	Rig.Console()->ExecuteLine("amf_discord_rpc 1", IConsole::CLIENT_ID_UNSPECIFIED);
	ASSERT_TRUE(Rig.ConfigManager()->Save());
	EXPECT_EQ(ReadStorageFile(Rig.Storage(), "settings_tclient.cfg").find("tc_input_mode"), std::string::npos);
	EXPECT_EQ(ReadStorageFile(Rig.Storage(), "settings_tclient.cfg").find("tc_discord_rpc"), std::string::npos);

	Rig.Restart();
	CAmfConfigCompatibility NextCompatibility;
	NextCompatibility.RegisterCommands(Rig.Console());
	NextCompatibility.BeginLoad(Rig.Storage());
	ASSERT_TRUE(Rig.LoadAllConfigDomains());
	NextCompatibility.FinishLoad(Rig.ConfigManager());
	EXPECT_EQ(g_Config.m_AmfInputMode, 6);
	EXPECT_EQ(g_Config.m_AmfDiscordRPC, 1);
	EXPECT_EQ(ReadStorageFile(Rig.Storage(), "settings_tclient.cfg").find("tc_input_mode"), std::string::npos);
}
