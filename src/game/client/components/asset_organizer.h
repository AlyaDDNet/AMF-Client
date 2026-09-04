#ifndef GAME_CLIENT_COMPONENTS_ASSET_ORGANIZER_H
#define GAME_CLIENT_COMPONENTS_ASSET_ORGANIZER_H

#include <string>
#include <set>
#include <vector>

class IConfigManager;
class IConsole;

namespace AssetOrganizer
{
	// Keep these values in sync with the asset selector tabs. The organizer only
	// stores UI metadata; it never changes the asset files or their load paths.
	enum EAssetType
	{
		ASSET_ENTITIES = 0,
		ASSET_GAME,
		ASSET_EMOTICONS,
		ASSET_PARTICLES,
		ASSET_HUD,
		ASSET_EXTRAS,
		ASSET_CURSOR,
		ASSET_ARROW,
		ASSET_AUDIO,
		NUM_ASSET_TYPES,
	};

	constexpr const char *FOLDER_FAVORITES = "favorites";
	constexpr const char *FOLDER_UNSORTED = "unsorted";

	struct SFolder
	{
		std::string m_Id;
		std::string m_Name;
		int m_PageCount = 1;
		bool m_System = false;
	};

	void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager);

	// Called only when a selector list was refreshed. Unknown packs are placed
	// in Unsorted; missing packs deliberately keep their saved layout entry.
	bool EnsureItems(int AssetType, const std::vector<std::string> &vAssetIds, std::set<std::string> &LegacyFavorites);

	const std::vector<SFolder> &Folders(int AssetType);
	const char *CurrentFolder(int AssetType);
	void SetCurrentFolder(int AssetType, const char *pFolderId);
	int CurrentPage(int AssetType);
	void SetCurrentPage(int AssetType, int Page);
	int PageCount(int AssetType, const char *pFolderId);
	bool IsFavoritesFolder(const char *pFolderId);
	bool IsSystemFolder(int AssetType, const char *pFolderId);
	const char *FolderName(int AssetType, const char *pFolderId);

	bool ItemIsVisible(int AssetType, const char *pAssetId, const char *pFolderId, int Page);
	int ItemSlot(int AssetType, const char *pAssetId);
	const char *ItemFolder(int AssetType, const char *pAssetId);
	int ItemPage(int AssetType, const char *pAssetId);
	int ItemCount(int AssetType, const char *pFolderId, int Page);
	int FolderItemCount(int AssetType, const char *pFolderId);
	bool IsFavorite(int AssetType, const char *pAssetId);
	void SetFavorite(int AssetType, const char *pAssetId, bool Favorite);

	bool CreateFolder(int AssetType, const char *pName, std::string *pCreatedId = nullptr);
	bool RenameFolder(int AssetType, const char *pFolderId, const char *pName);
	bool DeleteFolder(int AssetType, const char *pFolderId);
	bool MoveFolderTo(int AssetType, const char *pFolderId, int TargetIndex);
	bool AddPage(int AssetType, const char *pFolderId);
	bool DeletePage(int AssetType, const char *pFolderId, int Page);
	bool MoveItem(int AssetType, const char *pAssetId, const char *pTargetFolderId, int TargetPage, int TargetPosition);
	bool SwapItems(int AssetType, const char *pFirstAssetId, const char *pSecondAssetId);

	void Save();
} // namespace AssetOrganizer

#endif
