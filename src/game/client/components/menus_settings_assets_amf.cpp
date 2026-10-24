#include "menus.h"
#include "asset_organizer.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <game/mapitems.h>

#include <engine/font_icons.h>
#include <engine/config.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <unordered_set>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
	std::unordered_set<std::string> *m_pKnownAssetIds = nullptr;
};

// The game asset is the weapon/game texture pack. Packs may live below nested
// folders on disk, but their relative path remains the stable asset ID. The
// Asset Organizer keeps its separate user-facing folder/page arrangement in
// config, so it never needs to move these resource files.
struct SGameAssetScanUser
{
	CMenus *m_pMenus;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
	std::unordered_set<std::string> *m_pKnownPackIds = nullptr;
	char m_aRelativePath[IO_MAX_PATH_LENGTH] = {};
};

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	ASSETS_TAB_CURSOR = 6,
	ASSETS_TAB_ARROW = 7,
	ASSETS_TAB_AUDIO = 8,
	NUMBER_OF_ASSETS_TABS = 9,
};

// Preview textures have different transparent bounds from their visual content.
// Keep these corrections local to the organizer previews: gameplay rendering
// continues to use the unmodified native or custom texture paths.
static vec2 AssetPreviewOpticalOffset(int Tab, const char *pName, float Width, float Height)
{
	if(pName == nullptr || str_comp(pName, "default") != 0)
		return vec2(0.0f, 0.0f);
	if(Tab == ASSETS_TAB_CURSOR)
		// Alpha-weighted centroid of data/gui_cursor.png: (12.949, 29.410).
		return vec2(Width * (19.051f / 64.0f), Height * (2.590f / 64.0f));
	if(Tab == ASSETS_TAB_ARROW)
		// Alpha-weighted centroid of data/arrow.png: (23.125, 25.000).
		return vec2(Width * (0.875f / 48.0f), 0.0f);
	return vec2(0.0f, 0.0f);
}

// The alpha-weighted center of Font Awesome's MUSIC glyph is offset from its
// advance box. Scale the correction with the rendered font size so compact
// cards and the larger drag ghost share the same visual center.
static vec2 AudioPreviewOpticalOffset(float FontSize)
{
	return vec2(FontSize * (-12.943f / 160.0f), FontSize * (0.642f / 160.0f));
}

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		// Cache the flat-file fallback so packs without per-gametype variants don't get re-uploaded to the GPU MAP_IMAGE_MOD_TYPE_COUNT times.
		IGraphics::CTextureHandle FallbackTexture;
		bool FallbackAttempted = false;
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				if(!FallbackAttempted)
				{
					str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
					FallbackTexture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
					FallbackAttempted = true;
				}
				pEntitiesItem->m_aImages[i].m_Texture = FallbackTexture;
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vEntitiesList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		CMenus::LoadEntities(&EntitiesItem, pUser);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			CMenus::LoadEntities(&EntitiesItem, pUser);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	auto LoadPreviewTexture = [&](const char *pPath) {
		CImageInfo ImageInfo;
		if(pGraphics->LoadPng(ImageInfo, pPath, IStorage::TYPE_ALL))
		{
			if(ImageInfo.m_Width > 0 && ImageInfo.m_Height > 0)
				pAssetItem->m_PreviewAspect = (float)ImageInfo.m_Width / (float)ImageInfo.m_Height;
			pAssetItem->m_RenderTexture = pGraphics->LoadTextureRawMove(ImageInfo, 0, pPath);
			if(pAssetItem->m_RenderTexture.IsValid())
				return;
		}

		// Keep the original failure semantics (including the null texture) for
		// malformed or unavailable files. Successful assets take the one-decode path above.
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(pPath, IStorage::TYPE_ALL);
	};
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		LoadPreviewTexture(aPath);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		LoadPreviewTexture(aPath);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			LoadPreviewTexture(aPath);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, const char *pAssetName, IGraphics *pGraphics, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : vAssetList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			LoadAsset(&AssetItem, pAssetName, pGraphics);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pScanUser = static_cast<SGameAssetScanUser *>(pUser);
	CMenus *pThis = pScanUser->m_pMenus;

	auto AddPack = [pThis, pScanUser](const char *pRelativeName) {
		if(pRelativeName[0] == '\0' || str_length(pRelativeName) >= CMenus::SCustomItem::MAX_NAME_LENGTH)
			return;

		if(pScanUser->m_pKnownPackIds)
		{
			if(!pScanUser->m_pKnownPackIds->emplace(pRelativeName).second)
				return;
		}
		else
		{
			for(const auto &Item : pThis->m_vGameList)
				if(str_comp(Item.m_aName, pRelativeName) == 0)
					return;
		}

		SCustomGame GameItem;
		str_copy(GameItem.m_aName, pRelativeName);
		LoadAsset(&GameItem, "game", pThis->Graphics());
		pThis->m_vGameList.push_back(GameItem);
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		char aRelativePath[IO_MAX_PATH_LENGTH];
		if(pScanUser->m_aRelativePath[0] == '\0')
			str_copy(aRelativePath, pName);
		else
			str_format(aRelativePath, sizeof(aRelativePath), "%s/%s", pScanUser->m_aRelativePath, pName);

		char aDirectoryPath[IO_MAX_PATH_LENGTH];
		str_format(aDirectoryPath, sizeof(aDirectoryPath), "assets/game/%s", aRelativePath);
		char aGamePath[IO_MAX_PATH_LENGTH];
		str_format(aGamePath, sizeof(aGamePath), "%s/%s", aDirectoryPath, g_pData->m_aImages[IMAGE_GAME].m_pFilename);
		if(pThis->Storage()->FileExists(aGamePath, IStorage::TYPE_ALL))
		{
			AddPack(aRelativePath);
		}
		else
		{
			// ListDirectory is synchronous. The child context is therefore valid
			// for the entire recursive call and no persistent scan state is needed.
			SGameAssetScanUser ChildUser = *pScanUser;
			str_copy(ChildUser.m_aRelativePath, aRelativePath);
			pThis->Storage()->ListDirectory(IStorage::TYPE_ALL, aDirectoryPath, GameScan, &ChildUser);
		}
	}
	else if(str_endswith(pName, ".png"))
	{
		char aPackName[IO_MAX_PATH_LENGTH];
		str_truncate(aPackName, sizeof(aPackName), pName, str_length(pName) - 4);
		char aRelativeName[IO_MAX_PATH_LENGTH];
		if(pScanUser->m_aRelativePath[0] == '\0')
			str_copy(aRelativeName, aPackName);
		else
			str_format(aRelativeName, sizeof(aRelativeName), "%s/%s", pScanUser->m_aRelativePath, aPackName);
		AddPack(aRelativeName);
	}

	pScanUser->m_LoadedFunc();
	return 0;
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, "emoticons", pGraphics, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, "particles", pGraphics, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, "hud", pGraphics, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, "extras", pGraphics, pUser);
}

static int FavoriteAssetTabFromString(const char *pTab)
{
	if(str_comp_nocase(pTab, "entities") == 0)
		return ASSETS_TAB_ENTITIES;
	if(str_comp_nocase(pTab, "game") == 0)
		return ASSETS_TAB_GAME;
	if(str_comp_nocase(pTab, "emoticons") == 0)
		return ASSETS_TAB_EMOTICONS;
	if(str_comp_nocase(pTab, "particles") == 0)
		return ASSETS_TAB_PARTICLES;
	if(str_comp_nocase(pTab, "hud") == 0)
		return ASSETS_TAB_HUD;
	if(str_comp_nocase(pTab, "extras") == 0)
		return ASSETS_TAB_EXTRAS;
	if(str_comp_nocase(pTab, "cursor") == 0)
		return ASSETS_TAB_CURSOR;
	if(str_comp_nocase(pTab, "arrow") == 0)
		return ASSETS_TAB_ARROW;
	if(str_comp_nocase(pTab, "audio") == 0)
		return ASSETS_TAB_AUDIO;
	return -1;
}

static const char *FavoriteAssetTabToString(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES:
		return "entities";
	case ASSETS_TAB_GAME:
		return "game";
	case ASSETS_TAB_EMOTICONS:
		return "emoticons";
	case ASSETS_TAB_PARTICLES:
		return "particles";
	case ASSETS_TAB_HUD:
		return "hud";
	case ASSETS_TAB_EXTRAS:
		return "extras";
	case ASSETS_TAB_CURSOR:
		return "cursor";
	case ASSETS_TAB_ARROW:
		return "arrow";
	case ASSETS_TAB_AUDIO:
		return "audio";
	default:
		return "";
	}
}

static void LoadCursorPreview(CMenus::SCustomCursor *pCursorItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pCursorItem->m_aName, "default") == 0)
	{
		pCursorItem->m_RenderTexture = g_pData->m_aImages[IMAGE_CURSOR].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/cursor/%s.png", pCursorItem->m_aName);
	pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pCursorItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/cursor/%s/gui_cursor.png", pCursorItem->m_aName);
		pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pCursorItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/cursor/%s/cursor.png", pCursorItem->m_aName);
			pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

int CMenus::CursorScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vCursorList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomCursor CursorItem;
		str_copy(CursorItem.m_aName, pName);
		LoadCursorPreview(&CursorItem, pGraphics);
		pThis->m_vCursorList.push_back(CursorItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, aName);
			LoadCursorPreview(&CursorItem, pGraphics);
			pThis->m_vCursorList.push_back(CursorItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

static void LoadArrowPreview(CMenus::SCustomArrow *pArrowItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pArrowItem->m_aName, "default") == 0)
	{
		pArrowItem->m_RenderTexture = g_pData->m_aImages[IMAGE_ARROW].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/arrow/%s.png", pArrowItem->m_aName);
	pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pArrowItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pArrowItem->m_aName);
		pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
}

int CMenus::ArrowScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	auto Register = [&](const char *pItemName) {
		if(pRealUser->m_pKnownAssetIds)
			return pRealUser->m_pKnownAssetIds->emplace(pItemName).second;
		for(const auto &Item : pThis->m_vArrowList)
			if(str_comp(Item.m_aName, pItemName) == 0)
				return false;
		return true;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;
		if(!Register(pName))
			return 0;

		SCustomArrow ArrowItem;
		str_copy(ArrowItem.m_aName, pName);
		LoadArrowPreview(&ArrowItem, pGraphics);
		pThis->m_vArrowList.push_back(ArrowItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			if(!Register(aName))
				return 0;

			SCustomArrow ArrowItem;
			str_copy(ArrowItem.m_aName, aName);
			LoadArrowPreview(&ArrowItem, pGraphics);
			pThis->m_vArrowList.push_back(ArrowItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

int CMenus::AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	if(!IsDir || pName[0] == '.')
		return 0;
	if(str_comp(pName, "default") == 0)
		return 0;
	if(pRealUser->m_pKnownAssetIds)
	{
		if(!pRealUser->m_pKnownAssetIds->emplace(pName).second)
			return 0;
	}
	else
	{
		for(const auto &Item : pThis->m_vAudioPackList)
			if(str_comp(Item.m_aName, pName) == 0)
				return 0;
	}

	SCustomAudioPack PackItem;
	str_copy(PackItem.m_aName, pName);
	PackItem.m_RenderTexture = IGraphics::CTextureHandle();
	pThis->m_vAudioPackList.push_back(PackItem);

	pRealUser->m_LoadedFunc();
	return 0;
}

static void ClearCursorAssetList(std::vector<CMenus::SCustomCursor> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomCursor &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

static void ClearArrowAssetList(std::vector<CMenus::SCustomArrow> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomArrow &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ConAddFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	const int Tab = FavoriteAssetTabFromString(pResult->GetString(0));
	AssetOrganizer::SetFavorite(Tab, pResult->GetString(1), true, false);
	pSelf->AddFavoriteAsset(Tab, pResult->GetString(1));
}

void CMenus::ConRemoveFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	const int Tab = FavoriteAssetTabFromString(pResult->GetString(0));
	AssetOrganizer::SetFavorite(Tab, pResult->GetString(1), false, false);
	pSelf->RemoveFavoriteAsset(Tab, pResult->GetString(1));
}

void CMenus::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->OnConfigSave(pConfigManager);
}

void CMenus::OnConfigSave(IConfigManager *pConfigManager)
{
	for(int Tab = 0; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		const char *pTabName = FavoriteAssetTabToString(Tab);
		for(const auto &Favorite : m_aAssetFavorites[Tab])
		{
			char aBuffer[IO_MAX_PATH_LENGTH + 64];
			const char *pEnd = aBuffer + sizeof(aBuffer) - 2;

			str_copy(aBuffer, "add_favorite_asset \"");
			char *pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, pTabName, pEnd);
			str_append(aBuffer, "\" \"");
			pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, Favorite.c_str(), pEnd);
			str_append(aBuffer, "\"");

			pConfigManager->WriteLine(aBuffer, ConfigDomain::TCLIENT);
		}
	}
}

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true,
};

void CMenus::AddFavoriteAsset(const char *pTab, const char *pName)
{
	AddFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::RemoveFavoriteAsset(const char *pTab, const char *pName)
{
	RemoveFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::AddFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}
	if(pName[0] == '\0')
		return;

	const auto &[_, Inserted] = m_aAssetFavorites[Tab].emplace(pName);
	if(Inserted)
	{
		gs_aInitCustomList[Tab] = true;
	}
}

void CMenus::RemoveFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}

	const auto FavoriteIt = m_aAssetFavorites[Tab].find(pName);
	if(FavoriteIt != m_aAssetFavorites[Tab].end())
	{
		m_aAssetFavorites[Tab].erase(FavoriteIt);
		gs_aInitCustomList[Tab] = true;
	}
}

void CMenus::SetAssetOrganizerFavorite(int Tab, const char *pName, bool Favorite)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS || pName == nullptr || pName[0] == '\0')
		return;

	AssetOrganizer::SetFavorite(Tab, pName, Favorite);
	if(Favorite)
		AddFavoriteAsset(Tab, pName);
	else
		RemoveFavoriteAsset(Tab, pName);
}

bool CMenus::IsFavoriteAsset(int Tab, const char *pName) const
{
	return Tab >= 0 && Tab < NUMBER_OF_ASSETS_TABS && pName != nullptr && AssetOrganizer::IsFavorite(Tab, pName);
}

static void AssetsGetRelativePaths(int Tab, const char *pName, char *pFilePath, int FilePathSize, char *pFolderPath, int FolderPathSize, char *pAltFolderPath, int AltFolderPathSize)
{
	pFilePath[0] = '\0';
	pFolderPath[0] = '\0';
	if(pAltFolderPath != nullptr)
		pAltFolderPath[0] = '\0';

	const char *pTabFolder = FavoriteAssetTabToString(Tab);
	if(pTabFolder[0] == '\0' || pName == nullptr || pName[0] == '\0')
		return;

	if(Tab == ASSETS_TAB_AUDIO)
	{
		str_format(pFolderPath, FolderPathSize, "assets/audio/%s", pName);
		if(pAltFolderPath != nullptr)
			str_format(pAltFolderPath, AltFolderPathSize, "audio/%s", pName);
		return;
	}

	str_format(pFilePath, FilePathSize, "assets/%s/%s.png", pTabFolder, pName);
	str_format(pFolderPath, FolderPathSize, "assets/%s/%s", pTabFolder, pName);
}

bool CMenus::CanDeleteCustomAsset(int Tab, const char *pName) const
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS || pName == nullptr || pName[0] == '\0')
		return false;
	if(str_comp(pName, "default") == 0)
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	char aAltFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath), aAltFolderPath, sizeof(aAltFolderPath));

	if(aFilePath[0] != '\0' && Storage()->FileExists(aFilePath, IStorage::TYPE_SAVE))
		return true;
	if(aFolderPath[0] != '\0' && Storage()->FolderExists(aFolderPath, IStorage::TYPE_SAVE))
		return true;
	if(aAltFolderPath[0] != '\0' && Storage()->FolderExists(aAltFolderPath, IStorage::TYPE_SAVE))
		return true;
	return false;
}

struct SAssetsDeleteFolderContext
{
	IStorage *m_pStorage;
	char m_aBasePath[IO_MAX_PATH_LENGTH];
	bool m_Success = true;
	int m_Depth = 0;
};

static int AssetsDeleteFolderContents(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pContext = static_cast<SAssetsDeleteFolderContext *>(pUser);
	// Skip only "." / ".." — other dotfiles (e.g. .DS_Store) must be removed or RemoveFolder fails.
	if(pName[0] == '.' && (pName[1] == '\0' || (pName[1] == '.' && pName[2] == '\0')))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pContext->m_aBasePath, pName);
	if(IsDir)
	{
		if(pContext->m_Depth >= 16)
		{
			pContext->m_Success = false;
			return 0;
		}
		SAssetsDeleteFolderContext ChildContext;
		ChildContext.m_pStorage = pContext->m_pStorage;
		ChildContext.m_Depth = pContext->m_Depth + 1;
		str_copy(ChildContext.m_aBasePath, aPath);
		pContext->m_pStorage->ListDirectory(IStorage::TYPE_SAVE, aPath, AssetsDeleteFolderContents, &ChildContext);
		if(!ChildContext.m_Success || !pContext->m_pStorage->RemoveFolder(aPath, IStorage::TYPE_SAVE))
			pContext->m_Success = false;
	}
	else if(!pContext->m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE))
	{
		pContext->m_Success = false;
	}
	return 0;
}

static bool AssetsDeleteSavePath(IStorage *pStorage, const char *pPath, bool IsFolder)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return true;

	if(IsFolder)
	{
		if(!pStorage->FolderExists(pPath, IStorage::TYPE_SAVE))
			return true;

		SAssetsDeleteFolderContext Context;
		Context.m_pStorage = pStorage;
		str_copy(Context.m_aBasePath, pPath);
		pStorage->ListDirectory(IStorage::TYPE_SAVE, pPath, AssetsDeleteFolderContents, &Context);
		if(!Context.m_Success)
			return false;
		return pStorage->RemoveFolder(pPath, IStorage::TYPE_SAVE);
	}

	if(!pStorage->FileExists(pPath, IStorage::TYPE_SAVE))
		return true;
	return pStorage->RemoveFile(pPath, IStorage::TYPE_SAVE);
}

bool CMenus::DeleteCustomAsset(int Tab, const char *pName)
{
	if(!CanDeleteCustomAsset(Tab, pName))
		return false;

	char aFilePath[IO_MAX_PATH_LENGTH];
	char aFolderPath[IO_MAX_PATH_LENGTH];
	char aAltFolderPath[IO_MAX_PATH_LENGTH];
	AssetsGetRelativePaths(Tab, pName, aFilePath, sizeof(aFilePath), aFolderPath, sizeof(aFolderPath), aAltFolderPath, sizeof(aAltFolderPath));

	// Best-effort: remove every SAVE path. Success = nothing deletable remains
	// (handles partial deletes where e.g. the png is gone but a leftover folder stays).
	AssetsDeleteSavePath(Storage(), aFilePath, false);
	AssetsDeleteSavePath(Storage(), aFolderPath, true);
	AssetsDeleteSavePath(Storage(), aAltFolderPath, true);
	return !CanDeleteCustomAsset(Tab, pName);
}

static void AssetsResetSelectedToDefault(int Tab)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		str_copy(g_Config.m_ClAssetsEntities, "default");
	else if(Tab == ASSETS_TAB_GAME)
		str_copy(g_Config.m_ClAssetGame, "default");
	else if(Tab == ASSETS_TAB_EMOTICONS)
		str_copy(g_Config.m_ClAssetEmoticons, "default");
	else if(Tab == ASSETS_TAB_PARTICLES)
		str_copy(g_Config.m_ClAssetParticles, "default");
	else if(Tab == ASSETS_TAB_HUD)
		str_copy(g_Config.m_ClAssetHud, "default");
	else if(Tab == ASSETS_TAB_EXTRAS)
		str_copy(g_Config.m_ClAssetExtras, "default");
	else if(Tab == ASSETS_TAB_CURSOR)
		str_copy(g_Config.m_ClAssetCursor, "default");
	else if(Tab == ASSETS_TAB_ARROW)
		str_copy(g_Config.m_ClAssetArrow, "default");
	else if(Tab == ASSETS_TAB_AUDIO)
		str_copy(g_Config.m_SndPack, "default");
}

static bool AssetsIsCurrentlySelected(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
		return str_comp(pName, g_Config.m_ClAssetsEntities) == 0;
	if(Tab == ASSETS_TAB_GAME)
		return str_comp(pName, g_Config.m_ClAssetGame) == 0;
	if(Tab == ASSETS_TAB_EMOTICONS)
		return str_comp(pName, g_Config.m_ClAssetEmoticons) == 0;
	if(Tab == ASSETS_TAB_PARTICLES)
		return str_comp(pName, g_Config.m_ClAssetParticles) == 0;
	if(Tab == ASSETS_TAB_HUD)
		return str_comp(pName, g_Config.m_ClAssetHud) == 0;
	if(Tab == ASSETS_TAB_EXTRAS)
		return str_comp(pName, g_Config.m_ClAssetExtras) == 0;
	if(Tab == ASSETS_TAB_CURSOR)
		return str_comp(pName, g_Config.m_ClAssetCursor) == 0;
	if(Tab == ASSETS_TAB_ARROW)
		return str_comp(pName, g_Config.m_ClAssetArrow) == 0;
	if(Tab == ASSETS_TAB_AUDIO)
		return str_comp(pName, g_Config.m_SndPack) == 0;
	return false;
}

void CMenus::PopupConfirmDeleteAsset()
{
	if(m_DeleteAssetTab < 0 || m_DeleteAssetTab >= NUMBER_OF_ASSETS_TABS || m_aDeleteAssetName[0] == '\0')
		return;

	const int Tab = m_DeleteAssetTab;
	const bool WasSelected = AssetsIsCurrentlySelected(Tab, m_aDeleteAssetName);
	if(!DeleteCustomAsset(Tab, m_aDeleteAssetName))
	{
		char aError[128 + sizeof(m_aDeleteAssetName)];
		str_format(aError, sizeof(aError), Localize("Unable to delete the asset '%s'"), m_aDeleteAssetName);
		PopupMessage(Localize("Error"), aError, Localize("Ok"));
		m_aDeleteAssetName[0] = '\0';
		m_DeleteAssetTab = -1;
		return;
	}

	SetAssetOrganizerFavorite(Tab, m_aDeleteAssetName, false);
	if(WasSelected)
		AssetsResetSelectedToDefault(Tab);

	// Drop only the deleted entry — full ClearCustomItems reloads every preview texture and causes hitch.
	RemoveCustomAssetFromList(Tab, m_aDeleteAssetName);

	if(WasSelected)
	{
		if(Tab == ASSETS_TAB_ENTITIES)
			GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
		else if(Tab == ASSETS_TAB_GAME)
			GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
		else if(Tab == ASSETS_TAB_EMOTICONS)
			GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
		else if(Tab == ASSETS_TAB_PARTICLES)
			GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
		else if(Tab == ASSETS_TAB_HUD)
			GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
		else if(Tab == ASSETS_TAB_EXTRAS)
			GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
		else if(Tab == ASSETS_TAB_CURSOR)
			GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
		else if(Tab == ASSETS_TAB_ARROW)
			GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
		else if(Tab == ASSETS_TAB_AUDIO)
			GameClient()->m_Sounds.Clear();
	}

	gs_aInitCustomList[Tab] = true;
	m_aDeleteAssetName[0] = '\0';
	m_DeleteAssetTab = -1;
}

static void AssetsUnloadEntitiesPreview(CMenus::SCustomEntities &Entity, IGraphics *pGraphics)
{
	for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
	{
		IGraphics::CTextureHandle &Tex = Entity.m_aImages[i].m_Texture;
		if(!Tex.IsValid() || Tex.IsNullTexture())
			continue;

		const int TextureId = Tex.Id();
		for(int j = i + 1; j < MAP_IMAGE_MOD_TYPE_COUNT; ++j)
		{
			if(Entity.m_aImages[j].m_Texture.IsValid() && !Entity.m_aImages[j].m_Texture.IsNullTexture() && Entity.m_aImages[j].m_Texture.Id() == TextureId)
				Entity.m_aImages[j].m_Texture.Invalidate();
		}
		if(Entity.m_RenderTexture.IsValid() && !Entity.m_RenderTexture.IsNullTexture() && Entity.m_RenderTexture.Id() == TextureId)
			Entity.m_RenderTexture.Invalidate();

		pGraphics->UnloadTexture(&Tex);
	}
	Entity.m_RenderTexture.Invalidate();
}

template<typename TName>
static void AssetsEraseNamedPreview(std::vector<TName> &vList, IGraphics *pGraphics, const char *pName, bool UnloadTexture)
{
	for(auto It = vList.begin(); It != vList.end();)
	{
		if(str_comp(It->m_aName, pName) != 0)
		{
			++It;
			continue;
		}
		if(UnloadTexture)
			pGraphics->UnloadTexture(&It->m_RenderTexture);
		It = vList.erase(It);
	}
}

void CMenus::RemoveCustomAssetFromList(int Tab, const char *pName)
{
	if(Tab == ASSETS_TAB_ENTITIES)
	{
		for(auto It = m_vEntitiesList.begin(); It != m_vEntitiesList.end();)
		{
			if(str_comp(It->m_aName, pName) != 0)
			{
				++It;
				continue;
			}
			AssetsUnloadEntitiesPreview(*It, Graphics());
			It = m_vEntitiesList.erase(It);
		}
	}
	else if(Tab == ASSETS_TAB_GAME)
		AssetsEraseNamedPreview(m_vGameList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		AssetsEraseNamedPreview(m_vEmoticonList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_PARTICLES)
		AssetsEraseNamedPreview(m_vParticlesList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_HUD)
		AssetsEraseNamedPreview(m_vHudList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_EXTRAS)
		AssetsEraseNamedPreview(m_vExtrasList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_CURSOR)
		AssetsEraseNamedPreview(m_vCursorList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_ARROW)
		AssetsEraseNamedPreview(m_vArrowList, Graphics(), pName, true);
	else if(Tab == ASSETS_TAB_AUDIO)
		AssetsEraseNamedPreview(m_vAudioPackList, Graphics(), pName, false);
}

void CMenus::MarkCustomAssetsDeletable(int Tab)
{
	auto MarkList = [this, Tab](auto &vList) {
		for(auto &Asset : vList)
			Asset.m_Deletable = CanDeleteCustomAsset(Tab, Asset.m_aName);
	};

	if(Tab == ASSETS_TAB_ENTITIES)
		MarkList(m_vEntitiesList);
	else if(Tab == ASSETS_TAB_GAME)
		MarkList(m_vGameList);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		MarkList(m_vEmoticonList);
	else if(Tab == ASSETS_TAB_PARTICLES)
		MarkList(m_vParticlesList);
	else if(Tab == ASSETS_TAB_HUD)
		MarkList(m_vHudList);
	else if(Tab == ASSETS_TAB_EXTRAS)
		MarkList(m_vExtrasList);
	else if(Tab == ASSETS_TAB_CURSOR)
		MarkList(m_vCursorList);
	else if(Tab == ASSETS_TAB_ARROW)
		MarkList(m_vArrowList);
	else if(Tab == ASSETS_TAB_AUDIO)
		MarkList(m_vAudioPackList);
}

static std::vector<const CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<const CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<const CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<const CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<const CMenus::SCustomExtras *> gs_vpSearchExtrasList;
static std::vector<const CMenus::SCustomCursor *> gs_vpSearchCursorList;
static std::vector<const CMenus::SCustomArrow *> gs_vpSearchArrowList;
static std::vector<const CMenus::SCustomAudioPack *> gs_vpSearchAudioPackList;

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;
static void ResetAssetOrganizerInteraction(CUi *pUi = nullptr);

void CMenus::OpenAssetsSelector(EAssetsSelectorTab Tab)
{
	s_CurCustomTab = std::max((int)ASSETS_TAB_ENTITIES, std::min((int)Tab, (int)NUMBER_OF_ASSETS_TABS - 1));
	ResetAssetOrganizerInteraction(Ui());
	// Keep already scanned preview textures resident. Reopening a selector must
	// not unload/reload every card or reapply the active skin; the explicit
	// Refresh action remains the deliberate way to rescan the filesystem.
	g_Config.m_UiSettingsPage = SETTINGS_ASSETS;
	SetMenuPage(PAGE_SETTINGS);
}

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	else if(CurTab == ASSETS_TAB_CURSOR)
		return gs_vpSearchCursorList[Index];
	else if(CurTab == ASSETS_TAB_ARROW)
		return gs_vpSearchArrowList[Index];
	else if(CurTab == ASSETS_TAB_AUDIO)
		return gs_vpSearchAudioPackList[Index];

	return nullptr;
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == s_CurCustomTab)
		ResetAssetOrganizerInteraction(Ui());
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
			AssetsUnloadEntitiesPreview(Entity, Graphics());
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());
		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else if(CurTab == ASSETS_TAB_CURSOR)
	{
		ClearCursorAssetList(m_vCursorList, Graphics());
		GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
	}
	else if(CurTab == ASSETS_TAB_ARROW)
	{
		ClearArrowAssetList(m_vArrowList, Graphics());
		GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
	}
	else if(CurTab == ASSETS_TAB_AUDIO)
	{
		m_vAudioPackList.clear();
		GameClient()->m_Sounds.Clear();
	}
	gs_aInitCustomList[CurTab] = true;
}

void CMenus::SyncAssetOrganizer(int Tab)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
		return;

	std::vector<std::string> vAssetIds;
	auto Collect = [&vAssetIds](const auto &vAssets) {
		vAssetIds.reserve(vAssets.size());
		for(const auto &Asset : vAssets)
			vAssetIds.emplace_back(Asset.m_aName);
	};

	if(Tab == ASSETS_TAB_ENTITIES)
		Collect(m_vEntitiesList);
	else if(Tab == ASSETS_TAB_GAME)
		Collect(m_vGameList);
	else if(Tab == ASSETS_TAB_EMOTICONS)
		Collect(m_vEmoticonList);
	else if(Tab == ASSETS_TAB_PARTICLES)
		Collect(m_vParticlesList);
	else if(Tab == ASSETS_TAB_HUD)
		Collect(m_vHudList);
	else if(Tab == ASSETS_TAB_EXTRAS)
		Collect(m_vExtrasList);
	else if(Tab == ASSETS_TAB_CURSOR)
		Collect(m_vCursorList);
	else if(Tab == ASSETS_TAB_ARROW)
		Collect(m_vArrowList);
	else if(Tab == ASSETS_TAB_AUDIO)
		Collect(m_vAudioPackList);

	AssetOrganizer::EnsureItems(Tab, vAssetIds, m_aAssetFavorites[Tab]);
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, const char *pAssetName, FS_LISTDIR_CALLBACK pfnCallback, IGraphics *pGraphics, IStorage *pStorage, std::unordered_set<std::string> &KnownAssetIds, TCaller Caller)
{
	if(vAssetList.empty())
	{
		KnownAssetIds.clear();
		KnownAssetIds.emplace("default");
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		AssetItem.m_Deletable = false;
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<const TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	vpSearchList.reserve(vAssetList.size());
	const char *pFolderId = AssetOrganizer::CurrentFolder(s_CurCustomTab);
	const int Page = AssetOrganizer::CurrentPage(s_CurCustomTab);
	const bool HasFilter = !s_aFilterInputs[s_CurCustomTab].IsEmpty();
	const char *pFilter = HasFilter ? s_aFilterInputs[s_CurCustomTab].GetString() : nullptr;
	for(const TName &Asset : vAssetList)
	{
		const TName *pAsset = &Asset;

		if(!AssetOrganizer::ItemIsVisible(s_CurCustomTab, pAsset->m_aName, pFolderId, Page))
			continue;
		if(HasFilter && !str_utf8_find_nocase(pAsset->m_aName, pFilter))
			continue;

		vpSearchList.push_back(pAsset);
	}
	return (int)vpSearchList.size();
}

static const char *AssetOrganizerFolderLabel(int Tab, const char *pFolderId)
{
	if(AssetOrganizer::IsFavoritesFolder(pFolderId))
		return TCLocalize("Favorites", "AMF Client");
	if(str_comp(pFolderId, AssetOrganizer::FOLDER_UNSORTED) == 0)
		return TCLocalize("Unsorted", "AMF Client");
	return AssetOrganizer::FolderName(Tab, pFolderId);
}

struct SAssetOrganizerPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	AssetOrganizer::SItemContextPopupState m_Item;
	char m_aTargetFolderId[64] = {};
	int m_TargetPage = 0;
	CButtonContainer m_FavoriteButton;
	CButtonContainer m_TargetFolderPrevButton;
	CButtonContainer m_TargetFolderNextButton;
	CButtonContainer m_TargetPagePrevButton;
	CButtonContainer m_TargetPageNextButton;
	CButtonContainer m_MoveButton;

	void ClearTarget()
	{
		m_pMenus = nullptr;
		m_Item.Clear();
		m_aTargetFolderId[0] = '\0';
		m_TargetPage = 0;
	}

	void SyncWithUi(bool PopupIsOpen)
	{
		m_Item.SyncVisibility(PopupIsOpen);
		if(!PopupIsOpen)
		{
			m_pMenus = nullptr;
			m_aTargetFolderId[0] = '\0';
			m_TargetPage = 0;
		}
	}

	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active)
	{
		auto *pPopup = static_cast<SAssetOrganizerPopupContext *>(pContext);
		if(!pPopup->m_pMenus || !pPopup->m_pMenus->IsAssetOrganizerPageActive() ||
			pPopup->m_Item.m_Tab < 0 || pPopup->m_Item.m_Tab != s_CurCustomTab || pPopup->m_Item.m_AssetId.empty())
			return CUi::POPUP_CLOSE_CURRENT;

		CUi *pUi = pPopup->m_pMenus->AssetOrganizerUi();
		constexpr float Margin = 5.0f;
		constexpr float Spacing = 2.0f;
		constexpr float ButtonHeight = 17.5f;
		View.Margin(Margin, &View);
		// Popup rendering is a static callback, so read the same config source that
		// CMenus::UpdateColors() converts into ms_GuiColor for the main page.
		const ColorRGBA PopupInterfaceColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));
		const ColorHSLA PopupInterfaceHsla = color_cast<ColorHSLA>(PopupInterfaceColor);
		const float PopupLightness = std::clamp(PopupInterfaceHsla.l, 0.28f, 0.58f);
		const auto PopupAccent = [&](float Lightness, float Alpha) {
			return color_cast<ColorRGBA>(ColorHSLA(PopupInterfaceHsla.h, PopupInterfaceHsla.s, std::clamp(Lightness, 0.0f, 1.0f), Alpha));
		};
		const ColorRGBA PopupAccentPressed = PopupAccent(PopupLightness - 0.08f, 0.96f);
		const ColorRGBA PopupAccentSoft = PopupAccent(PopupLightness, 0.62f);
		const ColorRGBA PopupNeutral = ColorRGBA(0.055f, 0.075f, 0.10f, 0.92f);
		const ColorRGBA PopupNeutralHover = ColorRGBA(0.085f, 0.11f, 0.14f, 0.95f);
		const ColorRGBA PopupNeutralDisabled = ColorRGBA(0.035f, 0.045f, 0.06f, 0.52f);
		const ColorRGBA PopupBorder = ColorRGBA(0.10f, 0.13f, 0.17f, 0.78f);
		auto DoOrganizerPopupButton = [pUi, &PopupAccentPressed, &PopupAccentSoft, &PopupNeutral, &PopupNeutralHover, &PopupNeutralDisabled, &PopupBorder](CButtonContainer *pButton, const char *pText, const CUIRect *pRect, float FontSize, bool Enabled = true) {
			const bool Hovered = Enabled && pUi->MouseHovered(pRect);
			const bool Active = Enabled && pUi->CheckActiveItem(pButton);
			const ColorRGBA Fill = !Enabled ? PopupNeutralDisabled : (Active ? PopupAccentPressed : (Hovered ? PopupNeutralHover : PopupNeutral));
			const ColorRGBA Border = !Enabled ? PopupBorder : (Active ? PopupAccentPressed : (Hovered ? PopupAccentSoft : PopupBorder));
			pRect->Draw(Border, IGraphics::CORNER_ALL, 3.0f);
			CUIRect Inner = *pRect;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(Fill, IGraphics::CORNER_ALL, 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, pRect->w - 6.0f);
			pUi->DoLabel(pRect, pText, FontSize, TEXTALIGN_MC, Props);
			return Enabled ? pUi->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT) : 0;
		};
		auto DoOrganizerPopupIconButton = [&](CButtonContainer *pButton, const char *pIcon, const CUIRect *pRect, bool Enabled = true) {
			pUi->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			pUi->TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			const int Result = DoOrganizerPopupButton(pButton, pIcon, pRect, 10.5f, Enabled);
			pUi->TextRender()->SetRenderFlags(0);
			pUi->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			return Result;
		};

		CUIRect Row;
		View.HSplitTop(13.0f, &Row, &View);
		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = Row.w;
		TitleProps.m_EllipsisAtEnd = true;
		pUi->DoLabel(&Row, pPopup->m_Item.m_AssetId.c_str(), 10.0f, TEXTALIGN_MC, TitleProps);

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		const bool Favorite = AssetOrganizer::IsFavorite(pPopup->m_Item.m_Tab, pPopup->m_Item.m_AssetId.c_str());
		if(DoOrganizerPopupButton(&pPopup->m_FavoriteButton, TCLocalize(Favorite ? "Remove from favorites" : "Add to favorites", "AMF Client"), &Row, 9.0f))
		{
			pPopup->m_pMenus->SetAssetOrganizerFavorite(pPopup->m_Item.m_Tab, pPopup->m_Item.m_AssetId.c_str(), !Favorite);
			return CUi::POPUP_KEEP_OPEN;
		}

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		CUIRect Left, Label, Right;
		Row.VSplitLeft(ButtonHeight, &Left, &Row);
		Row.VSplitRight(ButtonHeight, &Row, &Right);
		Label = Row;
		const auto &vFolders = AssetOrganizer::Folders(pPopup->m_Item.m_Tab);
		const int FolderCount = (int)vFolders.size();
		int FolderIndex = 0;
		for(int Index = 0; Index < FolderCount; ++Index)
		{
			if(vFolders[Index].m_Id == pPopup->m_aTargetFolderId)
			{
				FolderIndex = Index;
				break;
			}
		}
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetFolderPrevButton, FontIcon::CHEVRON_LEFT, &Left, FolderCount > 1))
		{
			FolderIndex = (FolderIndex + FolderCount - 1) % FolderCount;
			str_copy(pPopup->m_aTargetFolderId, vFolders[FolderIndex].m_Id.c_str(), sizeof(pPopup->m_aTargetFolderId));
			pPopup->m_TargetPage = 0;
		}
		pUi->DoLabel(&Label, AssetOrganizerFolderLabel(pPopup->m_Item.m_Tab, pPopup->m_aTargetFolderId), 9.0f, TEXTALIGN_MC);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetFolderNextButton, FontIcon::CHEVRON_RIGHT, &Right, FolderCount > 1))
		{
			FolderIndex = (FolderIndex + 1) % FolderCount;
			str_copy(pPopup->m_aTargetFolderId, vFolders[FolderIndex].m_Id.c_str(), sizeof(pPopup->m_aTargetFolderId));
			pPopup->m_TargetPage = 0;
		}

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		Row.VSplitLeft(ButtonHeight, &Left, &Row);
		Row.VSplitRight(ButtonHeight, &Row, &Right);
		Label = Row;
		const int Pages = AssetOrganizer::PageCount(pPopup->m_Item.m_Tab, pPopup->m_aTargetFolderId);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetPagePrevButton, FontIcon::CHEVRON_LEFT, &Left, pPopup->m_TargetPage > 0))
			--pPopup->m_TargetPage;
		char aPageLabel[64];
		str_format(aPageLabel, sizeof(aPageLabel), "%s %d / %d", TCLocalize("Page", "AMF Client"), pPopup->m_TargetPage + 1, Pages);
		pUi->DoLabel(&Label, aPageLabel, 9.0f, TEXTALIGN_MC);
		if(DoOrganizerPopupIconButton(&pPopup->m_TargetPageNextButton, FontIcon::CHEVRON_RIGHT, &Right, pPopup->m_TargetPage + 1 < Pages))
			++pPopup->m_TargetPage;

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(ButtonHeight, &Row, &View);
		if(DoOrganizerPopupButton(&pPopup->m_MoveButton, TCLocalize("Move here", "AMF Client"), &Row, 9.0f))
		{
			// Exact position is a direct manipulation feature now. The menu keeps
			// only a convenient folder/page destination and appends there.
			AssetOrganizer::MoveItem(pPopup->m_Item.m_Tab, pPopup->m_Item.m_AssetId.c_str(), pPopup->m_aTargetFolderId, pPopup->m_TargetPage, AssetOrganizer::ItemCount(pPopup->m_Item.m_Tab, pPopup->m_aTargetFolderId, pPopup->m_TargetPage));
			gs_aInitCustomList[pPopup->m_Item.m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}

		return CUi::POPUP_KEEP_OPEN;
	}
};

static SAssetOrganizerPopupContext gs_AssetOrganizerPopupContext;

bool CMenus::IsAssetOrganizerPageActive() const
{
	if(g_Config.m_UiSettingsPage != SETTINGS_ASSETS)
		return false;
	if(Client()->State() == IClient::STATE_OFFLINE)
		return !m_ShowStart && m_MenuPage == PAGE_SETTINGS;
	return Client()->State() == IClient::STATE_ONLINE && m_MenuActive && m_GamePage == PAGE_SETTINGS;
}

struct SAssetFolderPopupContext : public SPopupMenuId
{
	CMenus *m_pMenus = nullptr;
	int m_Tab = -1;
	char m_aFolderId[64] = {};
	CLineInputBuffered<64> m_NameInput;
	CButtonContainer m_RenameButton;
	CButtonContainer m_DeleteButton;

	static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active)
	{
		auto *pPopup = static_cast<SAssetFolderPopupContext *>(pContext);
		if(!pPopup->m_pMenus || pPopup->m_Tab < 0 || pPopup->m_aFolderId[0] == '\0' || AssetOrganizer::IsSystemFolder(pPopup->m_Tab, pPopup->m_aFolderId))
			return CUi::POPUP_CLOSE_CURRENT;
		CUi *pUi = pPopup->m_pMenus->AssetOrganizerUi();
		const ColorRGBA RenameColor = ColorRGBA(0.22f, 0.64f, 0.38f, 0.99f);
		View.Margin(6.0f, &View);
		CUIRect Input, Rename, Delete;
		View.HSplitTop(21.0f, &Input, &View);
		View.HSplitTop(5.0f, nullptr, &View);
		View.HSplitTop(20.0f, &Rename, &View);
		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(20.0f, &Delete, &View);
		pUi->DoEditBox(&pPopup->m_NameInput, &Input, 10.0f);

		auto DoPopupButton = [pUi](CButtonContainer *pButton, const char *pText, const CUIRect *pRect, const ColorRGBA &Base) {
			const bool Hovered = pUi->MouseHovered(pRect);
			const bool Pressed = pUi->CheckActiveItem(pButton);
			const ColorRGBA Fill = Pressed ? ColorRGBA(Base.r * 1.12f, Base.g * 1.12f, Base.b * 1.12f, Base.a) : Hovered ? ColorRGBA(Base.r * 1.22f, Base.g * 1.22f, Base.b * 1.22f, Base.a) : Base;
			pRect->Draw(Base.WithAlpha(std::min(1.0f, Base.a + 0.10f)), IGraphics::CORNER_ALL, 3.0f);
			CUIRect Inner = *pRect;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(Fill, IGraphics::CORNER_ALL, 2.0f);
			SLabelProperties Props;
			Props.m_MaxWidth = std::max(0.0f, pRect->w - 8.0f);
			Props.m_EllipsisAtEnd = true;
			pUi->DoLabel(pRect, pText, 9.5f, TEXTALIGN_MC, Props);
			return pUi->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT);
		};
		if(DoPopupButton(&pPopup->m_RenameButton, TCLocalize("Rename", "AMF Client"), &Rename, RenameColor))
		{
			AssetOrganizer::RenameFolder(pPopup->m_Tab, pPopup->m_aFolderId, pPopup->m_NameInput.GetString());
			gs_aInitCustomList[pPopup->m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		if(DoPopupButton(&pPopup->m_DeleteButton, TCLocalize("Delete", "AMF Client"), &Delete, ColorRGBA(0.52f, 0.13f, 0.18f, 0.99f)))
		{
			AssetOrganizer::DeleteFolder(pPopup->m_Tab, pPopup->m_aFolderId);
			AssetOrganizer::SetCurrentFolder(pPopup->m_Tab, AssetOrganizer::FOLDER_UNSORTED);
			gs_aInitCustomList[pPopup->m_Tab] = true;
			return CUi::POPUP_CLOSE_CURRENT;
		}
		return CUi::POPUP_KEEP_OPEN;
	}
};

static SAssetFolderPopupContext gs_AssetFolderPopupContext;

// Kept independently from the asset vectors so a drag does not depend on a
// card's grid position. A press becomes a drag only after a visible movement
// threshold; otherwise it remains an ordinary pack-selection click.
struct SAssetOrganizerDragState
{
	const CMenus::SCustomItem *m_pSourceItem = nullptr;
	int m_Tab = -1;
	vec2 m_PressPos = vec2(0.0f, 0.0f);
	vec2 m_DragOffset = vec2(0.0f, 0.0f);
	vec2 m_GhostPos = vec2(0.0f, 0.0f);
	CUIRect m_SourceRect = {};
	CUIRect m_TargetRect = {};
	char m_aTargetFolderId[64] = {};
	char m_aTargetAssetId[128] = {};
	bool m_Dragging = false;
	bool m_TargetRectValid = false;
	bool m_CreateFolderDropTarget = false;
	bool m_CurrentPageDropTarget = false;

	void Reset()
	{
		m_pSourceItem = nullptr;
		m_Tab = -1;
		m_aTargetFolderId[0] = '\0';
		m_aTargetAssetId[0] = '\0';
		m_Dragging = false;
		m_TargetRectValid = false;
		m_CreateFolderDropTarget = false;
		m_CurrentPageDropTarget = false;
	}
};

static SAssetOrganizerDragState gs_AssetOrganizerDrag;

struct SAssetOrganizerSettleState
{
	bool m_Active = false;
	int m_Tab = -1;
	std::chrono::nanoseconds m_Start{};
	CUIRect m_From = {};
	CUIRect m_To = {};
	IGraphics::CTextureHandle m_Texture;
	float m_PreviewAspect = 1.0f;
	char m_aName[CMenus::SCustomItem::MAX_NAME_LENGTH] = {};
};

static SAssetOrganizerSettleState gs_AssetOrganizerSettle;

struct SAssetFolderDragState
{
	int m_Tab = -1;
	int m_SourceIndex = -1;
	int m_TargetIndex = -1;
	vec2 m_PressPos = vec2(0.0f, 0.0f);
	char m_aSourceId[64] = {};
	char m_aSourceName[64] = {};
	bool m_Dragging = false;

	void Reset()
	{
		m_Tab = -1;
		m_SourceIndex = -1;
		m_TargetIndex = -1;
		m_aSourceId[0] = '\0';
		m_aSourceName[0] = '\0';
		m_Dragging = false;
	}
};

static SAssetFolderDragState gs_AssetFolderDrag;

static void ResetAssetOrganizerInteraction(CUi *pUi)
{
	gs_AssetOrganizerDrag.Reset();
	gs_AssetFolderDrag.Reset();
	gs_AssetOrganizerSettle.m_Active = false;
	// A card ID points into one of the backing asset vectors. Refreshing that
	// vector, changing selector tab, or reopening the selector must not leave
	// it as the UI active item: otherwise a later DoButtonLogic call can meet a
	// stale active ID with no recorded activating mouse button.
	if(pUi)
		pUi->SetActiveItem(nullptr);
}

void CMenus::RenderSettingsAssets(CUIRect MainView)
{
	// Popup context stores stable asset IDs (never pointers into the refreshed
	// asset vectors). Clear the last target as soon as the popup has closed.
	gs_AssetOrganizerPopupContext.SyncWithUi(Ui()->IsPopupOpen(&gs_AssetOrganizerPopupContext));

	CUIRect TabBar, OrganizerToolbar, OrganizerBody, FoldersPanel, AssetsPanel, AssetHeader, OrganizerPagePrevious, OrganizerPageNav, OrganizerPageNext, OrganizerPageAdd, OrganizerPageDelete, CustomList, QuickSearch, InputHints, DirectoryButton, ReloadButton;
	static bool s_EntityGamePreview = true;
	// Keep the organizer's accent on the exact same source as the regular DDNet
	// menu: UpdateColors() derives ms_GuiColor from g_Config.m_UiColor every
	// frame. All variants below only adjust brightness/alpha for state contrast;
	// neutral surfaces remain independent of the user's interface color.
	const ColorRGBA InterfaceColor = ms_GuiColor.WithAlpha(1.0f);
	const ColorHSLA InterfaceHsla = color_cast<ColorHSLA>(InterfaceColor);
	const float AccentLightness = std::clamp(InterfaceHsla.l, 0.28f, 0.58f);
	const auto AccentColor = [&](float Lightness, float Alpha) {
		return color_cast<ColorRGBA>(ColorHSLA(InterfaceHsla.h, InterfaceHsla.s, std::clamp(Lightness, 0.0f, 1.0f), Alpha));
	};
	const ColorRGBA AccentSelected = AccentColor(AccentLightness, 0.94f);
	const ColorRGBA AccentHover = AccentColor(AccentLightness + 0.08f, 0.92f);
	const ColorRGBA AccentPressed = AccentColor(AccentLightness - 0.08f, 0.96f);
	const ColorRGBA AccentSoft = AccentColor(AccentLightness, 0.62f);
	const ColorRGBA AccentOutline = AccentColor(AccentLightness, 0.34f);
	const ColorRGBA AccentHeader = AccentColor(AccentLightness, 0.48f);
	const ColorRGBA NeutralBorder = ColorRGBA(0.12f, 0.15f, 0.19f, 0.90f);
	const ColorRGBA NeutralBorderSoft = ColorRGBA(0.10f, 0.13f, 0.17f, 0.72f);
	const ColorRGBA NeutralButton = ColorRGBA(0.055f, 0.075f, 0.10f, 0.92f);
	const ColorRGBA NeutralButtonHover = ColorRGBA(0.085f, 0.11f, 0.14f, 0.95f);
	const ColorRGBA NeutralButtonDisabled = ColorRGBA(0.035f, 0.045f, 0.06f, 0.52f);
	// The Audio organizer note intentionally keeps its pre-redesign fixed blue.
	// This is also the color used by the music player's no-media glyph.
	const ColorRGBA AudioNoteColor = ColorRGBA(0.54f, 0.76f, 0.92f, 0.90f);
	auto SortSearchList = [](auto &vpSearchList) {
		std::sort(vpSearchList.begin(), vpSearchList.end(), [](const auto *pLeft, const auto *pRight) {
			const int LeftSlot = AssetOrganizer::ItemSlot(s_CurCustomTab, pLeft->m_aName);
			const int RightSlot = AssetOrganizer::ItemSlot(s_CurCustomTab, pRight->m_aName);
			if(LeftSlot != RightSlot)
				return LeftSlot < RightSlot;
			return str_comp(pLeft->m_aName, pRight->m_aName) < 0;
		});
	};

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	const CUIRect TabBarOuter = TabBar;
	TabBarOuter.Draw(NeutralBorder, IGraphics::CORNER_ALL, 5.0f);
	CUIRect TabBarSurface = TabBarOuter;
	TabBarSurface.Margin(1.0f, &TabBarSurface);
	TabBarSurface.Draw(ColorRGBA(0.028f, 0.042f, 0.060f, 0.96f), IGraphics::CORNER_ALL, 4.0f);
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	// One palette and the standard UI button lifecycle for every organizer
	// control. In particular, nothing here assigns ActiveItem directly.
	auto DoOrganizerButton = [&](const void *pButton, const char *pText, const CUIRect *pRect, bool Enabled = true, bool Selected = false, int Corners = IGraphics::CORNER_ALL, float FontSize = 10.5f, vec2 TextOffset = vec2(0.0f, 0.0f), const CUIRect *pVisualRect = nullptr, bool DrawBorder = true) {
		const bool Hovered = Enabled && Ui()->MouseHovered(pRect);
		const bool Active = Enabled && Ui()->CheckActiveItem(pButton);
		const CUIRect DrawRect = pVisualRect ? *pVisualRect : *pRect;
		const ColorRGBA Color = !Enabled ? NeutralButtonDisabled :
			(Selected ? AccentColor(AccentLightness, 0.76f) :
				(Active ? AccentPressed :
					(Hovered ? NeutralButtonHover : NeutralButton)));
		const ColorRGBA Border = !Enabled ? NeutralBorderSoft :
			(Selected ? AccentSelected :
				(Hovered || Active ? AccentSoft : NeutralBorder));
		CUIRect Inner = DrawRect;
		if(DrawBorder)
		{
			DrawRect.Draw(Border, Corners, 4.0f);
			const float Inset = std::min(1.0f, std::max(0.0f, std::min(DrawRect.w, DrawRect.h) * 0.08f));
			Inner.Margin(Inset, &Inner);
		}
		Inner.Draw(Color, Corners, 3.0f);
		CUIRect LabelRect = DrawRect;
		LabelRect.x += TextOffset.x;
		LabelRect.y += TextOffset.y;
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, DrawRect.w - 8.0f);
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&LabelRect, pText, FontSize, TEXTALIGN_MC, Props);
		return Enabled ? Ui()->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT) : 0;
	};
	auto DoOrganizerIconButton = [&](const void *pButton, const char *pIcon, const CUIRect *pRect, bool Enabled = true, bool Selected = false, float FontSize = 12.0f, vec2 OpticalOffset = vec2(0.0f, 0.0f), const CUIRect *pVisualRect = nullptr) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		// Match DDNet's standard font-icon button metrics. NO_PIXEL_ALIGNMENT and
		// NO_OVERSIZE made some glyphs (notably STAR/EYE) collapse into a tiny dot
		// at this compact size.
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		const int Result = DoOrganizerButton(pButton, pIcon, pRect, Enabled, Selected, IGraphics::CORNER_ALL, FontSize, OpticalOffset, pVisualRect);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		return Result;
	};
	// Tabs share one frame. Their interaction rectangles stay unchanged, while
	// the visual tabs are inset into that frame so adjacent tabs never create a
	// pair of touching button borders.
	auto DoOrganizerTab = [&](const void *pButton, const char *pText, const CUIRect *pRect, bool Selected, int Corners) {
		const bool Hovered = Ui()->MouseHovered(pRect);
		const bool Active = Ui()->CheckActiveItem(pButton);
		CUIRect Visual = *pRect;
		Visual.Margin(1.0f, &Visual);
		const ColorRGBA Fill = Selected ? AccentSelected.WithAlpha(0.88f) :
			(Hovered || Active ? NeutralButtonHover : ColorRGBA(0.045f, 0.060f, 0.080f, 0.74f));
		Visual.Draw(Fill, Corners, 3.0f);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, Visual.w - 6.0f);
		Props.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Visual, pText, 9.0f, TEXTALIGN_MC, Props);
		return Ui()->DoButtonLogic(pButton, 0, pRect, BUTTONFLAG_LEFT);
	};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Cursor"),
		Localize("Arrow"),
		Localize("Audio")};

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		// Nine localized tabs share one row. Keep the label compact enough for
		// RU/UA on narrow settings windows instead of allowing a two-line wrap.
		if(DoOrganizerTab(&s_aPageTabs[Tab], apTabNames[Tab], &Button, s_CurCustomTab == Tab, Corners))
		{
			if(s_CurCustomTab != Tab)
			{
				while(Ui()->IsPopupOpen(&gs_AssetOrganizerPopupContext))
					Ui()->ClosePopupMenu(&gs_AssetOrganizerPopupContext);
				gs_AssetOrganizerPopupContext.ClearTarget();
				// A card/folder pointer belongs to the old list. Drop transient
				// interaction state before changing the backing vector.
				ResetAssetOrganizerInteraction(Ui());
				s_CurCustomTab = Tab;
			}
		}
		if(Tab + 1 < NUMBER_OF_ASSETS_TABS)
		{
			// The child tab surfaces are inset, leaving a single clean separator
			// gap. Draw it once, never as two neighboring tab borders.
			CUIRect Separator = {Button.x + Button.w - 0.5f, TabBarOuter.y + 4.0f, 1.0f, std::max(0.0f, TabBarOuter.h - 8.0f)};
			Separator.Draw(AccentOutline, IGraphics::CORNER_NONE, 0.0f);
		}
	}

	const auto LoadStartTime = time_get_nanoseconds();
	auto LastLoadingRenderTime = LoadStartTime;
	std::unordered_set<std::string> KnownAssetIds;
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_pKnownAssetIds = &KnownAssetIds;
	User.m_LoadedFunc = [&]() {
		const auto Now = time_get_nanoseconds();
		if(Now - LoadStartTime > 500ms && Now - LastLoadingRenderTime >= 100ms)
		{
			RenderLoading(Localize("Loading assets"), "", 0);
			LastLoadingRenderTime = Now;
		}
	};
	bool AssetListScanned = false;
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			EntitiesItem.m_Deletable = false;
			LoadEntities(&EntitiesItem, &User);
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_ENTITIES);
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		if(m_vGameList.empty())
		{
			AssetListScanned = true;
			SCustomGame DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			DefaultItem.m_Deletable = false;
			LoadAsset(&DefaultItem, "game", Graphics());
			m_vGameList.push_back(DefaultItem);

			std::unordered_set<std::string> KnownGamePackIds = {"default"};
			SGameAssetScanUser GameScanUser;
			GameScanUser.m_pMenus = this;
			GameScanUser.m_LoadedFunc = User.m_LoadedFunc;
			GameScanUser.m_pKnownPackIds = &KnownGamePackIds;
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/game", GameScan, &GameScanUser);
			std::sort(m_vGameList.begin(), m_vGameList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_GAME);
			// The initial scan fills the backing list, so rebuild the organizer's
			// prepared visible list in this same entry pass. Previously only a
			// folder interaction set this flag, leaving Game empty on first open.
			gs_aInitCustomList[ASSETS_TAB_GAME] = true;
		}
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		const bool WasEmpty = m_vEmoticonList.empty();
		InitAssetList(m_vEmoticonList, "assets/emoticons", "emoticons", EmoticonsScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EMOTICONS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		const bool WasEmpty = m_vParticlesList.empty();
		InitAssetList(m_vParticlesList, "assets/particles", "particles", ParticlesScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_PARTICLES);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		const bool WasEmpty = m_vHudList.empty();
		InitAssetList(m_vHudList, "assets/hud", "hud", HudScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_HUD);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		const bool WasEmpty = m_vExtrasList.empty();
		InitAssetList(m_vExtrasList, "assets/extras", "extras", ExtrasScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_EXTRAS);
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		if(m_vCursorList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, "default");
			CursorItem.m_Deletable = false;
			LoadCursorPreview(&CursorItem, Graphics());
			m_vCursorList.push_back(CursorItem);

			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/cursor", CursorScan, &User);
			std::sort(m_vCursorList.begin(), m_vCursorList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_CURSOR);
		}
		if(m_vCursorList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		const bool WasEmpty = m_vArrowList.empty();
		InitAssetList(m_vArrowList, "assets/arrow", "arrow", ArrowScan, Graphics(), Storage(), KnownAssetIds, &User);
		AssetListScanned |= WasEmpty;
		if(WasEmpty)
			MarkCustomAssetsDeletable(ASSETS_TAB_ARROW);
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		if(m_vAudioPackList.empty())
		{
			AssetListScanned = true;
			KnownAssetIds.emplace("default");
			SCustomAudioPack DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			DefaultItem.m_Deletable = false;
			DefaultItem.m_RenderTexture = IGraphics::CTextureHandle();
			m_vAudioPackList.push_back(DefaultItem);

			// TYPE_ALL includes the packaged data directory, so a transferred
			// Release build exposes its bundled packs (including JuKKi) directly.
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/audio", AudioPackScan, &User);
			Storage()->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackScan, &User);
			std::sort(m_vAudioPackList.begin(), m_vAudioPackList.end());
			MarkCustomAssetsDeletable(ASSETS_TAB_AUDIO);
		}
		if(m_vAudioPackList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	if(AssetListScanned)
		SyncAssetOrganizer(s_CurCustomTab);

	// The organizer is deliberately only a presentation layer. It stores a
	// library layout, never moves resource files or changes existing loaders.
	MainView.HSplitTop(6.0f, nullptr, &MainView);
	MainView.HSplitTop(31.0f, &OrganizerToolbar, &MainView);
	// Keep the toolbar and the two organizer columns in one coordinate frame.
	// The column rects below are inset for content, but their outer rects remain
	// the visual parents for the header and folder-creation controls.
	const CUIRect FolderToolbarOuter = OrganizerToolbar;
	MainView.HSplitBottom(57.0f, &OrganizerBody, &QuickSearch);
	QuickSearch.HSplitBottom(18.0f, &QuickSearch, &InputHints);
	constexpr float ORGANIZER_PANEL_PADDING = 7.0f;
	constexpr float ORGANIZER_CONTROL_HEIGHT = 21.0f;
	OrganizerBody.VSplitLeft(std::min(260.0f, std::max(185.0f, OrganizerBody.w * 0.24f)), &FoldersPanel, &AssetsPanel);
	const CUIRect FolderPanelOuter = FoldersPanel;
	const CUIRect AssetsPanelOuter = AssetsPanel;
	CUIRect FolderPanelFrame = FolderPanelOuter;
	FolderPanelFrame.Margin(2.0f, &FolderPanelFrame);
	CUIRect AssetsPanelFrame = AssetsPanelOuter;
	AssetsPanelFrame.Margin(2.0f, &AssetsPanelFrame);
	FoldersPanel.Margin(4.0f, &FoldersPanel);
	AssetsPanel.Margin(4.0f, &AssetsPanel);
	// Draw the frame at the actual column bounds, then inset the content below.
	// Previously the already-inset FoldersPanel was drawn as the parent, leaving
	// the toolbar title four pixels outside its apparent frame on narrow layouts.
	FolderPanelFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	CUIRect FolderPanelSurface = FolderPanelFrame;
	FolderPanelSurface.Margin(1.0f, &FolderPanelSurface);
	FolderPanelSurface.Draw(ColorRGBA(0.035f, 0.050f, 0.070f, 0.96f), IGraphics::CORNER_ALL, 5.0f);
	AssetsPanelFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	CUIRect AssetsPanelSurface = AssetsPanelFrame;
	AssetsPanelSurface.Margin(1.0f, &AssetsPanelSurface);
	AssetsPanelSurface.Draw(ColorRGBA(0.028f, 0.042f, 0.060f, 0.96f), IGraphics::CORNER_ALL, 5.0f);
	// Both dark organizer panels keep their content inside the same breathing
	// room. The frame rect remains unchanged; only the content rect is inset.
	AssetsPanel.Margin(ORGANIZER_PANEL_PADDING, &AssetsPanel);

	static CButtonContainer s_FolderCreateButton;
	static CButtonContainer s_PagePreviousButton;
	static CButtonContainer s_PageNextButton;
	static CButtonContainer s_PageAddButton;
	static CButtonContainer s_PageDeleteButton;
	static CLineInputBuffered<64> s_FolderNameInput;
	s_FolderNameInput.SetEmptyText(TCLocalize("Folder name", "AMF Client"));

	CUIRect FolderTitle, ToolbarActions;
	OrganizerToolbar.VSplitLeft(FoldersPanel.w, &FolderTitle, &OrganizerToolbar);
	OrganizerToolbar.VSplitLeft(8.0f, nullptr, &OrganizerToolbar);
	ToolbarActions = OrganizerToolbar;
	// The whole creation row is one clipped parent. This keeps localized labels,
	// the edit box and the action button inside their toolbar even when the
	// settings viewport is narrow or UI-scaled.
	CUIRect FolderToolbarFrame = FolderToolbarOuter;
	// Keep a deliberate breathing line between the creation toolbar and the
	// organizer columns below. The controls still use FolderToolbarOuter for
	// their exact interaction/clip rects; only the decorative frame is inset.
	FolderToolbarFrame.Margin(1.0f, &FolderToolbarFrame);
	FolderToolbarFrame.Draw(NeutralBorder, IGraphics::CORNER_ALL, 6.0f);
	FolderToolbarFrame.Margin(1.0f, &FolderToolbarFrame);
	FolderToolbarFrame.Draw(ColorRGBA(0.030f, 0.045f, 0.065f, 0.94f), IGraphics::CORNER_ALL, 5.0f);
	CUIRect FolderToolbarAccent = FolderToolbarOuter;
	FolderToolbarAccent.Margin(7.0f, &FolderToolbarAccent);
	FolderToolbarAccent.HSplitTop(2.0f, &FolderToolbarAccent, nullptr);
	FolderToolbarAccent.w = std::min(52.0f, std::max(0.0f, FolderToolbarAccent.w));
	FolderToolbarAccent.Draw(AccentHeader, IGraphics::CORNER_ALL, 1.0f);
	Ui()->ClipEnable(&FolderToolbarOuter);
	CUIRect FolderTitleVisual = FolderTitle;
	FolderTitleVisual.x = FolderPanelFrame.x;
	FolderTitleVisual.w = std::min(FolderTitleVisual.w, FolderPanelFrame.w);
	// Match the 31-unit creation controls while preserving a small horizontal
	// inset from the panel frame. The title, edit box and action button therefore
	// share the same outer height and vertical axis. The title is a label only;
	// the parent row owns the frame so it does not become a nested button border.
	constexpr float FOLDER_TOOLBAR_VERTICAL_PADDING = 2.0f;
	FolderTitleVisual.Margin(vec2(2.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderTitleVisual);
	SLabelProperties FolderTitleProps;
	FolderTitleProps.m_MaxWidth = std::max(0.0f, FolderTitleVisual.w - 8.0f);
	FolderTitleProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&FolderTitleVisual, TCLocalize("Folders", "AMF Client"), 12.0f, TEXTALIGN_MC, FolderTitleProps);

	CUIRect FolderInput, FolderCreate;
	ToolbarActions.VSplitLeft(std::min(190.0f, ToolbarActions.w * 0.45f), &FolderInput, &ToolbarActions);
	ToolbarActions.VSplitLeft(9.0f, nullptr, &ToolbarActions);
	ToolbarActions.VSplitLeft(std::min(120.0f, ToolbarActions.w), &FolderCreate, &ToolbarActions);
	FolderInput.Margin(vec2(0.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderInput);
	FolderCreate.Margin(vec2(0.0f, FOLDER_TOOLBAR_VERTICAL_PADDING), &FolderCreate);
	Ui()->DoEditBox(&s_FolderNameInput, &FolderInput, 10.5f);
	// The row already owns the enclosing frame. Keep the input's native
	// background/caret/selection, but do not add a second visual outline around
	// it. The create action uses the same compact button treatment without a
	// nested border, while retaining its original interaction rectangle.
	CUIRect FolderCreateVisual = FolderCreate;
	FolderCreateVisual.Margin(vec2(2.0f, 1.0f), &FolderCreateVisual);
	if(DoOrganizerButton(&s_FolderCreateButton, TCLocalize("Create folder", "AMF Client"), &FolderCreate,
		true, false, IGraphics::CORNER_ALL, 10.5f, vec2(0.0f, 0.0f), &FolderCreateVisual, false))
	{
		std::string CreatedId;
		if(AssetOrganizer::CreateFolder(s_CurCustomTab, s_FolderNameInput.GetString(), &CreatedId))
		{
			AssetOrganizer::SetCurrentFolder(s_CurCustomTab, CreatedId.c_str());
			s_FolderNameInput.Set("");
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
	}
	Ui()->ClipDisable();

	// The folder list is an actual navigation pane, rather than a collection of
	// small controls mixed into the asset grid. It only reads the organizer
	// model, which is refreshed when a selector is opened or explicitly reloaded.
	CUIRect FolderCaption, FolderList, FolderCreateDropZone;
	FoldersPanel.Margin(ORGANIZER_PANEL_PADDING, &FoldersPanel);
	FoldersPanel.HSplitTop(22.0f, &FolderCaption, &FolderList);
	CUIRect FolderCaptionLine = FolderCaption;
	FolderCaptionLine.HSplitBottom(1.0f, nullptr, &FolderCaptionLine);
	FolderCaptionLine.Draw(NeutralBorderSoft, IGraphics::CORNER_NONE, 0.0f);
	CUIRect FolderCaptionAccent = FolderCaptionLine;
	FolderCaptionAccent.w = std::min(30.0f, std::max(0.0f, FolderCaptionAccent.w));
	FolderCaptionAccent.Draw(AccentHeader, IGraphics::CORNER_NONE, 0.0f);
	CUIRect FolderCaptionText = FolderCaption;
	FolderCaptionText.VMargin(2.0f, &FolderCaptionText);
	SLabelProperties FolderCaptionProps;
	FolderCaptionProps.m_MaxWidth = std::max(0.0f, FolderCaptionText.w - 4.0f);
	FolderCaptionProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&FolderCaptionText, TCLocalize("Folders", "AMF Client"), 12.0f, TEXTALIGN_ML, FolderCaptionProps);
	FolderList.HSplitTop(5.0f, nullptr, &FolderList);
	FolderList.HSplitBottom(128.0f, &FolderList, &FolderCreateDropZone);
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab)
	{
		// Drop targets are frame-local. Leaving a folder/card must never retain a
		// stale target from a previous frame.
		gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
		gs_AssetOrganizerDrag.m_TargetRectValid = false;
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
		gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
	}
	if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab && Ui()->MouseButton(0))
	{
		// Folder insertion is recalculated for the current pointer position.
		// Keep the final valid destination for the release frame, but never reuse
		// a line that belonged to a row the pointer has already left.
		gs_AssetFolderDrag.m_TargetIndex = -1;
	}

	static CScrollRegion s_FolderScrollRegion;
	CScrollRegionParams FolderScrollParams;
	FolderScrollParams.m_ScrollbarThickness = 8.0f;
	FolderScrollParams.m_ScrollbarMargin = 2.0f;
	FolderScrollParams.m_ScrollUnit = 32.0f;
	FolderScrollParams.m_RailBgColor = AccentOutline.WithAlpha(0.22f);
	FolderScrollParams.m_SliderColor = AccentSoft;
	s_FolderScrollRegion.Begin(&FolderList, &FolderScrollParams);
	CUIRect FolderContent = FolderList;
	static CButtonContainer s_FavoritesFolderButton;
	const auto &vFolders = AssetOrganizer::Folders(s_CurCustomTab);
	char aCurrentFolderId[64];
	str_copy(aCurrentFolderId, AssetOrganizer::CurrentFolder(s_CurCustomTab), sizeof(aCurrentFolderId));
	auto RenderFolderRow = [&](const void *pId, const char *pIdString, const char *pLabel, const char *pIcon, int FolderIndex, bool UserFolder) {
		CUIRect Row;
		FolderContent.HSplitTop(30.0f, &Row, &FolderContent);
		FolderContent.HSplitTop(3.0f, nullptr, &FolderContent);
		if(!s_FolderScrollRegion.AddRect(Row))
			return;

		const bool Selected = str_comp(aCurrentFolderId, pIdString) == 0;
		const int Clicked = Ui()->DoButtonLogic(pId, 0, &Row, BUTTONFLAG_LEFT);
		const bool Hovered = Ui()->HotItem() == pId;
		const ColorRGBA RowFill = Selected ? AccentColor(AccentLightness, 0.20f) :
			(Hovered ? ColorRGBA(0.085f, 0.11f, 0.14f, 0.96f) : ColorRGBA(0.060f, 0.080f, 0.105f, 0.82f));
		const ColorRGBA RowBorder = Selected ? AccentSelected : (Hovered ? AccentSoft : NeutralBorderSoft);
		Row.Draw(RowBorder, IGraphics::CORNER_ALL, 4.0f);
		CUIRect RowInner = Row;
		RowInner.Margin(1.0f, &RowInner);
		RowInner.Draw(RowFill, IGraphics::CORNER_ALL, 3.0f);

		CUIRect IconRect, NameRect, CountRect;
		Row.VSplitLeft(27.0f, &IconRect, &NameRect);
		NameRect.VSplitRight(32.0f, &NameRect, &CountRect);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&IconRect, pIcon, 13.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		SLabelProperties FolderNameProps;
		FolderNameProps.m_MaxWidth = NameRect.w - 3.0f;
		Ui()->DoLabel(&NameRect, pLabel, 12.0f, TEXTALIGN_ML, FolderNameProps);
		char aCount[16];
		str_format(aCount, sizeof(aCount), "%d", AssetOrganizer::FolderItemCount(s_CurCustomTab, pIdString));
		Ui()->DoLabel(&CountRect, aCount, 11.0f, TEXTALIGN_MC);
		if(Clicked && !gs_AssetFolderDrag.m_Dragging)
		{
			AssetOrganizer::SetCurrentFolder(s_CurCustomTab, pIdString);
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
		if(UserFolder && Ui()->MouseHovered(&Row) && Ui()->MouseButtonClicked(1))
		{
			gs_AssetFolderPopupContext.m_pMenus = this;
			gs_AssetFolderPopupContext.m_Tab = s_CurCustomTab;
			str_copy(gs_AssetFolderPopupContext.m_aFolderId, pIdString, sizeof(gs_AssetFolderPopupContext.m_aFolderId));
			gs_AssetFolderPopupContext.m_NameInput.Set(pLabel);
			gs_AssetFolderPopupContext.m_NameInput.SetEmptyText(TCLocalize("Folder name", "AMF Client"));
			Ui()->DoPopupMenu(&gs_AssetFolderPopupContext, Ui()->MouseX(), Ui()->MouseY(), 176.0f, 84.0f, &gs_AssetFolderPopupContext, SAssetFolderPopupContext::Render);
		}

		if(UserFolder && gs_AssetFolderDrag.m_aSourceId[0] == '\0' && Ui()->CheckActiveItem(pId) && Ui()->MouseButton(0))
		{
			gs_AssetFolderDrag.m_Tab = s_CurCustomTab;
			gs_AssetFolderDrag.m_SourceIndex = FolderIndex;
			gs_AssetFolderDrag.m_PressPos = Ui()->MousePos();
			str_copy(gs_AssetFolderDrag.m_aSourceId, pIdString, sizeof(gs_AssetFolderDrag.m_aSourceId));
			str_copy(gs_AssetFolderDrag.m_aSourceName, pLabel, sizeof(gs_AssetFolderDrag.m_aSourceName));
		}
		if(gs_AssetFolderDrag.m_Tab == s_CurCustomTab && gs_AssetFolderDrag.m_aSourceId[0] != '\0' && Ui()->MouseButton(0) && !gs_AssetFolderDrag.m_Dragging && distance(Ui()->MousePos(), gs_AssetFolderDrag.m_PressPos) >= 6.0f)
			gs_AssetFolderDrag.m_Dragging = true;
		if(UserFolder && gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&Row))
		{
			int InsertAt = FolderIndex + (Ui()->MouseY() >= Row.y + Row.h / 2.0f ? 1 : 0);
			if(gs_AssetFolderDrag.m_SourceIndex < InsertAt)
				--InsertAt;
			const int TargetIndex = std::clamp(InsertAt, 1, std::max(1, (int)vFolders.size() - 1));
			// A line is only shown for a destination that produces an actual
			// reordering. Hovering the source row (or the adjacent no-op side) must
			// not advertise a drop target that MoveFolderTo would reject.
			if(TargetIndex != gs_AssetFolderDrag.m_SourceIndex)
			{
				gs_AssetFolderDrag.m_TargetIndex = TargetIndex;
				CUIRect InsertGuide = {Row.x + 4.0f, Ui()->MouseY() >= Row.y + Row.h / 2.0f ? Row.y + Row.h - 1.0f : Row.y, Row.w - 8.0f, 2.0f};
				InsertGuide.Draw(AccentSelected, IGraphics::CORNER_ALL, 1.0f);
			}
		}
		if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&Row) && !AssetOrganizer::IsFavoritesFolder(pIdString))
		{
			gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
			gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
			str_copy(gs_AssetOrganizerDrag.m_aTargetFolderId, pIdString, sizeof(gs_AssetOrganizerDrag.m_aTargetFolderId));
			gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
			gs_AssetOrganizerDrag.m_TargetRect = Row;
			gs_AssetOrganizerDrag.m_TargetRectValid = true;
			Row.Draw(AccentSoft, IGraphics::CORNER_ALL, 4.0f);
			CUIRect DragRowInner = Row;
			DragRowInner.Margin(1.0f, &DragRowInner);
			DragRowInner.Draw(AccentSoft.WithAlpha(0.28f), IGraphics::CORNER_ALL, 3.0f);
		}
	};
	for(size_t FolderIndex = 0; FolderIndex < vFolders.size(); ++FolderIndex)
	{
		const AssetOrganizer::SFolder &Folder = vFolders[FolderIndex];
		RenderFolderRow(&Folder, Folder.m_Id.c_str(), Folder.m_Name.c_str(), str_comp(Folder.m_Id.c_str(), AssetOrganizer::FOLDER_UNSORTED) == 0 ? FontIcon::FOLDER_OPEN : FontIcon::FOLDER, (int)FolderIndex, !Folder.m_System);
	}
	RenderFolderRow(&s_FavoritesFolderButton, AssetOrganizer::FOLDER_FAVORITES, TCLocalize("Favorites", "AMF Client"), FontIcon::STAR, -1, false);
	s_FolderScrollRegion.End();
	if(gs_AssetFolderDrag.m_Tab == s_CurCustomTab && gs_AssetFolderDrag.m_aSourceId[0] != '\0' && !Ui()->MouseButton(0))
	{
		if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_TargetIndex >= 1 && gs_AssetFolderDrag.m_TargetIndex != gs_AssetFolderDrag.m_SourceIndex)
			AssetOrganizer::MoveFolderTo(s_CurCustomTab, gs_AssetFolderDrag.m_aSourceId, gs_AssetFolderDrag.m_TargetIndex);
		gs_AssetFolderDrag.Reset();
	}
	if(gs_AssetFolderDrag.m_Dragging && gs_AssetFolderDrag.m_Tab == s_CurCustomTab)
	{
		CUIRect FolderGhost = {Ui()->MouseX() + 11.0f, Ui()->MouseY() + 9.0f, 145.0f, 27.0f};
		FolderGhost.Draw(AccentSelected, IGraphics::CORNER_ALL, 4.0f);
		CUIRect FolderGhostInner = FolderGhost;
		FolderGhostInner.Margin(1.0f, &FolderGhostInner);
		FolderGhostInner.Draw(AccentColor(AccentLightness, 0.72f), IGraphics::CORNER_ALL, 3.0f);
		SLabelProperties FolderGhostProps;
		FolderGhostProps.m_MaxWidth = FolderGhostInner.w - 8.0f;
		FolderGhostProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&FolderGhostInner, gs_AssetFolderDrag.m_aSourceName, 10.0f, TEXTALIGN_MC, FolderGhostProps);
	}

	// This is a passive drop target: it never owns a regular click, so it cannot
	// steal card selection. While a real drag is over it, dropping creates a
	// named folder and moves the item there in a single persisted operation.
	FolderCreateDropZone.Margin(5.0f, &FolderCreateDropZone);
	const bool HoveringCreateDropZone = gs_AssetOrganizerDrag.m_Dragging &&
		gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && Ui()->MouseHovered(&FolderCreateDropZone);
	// The organizer panel is the only solid container. The drop target itself
	// uses one dashed outline plus a quiet surface, avoiding a double frame.
	CUIRect FolderCreateDropZoneInner = FolderCreateDropZone;
	FolderCreateDropZoneInner.Margin(1.0f, &FolderCreateDropZoneInner);
	FolderCreateDropZoneInner.Draw(HoveringCreateDropZone ? AccentSoft.WithAlpha(0.58f) : ColorRGBA(0.030f, 0.045f, 0.065f, 0.84f), IGraphics::CORNER_ALL, 4.0f);
	const ColorRGBA DropBorder = HoveringCreateDropZone ? AccentSelected : NeutralBorderSoft;
	const float DashLength = 5.0f;
	const float DashGap = 4.0f;
	for(float X = FolderCreateDropZone.x + 4.0f; X < FolderCreateDropZone.x + FolderCreateDropZone.w - 4.0f; X += DashLength + DashGap)
	{
		const float Width = std::min(DashLength, FolderCreateDropZone.x + FolderCreateDropZone.w - 4.0f - X);
		CUIRect TopDash = {X, FolderCreateDropZone.y + 1.0f, Width, 1.0f};
		CUIRect BottomDash = {X, FolderCreateDropZone.y + FolderCreateDropZone.h - 2.0f, Width, 1.0f};
		TopDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
		BottomDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
	}
	for(float Y = FolderCreateDropZone.y + 4.0f; Y < FolderCreateDropZone.y + FolderCreateDropZone.h - 4.0f; Y += DashLength + DashGap)
	{
		const float Height = std::min(DashLength, FolderCreateDropZone.y + FolderCreateDropZone.h - 4.0f - Y);
		CUIRect LeftDash = {FolderCreateDropZone.x + 1.0f, Y, 1.0f, Height};
		CUIRect RightDash = {FolderCreateDropZone.x + FolderCreateDropZone.w - 2.0f, Y, 1.0f, Height};
		LeftDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
		RightDash.Draw(DropBorder, IGraphics::CORNER_NONE, 0.0f);
	}
	CUIRect DropIcon, DropText;
	FolderCreateDropZone.HSplitTop(FolderCreateDropZone.h * 0.52f, &DropIcon, &DropText);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	Ui()->DoLabel(&DropIcon, FontIcon::FOLDER, 28.0f, TEXTALIGN_MC);
	CUIRect DropPlus = {DropIcon.x + DropIcon.w * 0.56f, DropIcon.y + DropIcon.h * 0.43f, 13.0f, 13.0f};
	Ui()->DoLabel(&DropPlus, FontIcon::PLUS, 11.0f, TEXTALIGN_MC);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	DropText.Margin(12.0f, &DropText);
	SLabelProperties DropTextProps;
	DropTextProps.m_MaxWidth = DropText.w;
	DropTextProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&DropText, TCLocalize("Drop here to create folder", "AMF Client"), 8.5f, TEXTALIGN_MC, DropTextProps);
	if(HoveringCreateDropZone)
	{
		gs_AssetOrganizerDrag.m_CreateFolderDropTarget = true;
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
		gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
		gs_AssetOrganizerDrag.m_TargetRect = FolderCreateDropZone;
		gs_AssetOrganizerDrag.m_TargetRectValid = true;
	}

	// Page navigation belongs to the asset panel. Pages remain in the layout
	// even when empty, so the plus button always creates a stable destination.
	// Use the exact control height of the bottom "Assets directory" action. This
	// keeps the page row compact and visually consistent at every UI scale.
	AssetsPanel.HSplitTop(ORGANIZER_CONTROL_HEIGHT, &AssetHeader, &CustomList);
	// Split the remaining grid rect, not AssetsPanel again. The old second split
	// recreated CustomList from the full panel, which put card hitboxes on top of
	// the page controls even though the controls were visibly above them.
	CustomList.HSplitTop(6.0f, nullptr, &CustomList);
	// Keep one immutable parent rect for the complete header. It is the source
	// for the title, separator and every page-control visual center below.
	const CUIRect HeaderRect = AssetHeader;
	CUIRect FolderNameRect, PageControls;
	HeaderRect.VSplitLeft(150.0f, &FolderNameRect, &PageControls);
	SLabelProperties CurrentFolderProps;
	CurrentFolderProps.m_MaxWidth = FolderNameRect.w - 5.0f;
	Ui()->DoLabel(&FolderNameRect, AssetOrganizer::FolderName(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab)), 12.0f, TEXTALIGN_ML, CurrentFolderProps);
	CUIRect AssetHeaderLine = HeaderRect;
	AssetHeaderLine.HSplitBottom(1.0f, nullptr, &AssetHeaderLine);
	AssetHeaderLine.Draw(NeutralBorderSoft, IGraphics::CORNER_NONE, 0.0f);
	CUIRect AssetHeaderAccent = AssetHeaderLine;
	// Start the accent at the actual header/content edge. The previous extra
	// two-pixel shift created an unexplained dark "air" before the colored line.
	AssetHeaderAccent.w = std::min(34.0f, std::max(0.0f, AssetHeaderAccent.w));
	AssetHeaderAccent.Draw(AccentHeader, IGraphics::CORNER_NONE, 0.0f);
	constexpr float PAGE_CONTROL_GAP = 6.0f;
	constexpr float PAGE_NAV_WIDTH = 112.0f;
	const float PageIconWidth = ORGANIZER_CONTROL_HEIGHT;
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageAdd);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageNext);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PAGE_NAV_WIDTH, &PageControls, &OrganizerPageNav);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPagePrevious);
	PageControls.VSplitRight(PAGE_CONTROL_GAP, &PageControls, nullptr);
	PageControls.VSplitRight(PageIconWidth, &PageControls, &OrganizerPageDelete);
	// Keep every interaction rect (and therefore all X coordinates) unchanged.
	// AssetHeader is inset from the visible panel frame by the content padding,
	// so it is not the visual header's top edge. The visible header runs from the
	// frame top to the single separator drawn above; use those exact bounds for
	// the common Y center instead of centering from the inset interaction rect.
	CUIRect VisibleHeaderRect = AssetsPanelFrame;
	VisibleHeaderRect.h = std::max(0.0f, AssetHeaderLine.y + AssetHeaderLine.h - VisibleHeaderRect.y);
	const float HeaderCenterY = VisibleHeaderRect.y + VisibleHeaderRect.h * 0.5f;
	const float HeaderControlHeight = std::max(0.0f, HeaderRect.h - 4.0f);
	auto CenterHeaderControl = [&](const CUIRect &Control) {
		CUIRect Visual = Control;
		Visual.h = std::min(HeaderControlHeight, Control.h);
		Visual.y = HeaderCenterY - Visual.h * 0.5f;
		return Visual;
	};
	CUIRect OrganizerPagePreviousVisual = CenterHeaderControl(OrganizerPagePrevious);
	CUIRect OrganizerPageNextVisual = CenterHeaderControl(OrganizerPageNext);
	CUIRect OrganizerPageAddVisual = CenterHeaderControl(OrganizerPageAdd);
	CUIRect OrganizerPageDeleteVisual = CenterHeaderControl(OrganizerPageDelete);
	CUIRect OrganizerPageNavVisual = CenterHeaderControl(OrganizerPageNav);
	const bool CanEditPages = !AssetOrganizer::IsFavoritesFolder(AssetOrganizer::CurrentFolder(s_CurCustomTab));
	const int CurrentPage = AssetOrganizer::CurrentPage(s_CurCustomTab);
	const int PageCount = AssetOrganizer::PageCount(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab));
	if(DoOrganizerIconButton(&s_PagePreviousButton, FontIcon::CHEVRON_LEFT, &OrganizerPagePrevious, CurrentPage > 0, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPagePreviousVisual) && CurrentPage > 0)
	{
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, CurrentPage - 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	char aPageLabel[64];
	str_format(aPageLabel, sizeof(aPageLabel), "%s %d / %d", TCLocalize("Page", "AMF Client"), CurrentPage + 1, PageCount);
	OrganizerPageNavVisual.Draw(NeutralBorderSoft, IGraphics::CORNER_ALL, 4.0f);
	CUIRect OrganizerPageNavInner = OrganizerPageNavVisual;
	OrganizerPageNavInner.Margin(1.0f, &OrganizerPageNavInner);
	OrganizerPageNavInner.Draw(ColorRGBA(0.045f, 0.060f, 0.080f, 0.92f), IGraphics::CORNER_ALL, 3.0f);
	SLabelProperties PageLabelProps;
	PageLabelProps.m_MaxWidth = OrganizerPageNavInner.w - 6.0f;
	PageLabelProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&OrganizerPageNavInner, aPageLabel, 10.5f, TEXTALIGN_MC, PageLabelProps);
	if(DoOrganizerIconButton(&s_PageNextButton, FontIcon::CHEVRON_RIGHT, &OrganizerPageNext, CurrentPage + 1 < PageCount, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPageNextVisual) && CurrentPage + 1 < PageCount)
	{
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, CurrentPage + 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	if(DoOrganizerIconButton(&s_PageAddButton, FontIcon::PLUS, &OrganizerPageAdd, CanEditPages, false, 12.0f, vec2(0.0f, 0.0f), &OrganizerPageAddVisual) && CanEditPages)
	{
		AssetOrganizer::AddPage(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab));
		AssetOrganizer::SetCurrentPage(s_CurCustomTab, AssetOrganizer::PageCount(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab)) - 1);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_PageAddButton, &OrganizerPageAdd, TCLocalize("New page", "AMF Client"));
	const bool CanDeletePage = CanEditPages && PageCount > 1;
	TextRender()->TextColor(CanDeletePage ? ColorRGBA(1.0f, 0.70f, 0.72f, 0.98f) : ColorRGBA(0.45f, 0.49f, 0.54f, 0.65f));
	if(DoOrganizerIconButton(&s_PageDeleteButton, FontIcon::TRASH, &OrganizerPageDelete, CanDeletePage, false, 11.0f, vec2(0.0f, 0.0f), &OrganizerPageDeleteVisual) && CanDeletePage)
	{
		AssetOrganizer::DeletePage(s_CurCustomTab, AssetOrganizer::CurrentFolder(s_CurCustomTab), CurrentPage);
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	GameClient()->m_Tooltips.DoToolTip(&s_PageDeleteButton, &OrganizerPageDelete, TCLocalize("Delete page", "AMF Client"));
	if(gs_aInitCustomList[s_CurCustomTab])
	{
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			ListSize = InitSearchList(gs_vpSearchEntitiesList, m_vEntitiesList);
			SortSearchList(gs_vpSearchEntitiesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
			SortSearchList(gs_vpSearchGamesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
			SortSearchList(gs_vpSearchEmoticonsList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
			SortSearchList(gs_vpSearchParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
			SortSearchList(gs_vpSearchHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
			SortSearchList(gs_vpSearchExtrasList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
		{
			ListSize = InitSearchList(gs_vpSearchCursorList, m_vCursorList);
			SortSearchList(gs_vpSearchCursorList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ListSize = InitSearchList(gs_vpSearchArrowList, m_vArrowList);
			SortSearchList(gs_vpSearchArrowList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			ListSize = InitSearchList(gs_vpSearchAudioPackList, m_vAudioPackList);
			SortSearchList(gs_vpSearchAudioPackList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	constexpr float CardSpacing = 3.0f;
	float TextureWidth = 132.0f;
	float TextureHeight = 112.0f;

	size_t SearchListSize = 0;
	bool SkipSelectionBecauseDelete = false;

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		SearchListSize = gs_vpSearchCursorList.size();
		TextureHeight = 82.0f;
		TextureWidth = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		SearchListSize = gs_vpSearchArrowList.size();
		TextureHeight = 82.0f;
		TextureWidth = 82.0f;
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		SearchListSize = gs_vpSearchAudioPackList.size();
		TextureHeight = 74.0f;
		TextureWidth = 96.0f;
	}
	// Resolve the active asset before DoStart. Previously this was done inside
	// the card loop after DoStart had already captured -1 as the selected index,
	// which made selection state lag a frame and conflicted with drag release.
	for(size_t Index = 0; Index < SearchListSize; ++Index)
	{
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, Index);
		if(pItem && AssetsIsCurrentlySelected(s_CurCustomTab, pItem->m_aName))
		{
			OldSelected = (int)Index;
			break;
		}
	}

	static CListBox s_ListBox;
	// The listbox provides only scrolling and page-sized rows here. Every visual
	// and mouse interaction below uses the exact same card rect, avoiding the
	// old mismatch between a padded preview and an unpadded listbox hitbox.
	const float ItemHeight = TextureHeight + 36.0f;
	const int ItemsPerRow = std::max(1, (int)((CustomList.w + CardSpacing) / (TextureWidth + CardSpacing)));
	// CListBox auto-spacing is per item, not per row. Keep it disabled for a
	// multi-column grid so scrolling and hit geometry remain deterministic.
	s_ListBox.DoAutoSpacing(0.0f);
	s_ListBox.DoStart(ItemHeight, SearchListSize, ItemsPerRow, 1, OldSelected, &CustomList, false, IGraphics::CORNER_ALL, false, CardSpacing);
	for(size_t i = 0; i < SearchListSize; ++i)
	{
		if(i > 0 && i % ItemsPerRow == 0)
			s_ListBox.DoSpacing(CardSpacing);
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
		if(pItem == nullptr)
			continue;

		const bool Favorite = IsFavoriteAsset(s_CurCustomTab, pItem->m_aName);
		const bool CanDelete = pItem->m_Deletable;
		const CListboxItem Item = s_ListBox.DoNextItem(&pItem->m_CardButtonId, OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
			continue;
		const CUIRect CardRect = Item.m_Rect;
		CUIRect ItemRect = CardRect;
		// This is the original asset-selector card inset. Card/grid dimensions and
		// the listbox hitbox stay unchanged; restoring the inset restores the old
		// favorite padding and the old preview's content origin together.
		ItemRect.Margin(5.0f, &ItemRect);
		CUIRect HeaderRow, ActionRow, TextureRect;
		ItemRect.HSplitTop(20.0f, &HeaderRow, nullptr);
		CUIRect FavoriteButton, DeleteButton;
		HeaderRow.VSplitLeft(20.0f, &FavoriteButton, nullptr);
		ItemRect.HSplitBottom(20.0f, nullptr, &ActionRow);
		if(CanDelete)
		{
			ActionRow.VSplitRight(20.0f, nullptr, &DeleteButton);
		}
		else
			DeleteButton = {0, 0, 0, 0};
		constexpr float CARD_ACTION_SCALE = 0.98f;
		// Keep the original 20-unit slots and centers, but make both action
		// hitboxes uniformly smaller by the requested couple of percent.
		FavoriteButton.Margin((20.0f - 20.0f * CARD_ACTION_SCALE) * 0.5f, &FavoriteButton);
		if(CanDelete)
			DeleteButton.Margin((20.0f - 20.0f * CARD_ACTION_SCALE) * 0.5f, &DeleteButton);
		// Keep both action controls centered in their original 20-unit slots while
		// using the uniformly reduced hitboxes for the favorite/delete actions.
		constexpr float CARD_ACTION_HEIGHT = 20.0f * CARD_ACTION_SCALE;
		constexpr float CARD_VISIBLE_ACTION_HEIGHT = 16.0f * CARD_ACTION_SCALE;
		const float CARD_ACTION_VERTICAL_INSET = (CARD_ACTION_HEIGHT - CARD_VISIBLE_ACTION_HEIGHT) * 0.5f;
		CUIRect FavoriteVisual = FavoriteButton;
		CUIRect DeleteVisual = DeleteButton;
		if(CanDelete)
			DeleteVisual.Margin(vec2(0.0f, CARD_ACTION_VERTICAL_INSET), &DeleteVisual);
		// Restore the historical organizer preview ownership from
		// menus_settings_assets.cpp. The title and actions keep their existing
		// bands, while the texture is centered in the remaining card content.
		TextureRect = ItemRect;
		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			TextureRect.HSplitTop(15.0f, nullptr, &TextureRect);
			TextureRect.HSplitTop(10.0f, nullptr, &TextureRect);
			TextureRect.HSplitBottom(20.0f, &TextureRect, nullptr);
		}
		// Center every pack name in a stable, symmetric title area. The favorite
		// control remains in its fixed 20-unit slot, but it must not shift the
		// title's visual center toward the right.
		CUIRect TitleRect = HeaderRow;
		TitleRect.VMargin(23.0f, &TitleRect);
		TitleRect.y -= 3.0f;
		const bool CardSelected = OldSelected >= 0 && (size_t)OldSelected == i;
		const bool CardHovered = Ui()->MouseHovered(&CardRect);
		const bool CardIsDragSource = gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem == pItem;
		const ColorRGBA CardColor = CardHovered ? ColorRGBA(0.085f, 0.11f, 0.14f, 0.97f) : ColorRGBA(0.045f, 0.060f, 0.080f, 0.94f);
		// A subtle frame gives every card a stable silhouette. Selection/hover
		// changes only the frame (and the neutral hover surface); the CListBox
		// rect remains the sole interaction and drag hitbox.
		const ColorRGBA CardBorder = CardSelected ? AccentSelected : (CardHovered ? AccentSoft : NeutralBorderSoft);
		CardRect.Draw(CardBorder, IGraphics::CORNER_ALL, 5.0f);
		CUIRect CardInner = CardRect;
		// Keep the content template identical for normal, hover and selected
		// cards. The selected state is carried by the border color only, so it
		// cannot change preview padding or the grid's perceived card size.
		CardInner.Margin(1.0f, &CardInner);
		CardInner.Draw(CardColor, IGraphics::CORNER_ALL, 4.0f);

		// CListBox owns the button activation. Do not call SetActiveItem here:
		// DoButtonLogic also stores which mouse button activated the item, and
		// bypassing it leaves m_ActiveButtonLogicButton at -1 (the assertion from
		// the previous implementation).
		if(gs_AssetOrganizerDrag.m_pSourceItem == nullptr && CardSelected && Ui()->CheckActiveItem(&pItem->m_CardButtonId) && Ui()->MouseButton(0) && !Ui()->MouseHovered(&FavoriteButton) && !Ui()->MouseHovered(&DeleteButton))
		{
			gs_AssetOrganizerDrag.m_pSourceItem = pItem;
			gs_AssetOrganizerDrag.m_Tab = s_CurCustomTab;
			gs_AssetOrganizerDrag.m_PressPos = Ui()->MousePos();
			gs_AssetOrganizerDrag.m_SourceRect = CardRect;
			gs_AssetOrganizerDrag.m_DragOffset = Ui()->MousePos() - vec2(CardRect.x, CardRect.y);
			gs_AssetOrganizerDrag.m_GhostPos = vec2(CardRect.x, CardRect.y);
			gs_AssetOrganizerDrag.m_Dragging = false;
			gs_AssetOrganizerDrag.m_TargetRectValid = false;
			gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
			gs_AssetOrganizerDrag.m_aTargetAssetId[0] = '\0';
		}
		if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem != pItem && Ui()->MouseHovered(&CardRect))
		{
			gs_AssetOrganizerDrag.m_CreateFolderDropTarget = false;
			gs_AssetOrganizerDrag.m_CurrentPageDropTarget = false;
			str_copy(gs_AssetOrganizerDrag.m_aTargetAssetId, pItem->m_aName, sizeof(gs_AssetOrganizerDrag.m_aTargetAssetId));
			gs_AssetOrganizerDrag.m_aTargetFolderId[0] = '\0';
			gs_AssetOrganizerDrag.m_TargetRect = CardRect;
			gs_AssetOrganizerDrag.m_TargetRectValid = true;
			CardRect.Draw(AccentSoft, IGraphics::CORNER_ALL, 5.0f);
		}

		// Do not register a second full-card button for RMB. A right-only
		// DoButtonLogic call steals HotItem from CListBox's left-button card on the
		// same frame, which made selection and dragging impossible. The exact mouse
		// event is sufficient for opening a popup and does not mutate active state.
		if(Ui()->MouseHovered(&CardRect) && Ui()->MouseButtonClicked(1))
		{
			const float PopupX = Ui()->MouseX();
			const float PopupY = Ui()->MouseY();
			gs_AssetOrganizerPopupContext.m_Item.Retarget(s_CurCustomTab, pItem->m_aName, PopupX, PopupY);
			gs_AssetOrganizerPopupContext.m_pMenus = this;
			str_copy(gs_AssetOrganizerPopupContext.m_aTargetFolderId, AssetOrganizer::ItemFolder(s_CurCustomTab, pItem->m_aName), sizeof(gs_AssetOrganizerPopupContext.m_aTargetFolderId));
			gs_AssetOrganizerPopupContext.m_TargetPage = AssetOrganizer::ItemPage(s_CurCustomTab, pItem->m_aName);
			// Refresh the existing instance's anchor after every RMB. DoPopupMenu
			// is used only when the popup manager confirms no instance is open.
			if(!Ui()->UpdatePopupMenuPosition(&gs_AssetOrganizerPopupContext, PopupX, PopupY, 178.0f, 112.0f))
				Ui()->DoPopupMenu(&gs_AssetOrganizerPopupContext, PopupX, PopupY, 178.0f, 112.0f, &gs_AssetOrganizerPopupContext, SAssetOrganizerPopupContext::Render);
		}

		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			// Keep the title in the header band and the preview in its own band.
			CUIRect IconRect = TextureRect;
			SLabelProperties AudioLabelProps;
			AudioLabelProps.m_MaxWidth = std::max(0.0f, TitleRect.w - 4.0f);
			AudioLabelProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&TitleRect, pItem->m_aName, 10.0f, TEXTALIGN_MC, AudioLabelProps);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			TextRender()->TextColor(AudioNoteColor);
				const float MusicFontSize = 28.0f;
				CUIRect MusicVisual = IconRect;
				const vec2 MusicOffset = AudioPreviewOpticalOffset(MusicFontSize);
				MusicVisual.x += MusicOffset.x;
				const float PreviewAreaCenterY = IconRect.y + IconRect.h * 0.5f;
				MusicVisual.y = PreviewAreaCenterY - MusicVisual.h * 0.5f;
				Ui()->DoLabel(&MusicVisual, FontIcon::MUSIC, MusicFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
		// Keep the title in the current header band. The preview quad below uses
		// the original asset-selector max-fit limits while the current action
		// hitboxes remain unchanged.
		SLabelProperties AssetLabelProps;
		AssetLabelProps.m_MaxWidth = std::max(0.0f, TitleRect.w - 4.0f);
		AssetLabelProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&TitleRect, pItem->m_aName, 10.0f, TEXTALIGN_MC, AssetLabelProps);
		// This is the legacy size calculation: cap the preview at the per-category
		// dimensions, then aspect-fit the item's native preview ratio inside the
		// safe card content. Do not replace this with a fixed-scale layout.
		const float MaxPreviewWidth = std::max(1.0f, std::min(TextureWidth, TextureRect.w - 8.0f));
		const float MaxPreviewHeight = std::max(1.0f, std::min(TextureHeight, TextureRect.h - 6.0f));
		const float PreviewAspect = std::clamp(pItem->m_PreviewAspect, 0.1f, 10.0f);
		float PreviewWidth = MaxPreviewWidth;
		float PreviewHeight = PreviewWidth / PreviewAspect;
		if(PreviewHeight > MaxPreviewHeight)
		{
			PreviewHeight = MaxPreviewHeight;
			PreviewWidth = PreviewHeight * PreviewAspect;
		}
		if(s_CurCustomTab == ASSETS_TAB_CURSOR || s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			PreviewWidth *= 0.8f;
			PreviewHeight *= 0.8f;
		}
		// Keep the restored legacy preview virtually unchanged while leaving a
		// small, resolution-independent visual clearance from the card actions.
		constexpr float PREVIEW_SCALE = 0.975f;
		PreviewWidth *= PREVIEW_SCALE;
		PreviewHeight *= PREVIEW_SCALE;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview)
		{
			const auto *pEntitiesItem = static_cast<const SCustomEntities *>(pItem);
			IGraphics::CTextureHandle Tex;
			for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT && !Tex.IsValid(); m++)
				Tex = pEntitiesItem->m_aImages[m].m_Texture;
			if(!Tex.IsValid())
				Tex = pItem->m_RenderTexture;

			if(Tex.IsValid())
			{
				static const int COLS = 7, ROWS = 7;
				static const unsigned char aLayout[ROWS][COLS] = {
					{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
					{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
					{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
					{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
					{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
				};

				const float TileSize = std::min(PreviewWidth / (float)COLS, PreviewHeight / (float)ROWS);
				const float PreviewSceneWidth = COLS * TileSize;
				const float OffX = TextureRect.x + (TextureRect.w - PreviewSceneWidth) / 2.0f;
				const float OffY = TextureRect.y + (TextureRect.h - ROWS * TileSize) / 2.0f;
				const float KInset = 1.5f / 1024.0f;
				const float KTile = 1.0f / 16.0f;

				Graphics()->WrapClamp();
				Graphics()->TextureSet(Tex);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				for(int r = 0; r < ROWS; r++)
				{
					for(int c = 0; c < COLS; c++)
					{
						const unsigned char Tile = aLayout[r][c];
						if(Tile == 0)
							continue;
						const int Tx = Tile % 16;
						const int Ty = Tile / 16;
						const float U0 = Tx * KTile + KInset;
						const float V0 = Ty * KTile + KInset;
						const float U1 = U0 + KTile - KInset * 2;
						const float V1 = V0 + KTile - KInset * 2;
						Graphics()->QuadsSetSubset(U0, V0, U1, V1);
						IGraphics::CQuadItem Q(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
						Graphics()->QuadsDrawTL(&Q, 1);
					}
				}
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
		}
		else if(pItem->m_RenderTexture.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(pItem->m_RenderTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			const vec2 PreviewOffset = AssetPreviewOpticalOffset(s_CurCustomTab, pItem->m_aName, PreviewWidth, PreviewHeight);
			IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - PreviewWidth) / 2.0f + PreviewOffset.x, TextureRect.y + (TextureRect.h - PreviewHeight) / 2.0f + PreviewOffset.y, PreviewWidth, PreviewHeight);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		} // end else (non-audio rendering)
		if(CardIsDragSource)
		{
			CardRect.Draw(AccentSelected.WithAlpha(0.82f), IGraphics::CORNER_ALL, 5.0f);
			CUIRect DragCardInner = CardRect;
			DragCardInner.Margin(1.5f, &DragCardInner);
			DragCardInner.Draw(ColorRGBA(0.025f, 0.055f, 0.08f, 0.70f), IGraphics::CORNER_ALL, 4.0f);
		}
		// Labels are intentionally compact; the card tooltip always exposes the
		// complete pack name without changing grid geometry for long names.
		GameClient()->m_Tooltips.DoToolTip(&pItem->m_CardButtonId, &CardRect, pItem->m_aName);

		if(CanDelete)
		{
			TextRender()->TextColor(ColorRGBA(0.95f, 0.42f, 0.42f, 0.95f));
			const ColorRGBA DeleteFill = Ui()->MouseHovered(&DeleteButton) ? ColorRGBA(0.28f, 0.11f, 0.14f, 0.98f) : ColorRGBA(0.20f, 0.08f, 0.11f, 0.96f);
			DeleteVisual.Draw(ColorRGBA(0.60f, 0.20f, 0.24f, 0.74f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect DeleteInner = DeleteVisual;
			DeleteInner.Margin(1.0f, &DeleteInner);
			DeleteInner.Draw(DeleteFill, IGraphics::CORNER_ALL, 3.0f);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			Ui()->DoLabel(&DeleteVisual, FontIcon::TRASH, 11.0f * CARD_ACTION_SCALE, TEXTALIGN_MC);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			// The card already owns the listbox button. A second overlapping
			// DoButtonLogic corrupts the UI active-button bookkeeping, so compact
			// card actions consume the exact mouse event without registering a
			// second active item over the same rectangle.
			if(Ui()->MouseHovered(&DeleteButton) && Ui()->MouseButtonClicked(0))
			{
				SkipSelectionBecauseDelete = true;
				str_copy(m_aDeleteAssetName, pItem->m_aName);
				m_DeleteAssetTab = s_CurCustomTab;
				char aBuf[128 + sizeof(pItem->m_aName)];
				str_format(aBuf, sizeof(aBuf), Localize("Are you sure that you want to delete '%s'?"), pItem->m_aName);
				PopupConfirm(Localize("Delete asset"), aBuf, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDeleteAsset);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			GameClient()->m_Tooltips.DoToolTip(&pItem->m_DeleteButtonId, &DeleteButton, Localize("Delete this asset from your assets directory."));
		}

		// Render STAR through the same plain icon-font path as the visible folder
		// star. The generic compact icon helper used metrics that collapsed this
		// glyph into an empty-looking square on the actual Release renderer.
		const bool StarHovered = Ui()->MouseHovered(&FavoriteButton);
		const bool StarPressed = StarHovered && Ui()->MouseButton(0);
		const ColorRGBA FavoriteFill = Favorite ? AccentColor(AccentLightness, 0.72f) :
			(StarPressed ? AccentPressed : (StarHovered ? NeutralButtonHover : NeutralButton));
		const ColorRGBA FavoriteBorder = Favorite ? AccentSelected :
			(StarHovered || StarPressed ? AccentSoft : NeutralBorderSoft);
		FavoriteVisual.Draw(FavoriteBorder, IGraphics::CORNER_ALL, 4.0f);
		CUIRect FavoriteInner = FavoriteVisual;
		FavoriteInner.Margin(1.0f, &FavoriteInner);
		FavoriteInner.Draw(FavoriteFill, IGraphics::CORNER_ALL, 3.0f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->TextColor(AccentHover.WithAlpha(1.0f));
		CUIRect StarVisual = FavoriteVisual;
		// Render the natural STAR bitmap instead of the compressed NO_OVERSIZE
		// variant. With the x/y bearings disabled its actual glyph bounds equal
		// the measured advance box, so TEXTALIGN_MC centers the visible bitmap in
		// the unchanged 20x20 square on both axes.
		const float FavoriteCenterX = FavoriteVisual.x + FavoriteVisual.w * 0.5f;
		const float FavoriteCenterY = FavoriteVisual.y + FavoriteVisual.h * 0.5f;
		StarVisual.x = FavoriteCenterX - StarVisual.w * 0.5f;
		StarVisual.y = FavoriteCenterY - StarVisual.h * 0.5f;
		SLabelProperties StarProps;
		StarProps.m_MaxWidth = StarVisual.w;
		Ui()->DoLabel(&StarVisual, FontIcon::STAR, 14.0f * CARD_ACTION_SCALE, TEXTALIGN_MC, StarProps);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(0);
		if(StarHovered && Ui()->MouseButtonClicked(0))
		{
			SkipSelectionBecauseDelete = true;
			SetAssetOrganizerFavorite(s_CurCustomTab, pItem->m_aName, !Favorite);
			gs_aInitCustomList[s_CurCustomTab] = true;
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		GameClient()->m_Tooltips.DoToolTip(&pItem->m_FavoriteButtonId, &FavoriteButton,
			Favorite ? Localize("Click to remove this item from your favorites.") : Localize("Click to add this item to your favorites."));
	}
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab &&
		gs_AssetOrganizerDrag.m_aTargetFolderId[0] == '\0' && gs_AssetOrganizerDrag.m_aTargetAssetId[0] == '\0' &&
		!gs_AssetOrganizerDrag.m_CreateFolderDropTarget && !AssetOrganizer::IsFavoritesFolder(AssetOrganizer::CurrentFolder(s_CurCustomTab)) && Ui()->MouseHovered(&CustomList))
	{
		// Empty grid space is a useful, unambiguous "append to this page" drop
		// target. It lets users place an item after the final card without a
		// separate Position control.
		gs_AssetOrganizerDrag.m_CurrentPageDropTarget = true;
	}

	bool DragFinished = false;
	if(gs_AssetOrganizerDrag.m_pSourceItem != nullptr && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab)
	{
		if(Ui()->MouseButton(0))
		{
			if(!gs_AssetOrganizerDrag.m_Dragging && distance(Ui()->MousePos(), gs_AssetOrganizerDrag.m_PressPos) >= 6.0f)
				gs_AssetOrganizerDrag.m_Dragging = true;
			if(gs_AssetOrganizerDrag.m_Dragging)
			{
				const vec2 DesiredPos = Ui()->MousePos() - gs_AssetOrganizerDrag.m_DragOffset;
				// A short visual lag gives the lifted-card feel of desktop icon drag
				// without allocating or running any animation while no drag is active.
				gs_AssetOrganizerDrag.m_GhostPos += (DesiredPos - gs_AssetOrganizerDrag.m_GhostPos) * 0.48f;
			}
		}
		else
		{
			DragFinished = gs_AssetOrganizerDrag.m_Dragging;
			if(DragFinished)
			{
				const char *pSourceId = gs_AssetOrganizerDrag.m_pSourceItem->m_aName;
				if(gs_AssetOrganizerDrag.m_CreateFolderDropTarget)
				{
					std::string CreatedId;
					const std::string BaseName = TCLocalize("New folder", "AMF Client");
					for(int Number = 1; Number < 1000 && CreatedId.empty(); ++Number)
					{
						const std::string Name = Number == 1 ? BaseName : BaseName + " " + std::to_string(Number);
						AssetOrganizer::CreateFolder(s_CurCustomTab, Name.c_str(), &CreatedId);
					}
					if(!CreatedId.empty())
					{
						AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, CreatedId.c_str(), 0, 0);
						AssetOrganizer::SetCurrentFolder(s_CurCustomTab, CreatedId.c_str());
					}
				}
				else if(gs_AssetOrganizerDrag.m_aTargetFolderId[0] != '\0')
				{
					AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, gs_AssetOrganizerDrag.m_aTargetFolderId, 0, AssetOrganizer::ItemCount(s_CurCustomTab, gs_AssetOrganizerDrag.m_aTargetFolderId, 0));
				}
				else if(gs_AssetOrganizerDrag.m_aTargetAssetId[0] != '\0')
				{
					const char *pTargetId = gs_AssetOrganizerDrag.m_aTargetAssetId;
					AssetOrganizer::SwapItems(s_CurCustomTab, pSourceId, pTargetId);
				}
				else if(gs_AssetOrganizerDrag.m_CurrentPageDropTarget)
				{
					const char *pFolderId = AssetOrganizer::CurrentFolder(s_CurCustomTab);
					const int Page = AssetOrganizer::CurrentPage(s_CurCustomTab);
					AssetOrganizer::MoveItem(s_CurCustomTab, pSourceId, pFolderId, Page, AssetOrganizer::ItemCount(s_CurCustomTab, pFolderId, Page));
				}
				gs_AssetOrganizerSettle.m_Active = true;
				gs_AssetOrganizerSettle.m_Tab = s_CurCustomTab;
				gs_AssetOrganizerSettle.m_Start = time_get_nanoseconds();
				gs_AssetOrganizerSettle.m_From = {gs_AssetOrganizerDrag.m_GhostPos.x - 2.0f, gs_AssetOrganizerDrag.m_GhostPos.y - 2.0f, gs_AssetOrganizerDrag.m_SourceRect.w + 4.0f, gs_AssetOrganizerDrag.m_SourceRect.h + 4.0f};
				gs_AssetOrganizerSettle.m_To = gs_AssetOrganizerSettle.m_From;
				if(gs_AssetOrganizerDrag.m_TargetRectValid)
				{
					gs_AssetOrganizerSettle.m_To = gs_AssetOrganizerDrag.m_TargetRect;
					if(gs_AssetOrganizerDrag.m_aTargetAssetId[0] == '\0')
					{
						const vec2 Center(gs_AssetOrganizerSettle.m_To.x + gs_AssetOrganizerSettle.m_To.w / 2.0f, gs_AssetOrganizerSettle.m_To.y + gs_AssetOrganizerSettle.m_To.h / 2.0f);
						gs_AssetOrganizerSettle.m_To.w = gs_AssetOrganizerDrag.m_SourceRect.w * 0.24f;
						gs_AssetOrganizerSettle.m_To.h = gs_AssetOrganizerDrag.m_SourceRect.h * 0.24f;
						gs_AssetOrganizerSettle.m_To.x = Center.x - gs_AssetOrganizerSettle.m_To.w / 2.0f;
						gs_AssetOrganizerSettle.m_To.y = Center.y - gs_AssetOrganizerSettle.m_To.h / 2.0f;
					}
				}
				gs_AssetOrganizerSettle.m_Texture = gs_AssetOrganizerDrag.m_pSourceItem->m_RenderTexture;
				gs_AssetOrganizerSettle.m_PreviewAspect = gs_AssetOrganizerDrag.m_pSourceItem->m_PreviewAspect;
				str_copy(gs_AssetOrganizerSettle.m_aName, gs_AssetOrganizerDrag.m_pSourceItem->m_aName, sizeof(gs_AssetOrganizerSettle.m_aName));
				gs_aInitCustomList[s_CurCustomTab] = true;
			}
			gs_AssetOrganizerDrag.Reset();
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	auto RenderFloatingCard = [&](const CUIRect &Rect, const char *pName, IGraphics::CTextureHandle Texture, float Aspect, int Tab, float Alpha) {
		CUIRect Shadow = Rect;
		Shadow.x += 3.0f;
		Shadow.y += 4.0f;
		Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.34f * Alpha), IGraphics::CORNER_ALL, 6.0f);
		Rect.Draw(AccentSelected.WithMultipliedAlpha(Alpha), IGraphics::CORNER_ALL, 6.0f);
		CUIRect Inner = Rect;
		Inner.Margin(1.5f, &Inner);
		Inner.Draw(ColorRGBA(0.045f, 0.065f, 0.090f, 0.97f * Alpha), IGraphics::CORNER_ALL, 5.0f);
		Inner.Margin(5.0f, &Inner);
		CUIRect Title, Preview = Inner;
		Inner.HSplitTop(20.0f, &Title, nullptr);
		Title.VSplitLeft(20.0f, nullptr, &Title);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, Title.w - 3.0f);
		Props.m_EllipsisAtEnd = true;
		TextRender()->TextColor(ColorRGBA(0.93f, 0.96f, 1.0f, Alpha));
		Ui()->DoLabel(&Title, pName, 9.5f, TEXTALIGN_MC, Props);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		CUIRect DragStar = {Inner.x, Inner.y - 1.0f, 18.0f, 18.0f};
		Ui()->DoLabel(&DragStar, FontIcon::STAR, 12.0f, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(Tab == ASSETS_TAB_AUDIO || !Texture.IsValid())
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			TextRender()->TextColor(AudioNoteColor.WithMultipliedAlpha(Alpha));
			const float MusicFontSize = std::min(28.0f, Preview.h * 0.55f);
			CUIRect MusicVisual = Preview;
			const vec2 MusicOffset = AudioPreviewOpticalOffset(MusicFontSize);
			MusicVisual.x += MusicOffset.x;
			MusicVisual.y += MusicOffset.y;
			Ui()->DoLabel(&MusicVisual, FontIcon::MUSIC, MusicFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
		else
		{
			Aspect = std::clamp(Aspect, 0.1f, 10.0f);
			float Width = std::max(1.0f, Preview.w - 8.0f);
			float Height = Width / Aspect;
			if(Height > Preview.h - 6.0f)
			{
				Height = std::max(1.0f, Preview.h - 6.0f);
				Width = Height * Aspect;
			}
			if(Tab == ASSETS_TAB_CURSOR || Tab == ASSETS_TAB_ARROW)
			{
				Width *= 0.8f;
				Height *= 0.8f;
			}
			const vec2 PreviewOffset = AssetPreviewOpticalOffset(Tab, pName, Width, Height);
			Graphics()->WrapClamp();
			Graphics()->TextureSet(Texture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.92f * Alpha);
			IGraphics::CQuadItem Quad(Preview.x + (Preview.w - Width) / 2.0f + PreviewOffset.x, Preview.y + (Preview.h - Height) / 2.0f + PreviewOffset.y, Width, Height);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};
	if(gs_AssetOrganizerDrag.m_Dragging && gs_AssetOrganizerDrag.m_Tab == s_CurCustomTab && gs_AssetOrganizerDrag.m_pSourceItem != nullptr)
	{
		const SCustomItem *pDragItem = gs_AssetOrganizerDrag.m_pSourceItem;
		CUIRect DragCard = {gs_AssetOrganizerDrag.m_GhostPos.x - 2.0f, gs_AssetOrganizerDrag.m_GhostPos.y - 2.0f, gs_AssetOrganizerDrag.m_SourceRect.w + 4.0f, gs_AssetOrganizerDrag.m_SourceRect.h + 4.0f};
		RenderFloatingCard(DragCard, pDragItem->m_aName, pDragItem->m_RenderTexture, pDragItem->m_PreviewAspect, s_CurCustomTab, 0.94f);
	}
	if(gs_AssetOrganizerSettle.m_Active)
	{
		const float Progress = std::clamp(std::chrono::duration<float>(time_get_nanoseconds() - gs_AssetOrganizerSettle.m_Start).count() / 0.16f, 0.0f, 1.0f);
		const float Ease = 1.0f - (1.0f - Progress) * (1.0f - Progress) * (1.0f - Progress);
		CUIRect Rect = {
			gs_AssetOrganizerSettle.m_From.x + (gs_AssetOrganizerSettle.m_To.x - gs_AssetOrganizerSettle.m_From.x) * Ease,
			gs_AssetOrganizerSettle.m_From.y + (gs_AssetOrganizerSettle.m_To.y - gs_AssetOrganizerSettle.m_From.y) * Ease,
			gs_AssetOrganizerSettle.m_From.w + (gs_AssetOrganizerSettle.m_To.w - gs_AssetOrganizerSettle.m_From.w) * Ease,
			gs_AssetOrganizerSettle.m_From.h + (gs_AssetOrganizerSettle.m_To.h - gs_AssetOrganizerSettle.m_From.h) * Ease};
		RenderFloatingCard(Rect, gs_AssetOrganizerSettle.m_aName, gs_AssetOrganizerSettle.m_Texture, gs_AssetOrganizerSettle.m_PreviewAspect, gs_AssetOrganizerSettle.m_Tab, 1.0f - Progress * 0.75f);
		if(Progress >= 1.0f)
			gs_AssetOrganizerSettle.m_Active = false;
	}
	bool AssetSelectionChanged = false;
	if(OldSelected != NewSelected && NewSelected >= 0 && !SkipSelectionBecauseDelete && !DragFinished)
	{
		const SCustomItem *pSelectedItem = GetCustomItem(s_CurCustomTab, NewSelected);
		if(pSelectedItem != nullptr && pSelectedItem->m_aName[0] != '\0')
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				str_copy(g_Config.m_ClAssetsEntities, pSelectedItem->m_aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(pSelectedItem->m_aName);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, pSelectedItem->m_aName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, pSelectedItem->m_aName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, pSelectedItem->m_aName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, pSelectedItem->m_aName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				str_copy(g_Config.m_ClAssetExtras, pSelectedItem->m_aName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
			else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			{
				str_copy(g_Config.m_ClAssetCursor, pSelectedItem->m_aName);
				GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			{
				str_copy(g_Config.m_ClAssetArrow, pSelectedItem->m_aName);
				GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
			}
			else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			{
				str_copy(g_Config.m_SndPack, pSelectedItem->m_aName);
				GameClient()->m_Sounds.Clear();
			}
			AssetSelectionChanged = true;
		}
	}
	if(AssetSelectionChanged)
		ConfigManager()->SaveDomain(ConfigDomain::DDNET);

	// Quick search
	QuickSearch.Margin(4.0f, &QuickSearch);
	QuickSearch.VSplitLeft(std::min(260.0f, QuickSearch.w * 0.42f), &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}
	// DoEditBox_Search already owns the input's visual state. Do not add a second
	// outline around the combined search/icon/clear control.

	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		CUIRect ToggleRect;
		DirectoryButton.VSplitLeft(10.0f, nullptr, &DirectoryButton);
		DirectoryButton.VSplitLeft(25.0f, &ToggleRect, &DirectoryButton);
		DirectoryButton.VSplitLeft(5.0f, nullptr, &DirectoryButton);
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoOrganizerIconButton(&s_EntityPreviewToggleId, s_EntityGamePreview ? FontIcon::EYE : FontIcon::IMAGE, &ToggleRect, true, s_EntityGamePreview, 12.0f, vec2(0.0f, -0.5f)))
			s_EntityGamePreview = !s_EntityGamePreview;
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &ToggleRect, Localize("Toggle between game scene preview and raw texture"));
	}

	// Right cluster: Assets directory | gap | Reload
	constexpr float AssetsDirectoryW = 140.0f;
	constexpr float ReloadW = 25.0f;
	constexpr float RightGap = 10.0f;
	const float RightClusterW = AssetsDirectoryW + RightGap + ReloadW;

	CUIRect RightCluster;
	DirectoryButton.VSplitRight(RightClusterW, nullptr, &RightCluster);
	RightCluster.VSplitRight(ReloadW, &RightCluster, &ReloadButton);
	RightCluster.VSplitRight(RightGap, &RightCluster, nullptr);
	RightCluster.VSplitRight(AssetsDirectoryW, &RightCluster, &DirectoryButton);

	static CButtonContainer s_AssetsDirId;
	if(DoOrganizerButton(&s_AssetsDirId, Localize("Assets directory"), &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			str_copy(aBufFull, "assets/cursor");
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			str_copy(aBufFull, "assets/arrow");
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			str_copy(aBufFull, "assets/audio");
		else
			str_copy(aBufFull, "assets");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &DirectoryButton, Localize("Open the directory to add custom assets"));

	static CButtonContainer s_AssetsReloadBtnId;
	if(DoOrganizerIconButton(&s_AssetsReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
	}

	// Keep the control legend in the same localized UI path as the rest of the
	// organizer. It is passive: no hidden input region is placed over cards.
	InputHints.Margin(4.0f, &InputHints);
	InputHints.Draw(NeutralBorderSoft, IGraphics::CORNER_ALL, 4.0f);
	CUIRect InputHintsInner = InputHints;
	InputHintsInner.Margin(1.0f, &InputHintsInner);
	InputHintsInner.Draw(ColorRGBA(0.035f, 0.050f, 0.070f, 0.82f), IGraphics::CORNER_ALL, 3.0f);
	constexpr int NUM_INPUT_HINTS = 5;
	const char *apInputHints[NUM_INPUT_HINTS] = {
		TCLocalize("LMB - select", "AMF Client"),
		TCLocalize("RMB - actions", "AMF Client"),
		TCLocalize("Drag - move", "AMF Client"),
		TCLocalize("Mouse wheel - scroll", "AMF Client"),
		TCLocalize("Esc - back", "AMF Client")};
	CUIRect HintRow = InputHints;
	for(int HintIndex = 0; HintIndex < NUM_INPUT_HINTS; ++HintIndex)
	{
		CUIRect Hint;
		HintRow.VSplitLeft(HintRow.w / (float)(NUM_INPUT_HINTS - HintIndex), &Hint, &HintRow);
		SLabelProperties HintProps;
		HintProps.m_MaxWidth = std::max(0.0f, Hint.w - 6.0f);
		HintProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Hint, apInputHints[HintIndex], 8.5f, TEXTALIGN_MC, HintProps);
	}
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetCursor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetCursor) != 0)
		{
			pThis->GameClient()->LoadCursorAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetArrow(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetArrow) != 0)
		{
			pThis->GameClient()->LoadArrowAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainSndPack(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	char aOldSndPack[64];
	str_copy(aOldSndPack, g_Config.m_SndPack, sizeof(aOldSndPack));
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		if(str_comp(aOldSndPack, g_Config.m_SndPack) != 0)
			pThis->GameClient()->m_Sounds.Clear();
	}
}



/* Settings section: credits */
