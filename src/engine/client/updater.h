#ifndef ENGINE_CLIENT_UPDATER_H
#define ENGINE_CLIENT_UPDATER_H
#include <base/detect.h>
#include <base/lock.h>
#include <base/types.h>

#include <engine/updater.h>

#include <memory>

#define CLIENT_EXEC "DDNet"
#define SERVER_EXEC "DDNet-Server"

#if defined(CONF_FAMILY_WINDOWS)
#define PLAT_EXT ".exe"
#else
#define PLAT_EXT ""
#endif

#define PLAT_CLIENT_EXEC CLIENT_EXEC PLAT_EXT
#define PLAT_SERVER_EXEC SERVER_EXEC PLAT_EXT

class IEngineHttp;
class CHttpRequest;

class CUpdater : public IUpdater
{
	enum class ETaskKind
	{
		NONE,
		FETCH_RELEASE,
		DOWNLOAD_ARCHIVE,
	};

	class IClient *m_pClient;
	class IStorage *m_pStorage;
	IEngineHttp *m_pHttp;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);

	std::shared_ptr<CHttpRequest> m_pCurrentTask;
	ETaskKind m_TaskKind = ETaskKind::NONE;
	bool m_bAutoCheckPending = false;
	bool m_bHasCheckedForUpdate = false;
	bool m_bApplyLaunched = false;
	bool m_bInstallMigrationLaunched = false;
	int64_t m_LastCheckTime = 0;

	char m_aLatestVersion[64];
	char m_aArchiveName[128];
	char m_aArchiveUrl[2048];
	char m_aArchivePath[IO_MAX_PATH_LENGTH];

	void ResetTask() REQUIRES(!m_Lock);
	void StartReleaseFetch() REQUIRES(!m_Lock);
	void ParseReleaseTask() REQUIRES(!m_Lock);
	void StartArchiveDownload() REQUIRES(!m_Lock);
	bool LaunchUpdaterAndQuit() REQUIRES(!m_Lock);
	bool LaunchPortableInstallMigration() REQUIRES(!m_Lock);

	void SetCurrentState(EUpdaterState NewState) REQUIRES(!m_Lock);
	void SetStatus(const char *pStatus) REQUIRES(!m_Lock);
	void SetPercent(int Percent) REQUIRES(!m_Lock);

public:
	CUpdater();

	EUpdaterState GetCurrentState() override REQUIRES(!m_Lock);
	void GetCurrentFile(char *pBuf, int BufSize) override REQUIRES(!m_Lock);
	int GetCurrentPercent() override REQUIRES(!m_Lock);
	const char *GetLatestVersionString() override REQUIRES(!m_Lock);
	bool HasCheckedForUpdate() override REQUIRES(!m_Lock);

	void CheckForUpdate() REQUIRES(!m_Lock) override;
	void InitiateUpdate() REQUIRES(!m_Lock) override;
	void ApplyUpdateAndRestart() REQUIRES(!m_Lock) override;
	void Init();
	void Update() REQUIRES(!m_Lock) override;
};

#endif
