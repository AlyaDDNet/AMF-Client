#include "test.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/storage.h>

#include <game/client/components/tclient/amf_settings_search_match.h>
#include <game/localization.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <tuple>

namespace
{
bool WriteCatalog(IStorage *pStorage, const char *pPath, const std::string &Contents)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const bool Written = io_write(File, Contents.data(), Contents.size()) == Contents.size();
	return io_close(File) == 0 && Written;
}

const char *Lookup(const CLocalizationDatabase &Database, const char *pKey, const char *pContext)
{
	const char *pTranslation = Database.FindString(str_quickhash(pKey), str_quickhash(pContext));
	return pTranslation ? pTranslation : pKey;
}
} // namespace

TEST(LocalizationRuntime, AmfProcessPriorityLabelsResolveFromLoadedCatalogs)
{
	CTestInfo TestInfo;
	auto pStorage = TestInfo.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("languages", IStorage::TYPE_SAVE));

	const char *pSpotifyKey = "Low Priority Spotify | SoundCloud";
	const std::string Russian =
		"Low Priority Discord\n== Низкий приоритет Discord\n\n"
		"Low Priority Spotify | SoundCloud\n== Низкий приоритет Spotify | SoundCloud\n";
	ASSERT_TRUE(WriteCatalog(pStorage.get(), "languages/russian.txt", Russian));

	CLocalizationDatabase Database;
	ASSERT_TRUE(Database.Load("languages/russian.txt", pStorage.get(), nullptr));
	EXPECT_STREQ(Lookup(Database, "Low Priority Discord", "AMF Client"), "Низкий приоритет Discord");
	EXPECT_STREQ(Lookup(Database, pSpotifyKey, "AMF Client"), "Низкий приоритет Spotify | SoundCloud");

	const std::string Ukrainian =
		"Low Priority Discord\n== Низький пріоритет Discord\n\n"
		"Low Priority Spotify | SoundCloud\n== Низький пріоритет Spotify | SoundCloud\n";
	ASSERT_TRUE(WriteCatalog(pStorage.get(), "languages/ukrainian.txt", Ukrainian));
	ASSERT_TRUE(Database.Load("languages/ukrainian.txt", pStorage.get(), nullptr));
	EXPECT_STREQ(Lookup(Database, "Low Priority Discord", "AMF Client"), "Низький пріоритет Discord");
	EXPECT_STREQ(Lookup(Database, pSpotifyKey, "AMF Client"), "Низький пріоритет Spotify | SoundCloud");

	CLocalizationDatabase English;
	EXPECT_STREQ(Lookup(English, pSpotifyKey, "AMF Client"), "Low Priority Spotify | SoundCloud");
	TestInfo.m_DeleteTestStorageFilesOnSuccess = true;
}

TEST(LocalizationRuntime, AmfLabelsResolveFromStagedRuntimeCatalogs)
{
	auto pStorage = CreateLocalStorage();
	ASSERT_NE(pStorage, nullptr);

	const char *pSpotifyKey = "Low Priority Spotify | SoundCloud";
	const char *pDiscordKey = "Low Priority Discord";
	const char *pHooksKey = "Custom Hooks";
	const char *pEffectsKey = "Custom Effects";

	CLocalizationDatabase English;
	EXPECT_STREQ(Lookup(English, pSpotifyKey, "AMF Client"), "Low Priority Spotify | SoundCloud");
	EXPECT_STREQ(Lookup(English, pHooksKey, "AMF Client"), "Custom Hooks");
	EXPECT_STREQ(Lookup(English, pEffectsKey, "AMF Client"), "Custom Effects");

	for(const auto &[pLanguage, pSpotify, pDiscord, pHooks, pEffects] : {
		std::tuple<const char *, const char *, const char *, const char *, const char *>{
			"russian", "Низкий приоритет Spotify | SoundCloud", "Низкий приоритет Discord", "Кастомные хуки", "Кастомные эффекты"},
		{"ukrainian", "Низький пріоритет Spotify | SoundCloud", "Низький пріоритет Discord", "Кастомні хуки", "Кастомні ефекти"}})
	{
		CLocalizationDatabase Database;
		const std::string Path = std::string("data/languages/") + pLanguage + ".txt";
		ASSERT_TRUE(Database.Load(Path.c_str(), pStorage.get(), nullptr)) << Path;
		EXPECT_STREQ(Lookup(Database, pSpotifyKey, "AMF Client"), pSpotify);
		EXPECT_STREQ(Lookup(Database, pDiscordKey, "AMF Client"), pDiscord);
		EXPECT_STREQ(Lookup(Database, pHooksKey, "AMF Client"), pHooks);
		EXPECT_STREQ(Lookup(Database, pEffectsKey, "AMF Client"), pEffects);
	}
}

TEST(LocalizationRuntime, AmfSettingsSearchMatchesLocalizedVisibleLabels)
{
	auto pStorage = CreateLocalStorage();
	ASSERT_NE(pStorage, nullptr);
	const char *pRunStatisticsKey = "Run Statistics";
	const char *pFocusModeKey = "Focus Mode";
	const char *pMusicPlayerKey = "Music Player";
	const char *pImpactAttributionKey = "Impact System by AMF Client";

	CLocalizationDatabase English;
	EXPECT_TRUE(AmfSettingsSearch::MatchesText(Lookup(English, pMusicPlayerKey, "AMF Client"), "Music"));
	EXPECT_STREQ(Lookup(English, pImpactAttributionKey, "AMF Client"), pImpactAttributionKey);

	for(const auto &[pLanguage, pExpectedRunStatistics, pExpectedFocusMode, pExpectedMusicPlayer, pExpectedAttribution] : {
		std::tuple<const char *, const char *, const char *, const char *, const char *>{
			"russian", "Статистика забегов", "Режим фокуса", "Музыкальный проигрыватель", "Система Impact от AMF Client"},
		{"ukrainian", "Статистика забігів", "Режим фокусу", "Музичний програвач", "Система Impact від AMF Client"}})
	{
		CLocalizationDatabase Database;
		const std::string Path = std::string("data/languages/") + pLanguage + ".txt";
		ASSERT_TRUE(Database.Load(Path.c_str(), pStorage.get(), nullptr)) << Path;
		const char *pRunStatistics = Lookup(Database, pRunStatisticsKey, "AMF Client");
		const char *pFocusMode = Lookup(Database, pFocusModeKey, "AMF Client");
		const char *pMusicPlayer = Lookup(Database, pMusicPlayerKey, "AMF Client");
		const char *pAttribution = Lookup(Database, pImpactAttributionKey, "AMF Client");
		EXPECT_STREQ(pRunStatistics, pExpectedRunStatistics);
		EXPECT_STREQ(pFocusMode, pExpectedFocusMode);
		EXPECT_STREQ(pMusicPlayer, pExpectedMusicPlayer);
		EXPECT_STREQ(pAttribution, pExpectedAttribution);
		// Exact runtime-resolved visible labels must be searchable.
		EXPECT_TRUE(AmfSettingsSearch::MatchesText(pRunStatistics, "статистика"));
		EXPECT_TRUE(AmfSettingsSearch::MatchesText(pFocusMode, "фокус"));
		EXPECT_TRUE(AmfSettingsSearch::MatchesText(pMusicPlayer, pLanguage == std::string("russian") ? "музык" : "музич"));
	}
}
