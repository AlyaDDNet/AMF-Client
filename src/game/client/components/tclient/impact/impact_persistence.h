#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PERSISTENCE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_PERSISTENCE_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include "impact_types.h"

#include <vector>

class IStorage;

namespace AmfImpact
{
	class CPersistence
	{
		static constexpr const char *HISTORY_FILE = "amf_impact/history.json";
		static constexpr const char *HISTORY_TEMP_FILE = "amf_impact/history.json.tmp";
		static constexpr const char *HISTORY_BACKUP_FILE = "amf_impact/history.json.bak";
		static constexpr const char *ACTIVE_FILE = "amf_impact/active.json";
		static constexpr const char *ACTIVE_TEMP_FILE = "amf_impact/active.json.tmp";
		static constexpr const char *ACTIVE_BACKUP_FILE = "amf_impact/active.json.bak";

		static bool WriteAtomic(IStorage *pStorage, const char *pFile, const char *pTempFile, const char *pBackupFile, const std::string &Data);
		static bool ReadJson(IStorage *pStorage, const char *pFile, bool (*pReader)(const void *, void *), void *pUser);
		static bool ReadHistoryDocument(const void *pJson, void *pUser);
		static bool ReadActiveDocument(const void *pJson, void *pUser);

	public:
		static bool SaveHistory(IStorage *pStorage, const std::vector<SRunSnapshot> &vHistory, bool HistoryLoadSucceeded = true);
		static bool LoadHistory(IStorage *pStorage, std::vector<SRunSnapshot> *pHistory);
		static bool SaveActive(IStorage *pStorage, const SRunSnapshot &SnapshotData, uint64_t NextRunId, uint64_t NextEventId);
		static bool LoadActive(IStorage *pStorage, SRunSnapshot *pSnapshot, uint64_t *pNextRunId, uint64_t *pNextEventId);
		static void RemoveActive(IStorage *pStorage);
	};
}

#endif
