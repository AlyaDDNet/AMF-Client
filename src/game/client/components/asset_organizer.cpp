#include "asset_organizer.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <array>
#include <map>

namespace AssetOrganizer
{
	namespace
	{
		struct SItem
		{
			std::string m_FolderId = FOLDER_UNSORTED;
			int m_Page = 0;
			int m_Slot = 0;
			bool m_Favorite = false;
		};

		struct SLibrary
		{
			std::vector<SFolder> m_vFolders;
			std::map<std::string, SItem> m_Items;
			// The folder pane asks for all counts every frame while the organizer is
			// open. Keep those aggregate values lazy-cached instead of walking every
			// saved item once per visible folder.
			std::map<std::string, int> m_FolderItemCounts;
			int m_FavoriteItemCount = 0;
			bool m_ItemCountCacheDirty = true;
			std::string m_CurrentFolderId = FOLDER_UNSORTED;
			int m_CurrentPage = 0;
		};

		std::array<SLibrary, NUM_ASSET_TYPES> gs_aLibraries;
		IConfigManager *gs_pConfigManager = nullptr;
		bool gs_ConsoleRegistered = false;
		bool gs_ConfigCallbackRegistered = false;

		constexpr std::array<const char *, NUM_ASSET_TYPES> gs_apAssetTypeNames = {
			"entities", "game", "emoticons", "particles", "hud", "extras", "cursor", "arrow", "audio"};

		bool IsValidAssetType(int AssetType)
		{
			return AssetType >= 0 && AssetType < NUM_ASSET_TYPES;
		}

		int AssetTypeFromString(const char *pName)
		{
			for(int Type = 0; Type < NUM_ASSET_TYPES; ++Type)
			{
				if(str_comp_nocase(pName, gs_apAssetTypeNames[Type]) == 0)
					return Type;
			}
			return -1;
		}

		void EnsureLibrary(SLibrary &Library)
		{
			if(!Library.m_vFolders.empty())
				return;
			Library.m_vFolders.push_back({FOLDER_UNSORTED, "Unsorted", 1, true});
		}

		void InvalidateItemCountCache(SLibrary &Library)
		{
			Library.m_ItemCountCacheDirty = true;
		}

		void RebuildItemCountCache(SLibrary &Library)
		{
			if(!Library.m_ItemCountCacheDirty)
				return;
			Library.m_FolderItemCounts.clear();
			Library.m_FavoriteItemCount = 0;
			for(const auto &[Id, Item] : Library.m_Items)
			{
				++Library.m_FolderItemCounts[Item.m_FolderId];
				if(Item.m_Favorite)
					++Library.m_FavoriteItemCount;
			}
			Library.m_ItemCountCacheDirty = false;
		}

		SFolder *FindFolder(SLibrary &Library, const char *pFolderId)
		{
			EnsureLibrary(Library);
			for(auto &Folder : Library.m_vFolders)
			{
				if(Folder.m_Id == pFolderId)
					return &Folder;
			}
			return nullptr;
		}

		const SFolder *FindFolder(const SLibrary &Library, const char *pFolderId)
		{
			for(const auto &Folder : Library.m_vFolders)
			{
				if(Folder.m_Id == pFolderId)
					return &Folder;
			}
			return nullptr;
		}

		bool IsRealFolder(const SLibrary &Library, const char *pFolderId)
		{
			const SFolder *pFolder = FindFolder(Library, pFolderId);
			return pFolder != nullptr;
		}

		void NormalizeCurrentSelection(SLibrary &Library)
		{
			EnsureLibrary(Library);
			if(Library.m_CurrentFolderId != FOLDER_FAVORITES && !IsRealFolder(Library, Library.m_CurrentFolderId.c_str()))
				Library.m_CurrentFolderId = FOLDER_UNSORTED;
			const int Pages = Library.m_CurrentFolderId == FOLDER_FAVORITES ? 1 : std::max(1, FindFolder(Library, Library.m_CurrentFolderId.c_str())->m_PageCount);
			Library.m_CurrentPage = std::clamp(Library.m_CurrentPage, 0, Pages - 1);
		}

		std::vector<std::string> OrderedItems(const SLibrary &Library, const char *pFolderId, int Page)
		{
			std::vector<std::string> vResult;
			for(const auto &[Id, Item] : Library.m_Items)
			{
				if(Item.m_FolderId == pFolderId && Item.m_Page == Page)
					vResult.push_back(Id);
			}
			std::sort(vResult.begin(), vResult.end(), [&Library](const std::string &Left, const std::string &Right) {
				const SItem &LeftItem = Library.m_Items.at(Left);
				const SItem &RightItem = Library.m_Items.at(Right);
				if(LeftItem.m_Slot != RightItem.m_Slot)
					return LeftItem.m_Slot < RightItem.m_Slot;
				return Left < Right;
			});
			return vResult;
		}

		void WriteItemOrder(SLibrary &Library, const char *pFolderId, int Page, const std::vector<std::string> &vItems)
		{
			for(size_t Index = 0; Index < vItems.size(); ++Index)
			{
				SItem &Item = Library.m_Items[vItems[Index]];
				Item.m_FolderId = pFolderId;
				Item.m_Page = Page;
				Item.m_Slot = Index;
			}
		}

		int NextSlot(const SLibrary &Library, const char *pFolderId, int Page)
		{
			int MaxSlot = -1;
			for(const auto &[Id, Item] : Library.m_Items)
			{
				if(Item.m_FolderId == pFolderId && Item.m_Page == Page)
					MaxSlot = std::max(MaxSlot, Item.m_Slot);
			}
			return MaxSlot + 1;
		}

		SItem &FindOrCreateItem(SLibrary &Library, const char *pAssetId, bool *pCreated = nullptr)
		{
			auto It = Library.m_Items.find(pAssetId);
			if(It != Library.m_Items.end())
			{
				if(pCreated)
					*pCreated = false;
				return It->second;
			}

			SItem Item;
			Item.m_Slot = NextSlot(Library, FOLDER_UNSORTED, 0);
			auto [NewIt, Inserted] = Library.m_Items.emplace(pAssetId, std::move(Item));
			if(Inserted)
				InvalidateItemCountCache(Library);
			if(pCreated)
				*pCreated = Inserted;
			return NewIt->second;
		}

		void EscapeAppend(char *&pDst, const char *pText, const char *pEnd)
		{
			str_append(pDst, "\"", (int)(pEnd - pDst + 1));
			pDst += str_length(pDst);
			str_escape(&pDst, pText, pEnd);
			str_append(pDst, "\"", (int)(pEnd - pDst + 1));
			pDst += str_length(pDst);
		}

		void WriteFolder(IConfigManager *pConfigManager, int AssetType, const SFolder &Folder, int Order)
		{
			char aLine[512];
			const char *pEnd = aLine + sizeof(aLine) - 2;
			char *pDst = aLine;
			str_copy(aLine, "amf_asset_layout_folder ");
			pDst += str_length(pDst);
			EscapeAppend(pDst, gs_apAssetTypeNames[AssetType], pEnd);
			str_append(pDst, " ", (int)(pEnd - pDst + 1));
			++pDst;
			EscapeAppend(pDst, Folder.m_Id.c_str(), pEnd);
			str_append(pDst, " ", (int)(pEnd - pDst + 1));
			++pDst;
			EscapeAppend(pDst, Folder.m_Name.c_str(), pEnd);
			str_format(pDst, (int)(pEnd - pDst + 1), " %d %d", Order, std::max(1, Folder.m_PageCount));
			pConfigManager->WriteLine(aLine, ConfigDomain::AMFASSETLAYOUT);
		}

		void WriteItem(IConfigManager *pConfigManager, int AssetType, const std::string &Id, const SItem &Item)
		{
			char aLine[512];
			const char *pEnd = aLine + sizeof(aLine) - 2;
			char *pDst = aLine;
			str_copy(aLine, "amf_asset_layout_item ");
			pDst += str_length(pDst);
			EscapeAppend(pDst, gs_apAssetTypeNames[AssetType], pEnd);
			str_append(pDst, " ", (int)(pEnd - pDst + 1));
			++pDst;
			EscapeAppend(pDst, Id.c_str(), pEnd);
			str_append(pDst, " ", (int)(pEnd - pDst + 1));
			++pDst;
			EscapeAppend(pDst, Item.m_FolderId.c_str(), pEnd);
			str_format(pDst, (int)(pEnd - pDst + 1), " %d %d %d", std::max(0, Item.m_Page), std::max(0, Item.m_Slot), Item.m_Favorite ? 1 : 0);
			pConfigManager->WriteLine(aLine, ConfigDomain::AMFASSETLAYOUT);
		}

		void WriteView(IConfigManager *pConfigManager, int AssetType, const SLibrary &Library)
		{
			char aLine[256];
			const char *pEnd = aLine + sizeof(aLine) - 2;
			char *pDst = aLine;
			str_copy(aLine, "amf_asset_layout_view ");
			pDst += str_length(pDst);
			EscapeAppend(pDst, gs_apAssetTypeNames[AssetType], pEnd);
			str_append(pDst, " ", (int)(pEnd - pDst + 1));
			++pDst;
			EscapeAppend(pDst, Library.m_CurrentFolderId.c_str(), pEnd);
			str_format(pDst, (int)(pEnd - pDst + 1), " %d", std::max(0, Library.m_CurrentPage));
			pConfigManager->WriteLine(aLine, ConfigDomain::AMFASSETLAYOUT);
		}

		void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
		{
			(void)pUserData;
			for(int AssetType = 0; AssetType < NUM_ASSET_TYPES; ++AssetType)
			{
				SLibrary &Library = gs_aLibraries[AssetType];
				EnsureLibrary(Library);
				for(size_t Index = 0; Index < Library.m_vFolders.size(); ++Index)
					WriteFolder(pConfigManager, AssetType, Library.m_vFolders[Index], (int)Index - 1);
				for(const auto &[Id, Item] : Library.m_Items)
					WriteItem(pConfigManager, AssetType, Id, Item);
				WriteView(pConfigManager, AssetType, Library);
			}
		}

		void ConLayoutFolder(IConsole::IResult *pResult, void *pUserData)
		{
			(void)pUserData;
			if(pResult->NumArguments() < 5)
				return;
			const int AssetType = AssetTypeFromString(pResult->GetString(0));
			if(!IsValidAssetType(AssetType))
				return;
			const char *pId = pResult->GetString(1);
			if(pId[0] == '\0' || str_comp(pId, FOLDER_FAVORITES) == 0)
				return;

			SLibrary &Library = gs_aLibraries[AssetType];
			EnsureLibrary(Library);
			SFolder *pFolder = FindFolder(Library, pId);
			if(!pFolder)
			{
				Library.m_vFolders.push_back({pId, pResult->GetString(2), std::max(1, pResult->GetInteger(4)), str_comp(pId, FOLDER_UNSORTED) == 0});
				pFolder = &Library.m_vFolders.back();
			}
			else
			{
				pFolder->m_Name = pResult->GetString(2);
				pFolder->m_PageCount = std::max(1, pResult->GetInteger(4));
			}

			const int Order = pResult->GetInteger(3);
			if(!pFolder->m_System)
			{
				auto It = std::find_if(Library.m_vFolders.begin(), Library.m_vFolders.end(), [pFolder](const SFolder &Folder) { return &Folder == pFolder; });
				if(It != Library.m_vFolders.end())
				{
					SFolder Folder = *It;
					Library.m_vFolders.erase(It);
					const int InsertAt = std::clamp(Order + 1, 1, (int)Library.m_vFolders.size());
					Library.m_vFolders.insert(Library.m_vFolders.begin() + InsertAt, std::move(Folder));
				}
			}
		}

		void ConLayoutItem(IConsole::IResult *pResult, void *pUserData)
		{
			(void)pUserData;
			if(pResult->NumArguments() < 6)
				return;
			const int AssetType = AssetTypeFromString(pResult->GetString(0));
			if(!IsValidAssetType(AssetType))
				return;
			const char *pAssetId = pResult->GetString(1);
			const char *pFolderId = pResult->GetString(2);
			if(pAssetId[0] == '\0' || pFolderId[0] == '\0' || str_comp(pFolderId, FOLDER_FAVORITES) == 0)
				return;

			SLibrary &Library = gs_aLibraries[AssetType];
			EnsureLibrary(Library);
			SItem &Item = FindOrCreateItem(Library, pAssetId);
			Item.m_FolderId = IsRealFolder(Library, pFolderId) ? pFolderId : FOLDER_UNSORTED;
			Item.m_Page = std::max(0, pResult->GetInteger(3));
			Item.m_Slot = std::max(0, pResult->GetInteger(4));
			Item.m_Favorite = pResult->GetInteger(5) != 0;
			InvalidateItemCountCache(Library);
			if(SFolder *pFolder = FindFolder(Library, Item.m_FolderId.c_str()))
				pFolder->m_PageCount = std::max(pFolder->m_PageCount, Item.m_Page + 1);
		}

		void ConLayoutView(IConsole::IResult *pResult, void *pUserData)
		{
			(void)pUserData;
			if(pResult->NumArguments() < 3)
				return;
			const int AssetType = AssetTypeFromString(pResult->GetString(0));
			if(!IsValidAssetType(AssetType))
				return;
			SLibrary &Library = gs_aLibraries[AssetType];
			EnsureLibrary(Library);
			const char *pFolderId = pResult->GetString(1);
			Library.m_CurrentFolderId = (IsFavoritesFolder(pFolderId) || IsRealFolder(Library, pFolderId)) ? pFolderId : FOLDER_UNSORTED;
			Library.m_CurrentPage = std::max(0, pResult->GetInteger(2));
			NormalizeCurrentSelection(Library);
		}
	} // namespace

	void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager)
	{
		gs_pConfigManager = pConfigManager;
		if(!gs_ConsoleRegistered && pConsole)
		{
			pConsole->Register("amf_asset_layout_folder", "s[type] s[id] s[name] i[order] i[pages]", CFGFLAG_CLIENT, ConLayoutFolder, nullptr, "AMF asset organizer folder");
			pConsole->Register("amf_asset_layout_item", "s[type] s[asset] s[folder] i[page] i[slot] i[favorite]", CFGFLAG_CLIENT, ConLayoutItem, nullptr, "AMF asset organizer item");
			pConsole->Register("amf_asset_layout_view", "s[type] s[folder] i[page]", CFGFLAG_CLIENT, ConLayoutView, nullptr, "AMF asset organizer current view");
			gs_ConsoleRegistered = true;
		}
		if(!gs_ConfigCallbackRegistered && pConfigManager)
		{
			pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr, ConfigDomain::AMFASSETLAYOUT);
			gs_ConfigCallbackRegistered = true;
		}
	}

	bool EnsureItems(int AssetType, const std::vector<std::string> &vAssetIds, std::set<std::string> &LegacyFavorites)
	{
		if(!IsValidAssetType(AssetType))
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		bool Changed = false;
		int NextUnsortedSlot = -1;
		auto AllocateUnsortedSlot = [&Library, &NextUnsortedSlot]() {
			if(NextUnsortedSlot < 0)
				NextUnsortedSlot = NextSlot(Library, FOLDER_UNSORTED, 0);
			return NextUnsortedSlot++;
		};
		for(const std::string &Id : vAssetIds)
		{
			auto It = Library.m_Items.find(Id);
			if(It == Library.m_Items.end())
			{
				SItem Item;
				Item.m_Slot = AllocateUnsortedSlot();
				It = Library.m_Items.emplace(Id, std::move(Item)).first;
				Changed = true;
			}
			SItem &Item = It->second;
			if(LegacyFavorites.contains(Id) && !Item.m_Favorite)
			{
				Item.m_Favorite = true;
				Changed = true;
			}
			if(Item.m_Favorite)
				LegacyFavorites.insert(Id);
			if(!IsRealFolder(Library, Item.m_FolderId.c_str()))
			{
				Item.m_FolderId = FOLDER_UNSORTED;
				Item.m_Page = 0;
				Item.m_Slot = AllocateUnsortedSlot();
				Changed = true;
			}
		}
		NormalizeCurrentSelection(Library);
		if(Changed)
		{
			InvalidateItemCountCache(Library);
			Save();
		}
		return Changed;
	}

	const std::vector<SFolder> &Folders(int AssetType)
	{
		static const std::vector<SFolder> s_Empty;
		if(!IsValidAssetType(AssetType))
			return s_Empty;
		EnsureLibrary(gs_aLibraries[AssetType]);
		return gs_aLibraries[AssetType].m_vFolders;
	}

	const char *CurrentFolder(int AssetType)
	{
		if(!IsValidAssetType(AssetType))
			return FOLDER_UNSORTED;
		NormalizeCurrentSelection(gs_aLibraries[AssetType]);
		return gs_aLibraries[AssetType].m_CurrentFolderId.c_str();
	}

	void SetCurrentFolder(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr)
			return;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		const std::string NewFolderId = (str_comp(pFolderId, FOLDER_FAVORITES) == 0 || IsRealFolder(Library, pFolderId)) ? pFolderId : FOLDER_UNSORTED;
		if(Library.m_CurrentFolderId == NewFolderId && Library.m_CurrentPage == 0)
			return;
		Library.m_CurrentFolderId = NewFolderId;
		Library.m_CurrentPage = 0;
		Save();
	}

	int CurrentPage(int AssetType)
	{
		if(!IsValidAssetType(AssetType))
			return 0;
		NormalizeCurrentSelection(gs_aLibraries[AssetType]);
		return gs_aLibraries[AssetType].m_CurrentPage;
	}

	void SetCurrentPage(int AssetType, int Page)
	{
		if(!IsValidAssetType(AssetType))
			return;
		SLibrary &Library = gs_aLibraries[AssetType];
		NormalizeCurrentSelection(Library);
		const int Pages = PageCount(AssetType, Library.m_CurrentFolderId.c_str());
		const int NewPage = std::clamp(Page, 0, Pages - 1);
		if(Library.m_CurrentPage == NewPage)
			return;
		Library.m_CurrentPage = NewPage;
		Save();
	}

	int PageCount(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || IsFavoritesFolder(pFolderId))
			return 1;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		const SFolder *pFolder = FindFolder(Library, pFolderId);
		return pFolder ? std::max(1, pFolder->m_PageCount) : 1;
	}

	bool IsFavoritesFolder(const char *pFolderId)
	{
		return pFolderId != nullptr && str_comp(pFolderId, FOLDER_FAVORITES) == 0;
	}

	bool IsSystemFolder(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || IsFavoritesFolder(pFolderId))
			return true;
		const SFolder *pFolder = FindFolder(gs_aLibraries[AssetType], pFolderId);
		return pFolder == nullptr || pFolder->m_System;
	}

	const char *FolderName(int AssetType, const char *pFolderId)
	{
		if(IsFavoritesFolder(pFolderId))
			return "Favorites";
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr)
			return "Unsorted";
		const SFolder *pFolder = FindFolder(gs_aLibraries[AssetType], pFolderId);
		return pFolder ? pFolder->m_Name.c_str() : "Unsorted";
	}

	bool ItemIsVisible(int AssetType, const char *pAssetId, const char *pFolderId, int Page)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr || pFolderId == nullptr)
			return false;
		const SLibrary &Library = gs_aLibraries[AssetType];
		const auto It = Library.m_Items.find(pAssetId);
		if(It == Library.m_Items.end())
			return false;
		if(IsFavoritesFolder(pFolderId))
			return It->second.m_Favorite;
		return It->second.m_FolderId == pFolderId && It->second.m_Page == Page;
	}

	int ItemSlot(int AssetType, const char *pAssetId)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr)
			return 0;
		const auto It = gs_aLibraries[AssetType].m_Items.find(pAssetId);
		return It == gs_aLibraries[AssetType].m_Items.end() ? 0 : It->second.m_Slot;
	}

	const char *ItemFolder(int AssetType, const char *pAssetId)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr)
			return FOLDER_UNSORTED;
		const auto It = gs_aLibraries[AssetType].m_Items.find(pAssetId);
		return It == gs_aLibraries[AssetType].m_Items.end() ? FOLDER_UNSORTED : It->second.m_FolderId.c_str();
	}

	int ItemPage(int AssetType, const char *pAssetId)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr)
			return 0;
		const auto It = gs_aLibraries[AssetType].m_Items.find(pAssetId);
		return It == gs_aLibraries[AssetType].m_Items.end() ? 0 : It->second.m_Page;
	}

	int ItemCount(int AssetType, const char *pFolderId, int Page)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || IsFavoritesFolder(pFolderId))
			return 0;
		const SLibrary &Library = gs_aLibraries[AssetType];
		int Count = 0;
		for(const auto &[Id, Item] : Library.m_Items)
		{
			if(Item.m_FolderId == pFolderId && Item.m_Page == Page)
				++Count;
		}
		return Count;
	}

	int FolderItemCount(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr)
			return 0;
		SLibrary &Library = gs_aLibraries[AssetType];
		RebuildItemCountCache(Library);
		if(IsFavoritesFolder(pFolderId))
			return Library.m_FavoriteItemCount;
		const auto It = Library.m_FolderItemCounts.find(pFolderId);
		return It == Library.m_FolderItemCounts.end() ? 0 : It->second;
	}

	bool IsFavorite(int AssetType, const char *pAssetId)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr)
			return false;
		const auto It = gs_aLibraries[AssetType].m_Items.find(pAssetId);
		return It != gs_aLibraries[AssetType].m_Items.end() && It->second.m_Favorite;
	}

	void SetFavorite(int AssetType, const char *pAssetId, bool Favorite)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr || pAssetId[0] == '\0')
			return;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		SItem &Item = FindOrCreateItem(Library, pAssetId);
		if(Item.m_Favorite == Favorite)
			return;
		Item.m_Favorite = Favorite;
		InvalidateItemCountCache(Library);
		Save();
	}

	bool CreateFolder(int AssetType, const char *pName, std::string *pCreatedId)
	{
		if(!IsValidAssetType(AssetType) || pName == nullptr || pName[0] == '\0')
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		for(const SFolder &Folder : Library.m_vFolders)
		{
			if(str_comp_nocase(Folder.m_Name.c_str(), pName) == 0)
				return false;
		}

		int Serial = 1;
		std::string Id;
		do
		{
			Id = "folder_" + std::to_string(Serial++);
		} while(FindFolder(Library, Id.c_str()) != nullptr);
		Library.m_vFolders.push_back({Id, pName, 1, false});
		if(pCreatedId)
			*pCreatedId = Id;
		Save();
		return true;
	}

	bool RenameFolder(int AssetType, const char *pFolderId, const char *pName)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || pName == nullptr || pName[0] == '\0')
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		SFolder *pFolder = FindFolder(Library, pFolderId);
		if(!pFolder || pFolder->m_System)
			return false;
		for(const SFolder &Folder : Library.m_vFolders)
		{
			if(Folder.m_Id != pFolderId && str_comp_nocase(Folder.m_Name.c_str(), pName) == 0)
				return false;
		}
		if(pFolder->m_Name == pName)
			return false;
		pFolder->m_Name = pName;
		Save();
		return true;
	}

	bool DeleteFolder(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr)
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		SFolder *pFolder = FindFolder(Library, pFolderId);
		if(!pFolder || pFolder->m_System)
			return false;
		int NextUnsortedSlot = NextSlot(Library, FOLDER_UNSORTED, 0);
		for(auto &[Id, Item] : Library.m_Items)
		{
			if(Item.m_FolderId == pFolderId)
			{
				Item.m_FolderId = FOLDER_UNSORTED;
				Item.m_Page = 0;
				Item.m_Slot = NextUnsortedSlot++;
			}
		}
		Library.m_vFolders.erase(std::remove_if(Library.m_vFolders.begin(), Library.m_vFolders.end(), [pFolderId](const SFolder &Folder) { return Folder.m_Id == pFolderId; }), Library.m_vFolders.end());
		NormalizeCurrentSelection(Library);
		InvalidateItemCountCache(Library);
		Save();
		return true;
	}

	bool MoveFolderTo(int AssetType, const char *pFolderId, int TargetIndex)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr)
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		auto It = std::find_if(Library.m_vFolders.begin(), Library.m_vFolders.end(), [pFolderId](const SFolder &Folder) { return Folder.m_Id == pFolderId; });
		if(It == Library.m_vFolders.end() || It->m_System)
			return false;
		const int Index = It - Library.m_vFolders.begin();
		const int Target = std::clamp(TargetIndex, 1, (int)Library.m_vFolders.size() - 1);
		if(Target == Index)
			return false;
		SFolder Folder = std::move(*It);
		Library.m_vFolders.erase(It);
		Library.m_vFolders.insert(Library.m_vFolders.begin() + Target, std::move(Folder));
		Save();
		return true;
	}

	bool AddPage(int AssetType, const char *pFolderId)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || IsFavoritesFolder(pFolderId))
			return false;
		SFolder *pFolder = FindFolder(gs_aLibraries[AssetType], pFolderId);
		if(!pFolder)
			return false;
		++pFolder->m_PageCount;
		Save();
		return true;
	}

	bool DeletePage(int AssetType, const char *pFolderId, int Page)
	{
		if(!IsValidAssetType(AssetType) || pFolderId == nullptr || IsFavoritesFolder(pFolderId))
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		SFolder *pFolder = FindFolder(Library, pFolderId);
		if(!pFolder || pFolder->m_PageCount <= 1 || Page < 0 || Page >= pFolder->m_PageCount)
			return false;

		// Never lose assets when removing a page. Its ordered contents are appended
		// to the closest surviving page and all following page numbers are closed.
		std::vector<std::string> vRemoved = OrderedItems(Library, pFolderId, Page);
		const int TargetPage = Page > 0 ? Page - 1 : 0;
		if(Page > 0)
		{
			std::vector<std::string> vTarget = OrderedItems(Library, pFolderId, TargetPage);
			vTarget.insert(vTarget.end(), vRemoved.begin(), vRemoved.end());
			WriteItemOrder(Library, pFolderId, TargetPage, vTarget);
		}
		else
		{
			// Page 1 becomes the new first page. Put the removed first-page items
			// before its contents and write them after page numbers are shifted.
			std::vector<std::string> vTarget = std::move(vRemoved);
			std::vector<std::string> vNext = OrderedItems(Library, pFolderId, 1);
			vTarget.insert(vTarget.end(), vNext.begin(), vNext.end());
			for(auto &[Id, Item] : Library.m_Items)
			{
				if(Item.m_FolderId == pFolderId && Item.m_Page > 0)
					--Item.m_Page;
			}
			WriteItemOrder(Library, pFolderId, 0, vTarget);
		}
		if(Page > 0)
		{
			for(auto &[Id, Item] : Library.m_Items)
			{
				if(Item.m_FolderId == pFolderId && Item.m_Page > Page)
					--Item.m_Page;
			}
		}
		--pFolder->m_PageCount;
		Library.m_CurrentPage = std::clamp(TargetPage, 0, pFolder->m_PageCount - 1);
		Save();
		return true;
	}

	bool MoveItem(int AssetType, const char *pAssetId, const char *pTargetFolderId, int TargetPage, int TargetPosition)
	{
		if(!IsValidAssetType(AssetType) || pAssetId == nullptr || pTargetFolderId == nullptr || pAssetId[0] == '\0' || IsFavoritesFolder(pTargetFolderId))
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		EnsureLibrary(Library);
		if(!IsRealFolder(Library, pTargetFolderId))
			return false;
		SItem &Item = FindOrCreateItem(Library, pAssetId);
		SFolder *pTargetFolder = FindFolder(Library, pTargetFolderId);
		TargetPage = std::max(0, TargetPage);
		pTargetFolder->m_PageCount = std::max(pTargetFolder->m_PageCount, TargetPage + 1);

		const std::string SourceFolder = Item.m_FolderId;
		const int SourcePage = Item.m_Page;
		std::vector<std::string> vSource = OrderedItems(Library, SourceFolder.c_str(), SourcePage);
		vSource.erase(std::remove(vSource.begin(), vSource.end(), pAssetId), vSource.end());
		WriteItemOrder(Library, SourceFolder.c_str(), SourcePage, vSource);

		std::vector<std::string> vTarget;
		if(SourceFolder == pTargetFolderId && SourcePage == TargetPage)
			vTarget = std::move(vSource);
		else
			vTarget = OrderedItems(Library, pTargetFolderId, TargetPage);
		const int InsertAt = std::clamp(TargetPosition, 0, (int)vTarget.size());
		vTarget.insert(vTarget.begin() + InsertAt, pAssetId);
		WriteItemOrder(Library, pTargetFolderId, TargetPage, vTarget);
		InvalidateItemCountCache(Library);
		Save();
		return true;
	}

	bool SwapItems(int AssetType, const char *pFirstAssetId, const char *pSecondAssetId)
	{
		if(!IsValidAssetType(AssetType) || pFirstAssetId == nullptr || pSecondAssetId == nullptr || str_comp(pFirstAssetId, pSecondAssetId) == 0)
			return false;
		SLibrary &Library = gs_aLibraries[AssetType];
		auto First = Library.m_Items.find(pFirstAssetId);
		auto Second = Library.m_Items.find(pSecondAssetId);
		if(First == Library.m_Items.end() || Second == Library.m_Items.end())
			return false;
		std::swap(First->second.m_FolderId, Second->second.m_FolderId);
		std::swap(First->second.m_Page, Second->second.m_Page);
		std::swap(First->second.m_Slot, Second->second.m_Slot);
		Save();
		return true;
	}

	void Save()
	{
		if(gs_pConfigManager && !gs_pConfigManager->SaveDomain(ConfigDomain::AMFASSETLAYOUT))
			log_warn("amf-assets", "failed to save amf_asset_layout.cfg");
	}
} // namespace AssetOrganizer
