#include "updater.h"

#include <base/math.h>
#include <base/process.h>
#include <base/fs.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/version.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

namespace
{
constexpr const char *PRODUCTION_RELEASES_URL = "https://api.github.com/repos/AlyaDDNet/AMF-Client/releases?per_page=10";
constexpr const char *UPDATE_ARCHIVE_PATH = "update/amfclient-release.zip";
constexpr const char *DISTRIBUTION_MANIFEST_PATH = "amf_distribution_manifest.txt";
constexpr const char *INSTALL_MIGRATOR_PATH = "amfclient-install-helper.exe";
constexpr int64_t CHECK_COOLDOWN_SECONDS = 4;

void BuildReleasesUrl(char *pBuf, int BufSize)
{
	const char *pSource = PRODUCTION_RELEASES_URL;
	str_format(pBuf, BufSize, "%s%ct=%lld", pSource, str_find(pSource, "?") ? '&' : '?', (long long)time_timestamp());
}

bool StrEndsWithNoCase(const char *pStr, const char *pSuffix)
{
	if(!pStr || !pSuffix)
		return false;
	const int StrLen = str_length(pStr);
	const int SuffixLen = str_length(pSuffix);
	return SuffixLen <= StrLen && str_comp_nocase(pStr + StrLen - SuffixLen, pSuffix) == 0;
}

std::string ToLowerAscii(const char *pStr)
{
	std::string Lower;
	if(!pStr)
		return Lower;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pStr); *p != '\0'; ++p)
		Lower.push_back(static_cast<char>(std::tolower(*p)));
	return Lower;
}

void NormalizeVersionString(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;
	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}
	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;
	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;
	str_copy(pBuf, pVersion, BufSize);
}

std::vector<int> ExtractVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current > 100000000 ? 1000000000 : Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}
	if(Current >= 0)
		vNumbers.push_back(Current);
	return vNumbers;
}

// Human-readable decorations (edition labels, "-beta") never decide the
// release order. AMF updates use the structured numeric components only.
int CompareVersionStrings(const char *pLeft, const char *pRight)
{
	char aLeftNormalized[64];
	char aRightNormalized[64];
	NormalizeVersionString(pLeft, aLeftNormalized, sizeof(aLeftNormalized));
	NormalizeVersionString(pRight, aRightNormalized, sizeof(aRightNormalized));
	const std::vector<int> vLeft = ExtractVersionNumbers(aLeftNormalized);
	const std::vector<int> vRight = ExtractVersionNumbers(aRightNormalized);
	if(!vLeft.empty() || !vRight.empty())
	{
		const size_t Num = std::max(vLeft.size(), vRight.size());
		for(size_t i = 0; i < Num; ++i)
		{
			const int Left = i < vLeft.size() ? vLeft[i] : 0;
			const int Right = i < vRight.size() ? vRight[i] : 0;
			if(Left < Right)
				return -1;
			if(Left > Right)
				return 1;
		}
		return 0;
	}
	return str_comp_nocase(aLeftNormalized, aRightNormalized);
}

int ScoreArchiveAsset(const char *pAssetName)
{
	if(!pAssetName)
		return -1;
	const std::string Lower = ToLowerAscii(pAssetName);
	if(Lower.find("amfclient") == std::string::npos || Lower.find("portable") == std::string::npos)
		return -1;
#if defined(CONF_FAMILY_WINDOWS)
	if(!StrEndsWithNoCase(pAssetName, ".zip"))
		return -1;
#else
	return -1;
#endif
	if(Lower.find("debug") != std::string::npos || Lower.find("symbols") != std::string::npos || Lower.find("source") != std::string::npos)
		return -1;

	int Score = 100;
	if(Lower.find("x64") != std::string::npos || Lower.find("amd64") != std::string::npos || Lower.find("win64") != std::string::npos)
		Score += 20;
	return Score;
}

bool ParseReleaseObject(const json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson || pJson->type != json_object || json_boolean_get(json_object_get(pJson, "draft")))
		return false;
	const char *pReleaseVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pReleaseVersion)
		pReleaseVersion = json_string_get(json_object_get(pJson, "name"));
	const json_value *pAssets = json_object_get(pJson, "assets");
	if(!pReleaseVersion || !pAssets || pAssets->type != json_array)
		return false;

	int BestScore = -1;
	char aBestName[128] = "";
	char aBestUrl[2048] = "";
	for(int i = 0; i < json_array_length(pAssets); ++i)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		if(!pAsset || pAsset->type != json_object)
			continue;
		const char *pName = json_string_get(json_object_get(pAsset, "name"));
		const char *pUrl = json_string_get(json_object_get(pAsset, "browser_download_url"));
		const int Score = ScoreArchiveAsset(pName);
		if(!pName || !pUrl || Score < BestScore)
			continue;
		BestScore = Score;
		str_copy(aBestName, pName, sizeof(aBestName));
		str_copy(aBestUrl, pUrl, sizeof(aBestUrl));
	}
	if(BestScore < 0)
		return false;
	NormalizeVersionString(pReleaseVersion, pVersion, VersionSize);
	str_copy(pArchiveName, aBestName, ArchiveNameSize);
	str_copy(pArchiveUrl, aBestUrl, ArchiveUrlSize);
	return true;
}

bool ParseLatestRelease(json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson)
		return false;
	if(pJson->type == json_object)
		return ParseReleaseObject(pJson, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize);
	if(pJson->type != json_array)
		return false;

	bool Found = false;
	char aBestVersion[64] = "";
	char aBestArchiveName[128] = "";
	char aBestArchiveUrl[2048] = "";
	for(int i = 0; i < json_array_length(pJson); ++i)
	{
		char aCandidateVersion[64] = "";
		char aCandidateArchiveName[128] = "";
		char aCandidateArchiveUrl[2048] = "";
		if(!ParseReleaseObject(json_array_get(pJson, i), aCandidateVersion, sizeof(aCandidateVersion), aCandidateArchiveName, sizeof(aCandidateArchiveName), aCandidateArchiveUrl, sizeof(aCandidateArchiveUrl)))
			continue;
		if(!Found || CompareVersionStrings(aCandidateVersion, aBestVersion) > 0)
		{
			Found = true;
			str_copy(aBestVersion, aCandidateVersion, sizeof(aBestVersion));
			str_copy(aBestArchiveName, aCandidateArchiveName, sizeof(aBestArchiveName));
			str_copy(aBestArchiveUrl, aCandidateArchiveUrl, sizeof(aBestArchiveUrl));
		}
	}
	if(!Found)
		return false;
	str_copy(pVersion, aBestVersion, VersionSize);
	str_copy(pArchiveName, aBestArchiveName, ArchiveNameSize);
	str_copy(pArchiveUrl, aBestArchiveUrl, ArchiveUrlSize);
	return true;
}

void StripFilename(char *pPath)
{
	for(int i = str_length(pPath) - 1; i >= 0; --i)
	{
		if(pPath[i] == '/' || pPath[i] == '\\')
		{
			pPath[i] = '\0';
			return;
		}
	}
	pPath[0] = '\0';
}

const char *Filename(const char *pPath)
{
	const char *pFilename = pPath;
	for(const char *pCurrent = pPath; pCurrent && *pCurrent; ++pCurrent)
	{
		if(*pCurrent == '/' || *pCurrent == '\\')
			pFilename = pCurrent + 1;
	}
	return pFilename;
}

bool IsLegacyPortableDirectory(const char *pDirectory)
{
	const char *pName = Filename(pDirectory);
	const int NameLength = str_length(pName);
	constexpr const char *pPrefix = "AMFClient-";
	constexpr const char *pSuffix = "-Portable";
	return NameLength > str_length(pPrefix) + str_length(pSuffix) &&
		str_comp_nocase_num(pName, pPrefix, str_length(pPrefix)) == 0 &&
		StrEndsWithNoCase(pName, pSuffix);
}
} // namespace

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;
	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
}

void CUpdater::Init()
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = Kernel()->RequestInterface<IEngineHttp>();
#if !defined(CONF_HEADLESS_CLIENT) && defined(CONF_FAMILY_WINDOWS)
	if(LaunchPortableInstallMigration())
		return;
	m_bAutoCheckPending = true;
#endif
}

bool CUpdater::LaunchPortableInstallMigration()
{
#if !defined(CONF_FAMILY_WINDOWS)
	return false;
#else
	if(m_bInstallMigrationLaunched || !m_pClient || !m_pStorage)
		return false;

	char aManifestPath[IO_MAX_PATH_LENGTH];
	char aMigratorPath[IO_MAX_PATH_LENGTH];
	char aClientPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPathAbsolute(DISTRIBUTION_MANIFEST_PATH, aManifestPath, sizeof(aManifestPath));
	m_pStorage->GetBinaryPathAbsolute(INSTALL_MIGRATOR_PATH, aMigratorPath, sizeof(aMigratorPath));
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aClientPath, sizeof(aClientPath));
	if(!m_pStorage->FileExists(aManifestPath, IStorage::TYPE_ABSOLUTE) || !m_pStorage->FileExists(aMigratorPath, IStorage::TYPE_ABSOLUTE) || !m_pStorage->FileExists(aClientPath, IStorage::TYPE_ABSOLUTE))
		return false;

	char aInstallDirectory[IO_MAX_PATH_LENGTH];
	str_copy(aInstallDirectory, aClientPath, sizeof(aInstallDirectory));
	StripFilename(aInstallDirectory);
	if(!IsLegacyPortableDirectory(aInstallDirectory))
		return false;

	char aParentDirectory[IO_MAX_PATH_LENGTH];
	str_copy(aParentDirectory, aInstallDirectory, sizeof(aParentDirectory));
	StripFilename(aParentDirectory);
	char aTargetDirectory[IO_MAX_PATH_LENGTH];
	str_format(aTargetDirectory, sizeof(aTargetDirectory), "%s/%s", aParentDirectory, "AMFClient-" AMF_CLIENT_VERSION "-Ascension-Portable");
	if(str_comp_nocase(aInstallDirectory, aTargetDirectory) == 0 || m_pStorage->FolderExists(aTargetDirectory, IStorage::TYPE_ABSOLUTE))
		return false;

	char aTargetClientPath[IO_MAX_PATH_LENGTH];
	str_format(aTargetClientPath, sizeof(aTargetClientPath), "%s/%s", aTargetDirectory, PLAT_CLIENT_EXEC);
	char aPid[32];
	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {"--migrate-install", aPid, aInstallDirectory, aTargetDirectory, aTargetClientPath};
	if(process_execute(aMigratorPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
		return false;

	m_bInstallMigrationLaunched = true;
	m_pClient->Quit();
	return true;
#endif
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	const CLockScope LockScope(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope LockScope(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

const char *CUpdater::GetLatestVersionString()
{
	return m_aLatestVersion;
}

bool CUpdater::HasCheckedForUpdate()
{
	return m_bHasCheckedForUpdate;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	SetStatus("Checking latest AMF Client release");
	SetPercent(0);
	SetCurrentState(IUpdater::GETTING_MANIFEST);

	char aUrl[2304];
	BuildReleasesUrl(aUrl, sizeof(aUrl));
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = HttpGet(aUrl);
	m_pCurrentTask->HeaderString("Accept", "application/vnd.github+json");
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->HeaderString("X-GitHub-Api-Version", "2022-11-28");
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->MaxResponseSize(1024 * 1024);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

void CUpdater::ParseReleaseTask()
{
	json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
	char aVersion[64] = "";
	char aArchiveName[128] = "";
	char aArchiveUrl[2048] = "";
	const bool Parsed = pJson && ParseLatestRelease(pJson, aVersion, sizeof(aVersion), aArchiveName, sizeof(aArchiveName), aArchiveUrl, sizeof(aArchiveUrl));
	if(pJson)
		json_value_free(pJson);

	m_bHasCheckedForUpdate = true;
	if(!Parsed)
	{
		SetStatus("No compatible AMF Client release was found");
		SetCurrentState(IUpdater::FAIL);
		return;
	}
	if(CompareVersionStrings(aVersion, CLIENT_RELEASE_VERSION) <= 0)
	{
		m_aLatestVersion[0] = '\0';
		m_aArchiveName[0] = '\0';
		m_aArchiveUrl[0] = '\0';
		SetStatus("AMF Client is up to date");
		SetCurrentState(IUpdater::CLEAN);
		return;
	}
	str_copy(m_aLatestVersion, aVersion, sizeof(m_aLatestVersion));
	str_copy(m_aArchiveName, aArchiveName, sizeof(m_aArchiveName));
	str_copy(m_aArchiveUrl, aArchiveUrl, sizeof(m_aArchiveUrl));
	SetStatus("AMF Client update available");
	SetCurrentState(IUpdater::VERSION_AVAILABLE);
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
	m_bApplyLaunched = false;
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->RemoveBinaryFile(m_aArchivePath);
	SetStatus(m_aArchiveName);
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);
	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_ABSOLUTE);
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->MaxResponseSize(512ll * 1024ll * 1024ll);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::LaunchUpdaterAndQuit()
{
#if defined(CONF_FAMILY_WINDOWS)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aUpdaterPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];
	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded update archive is missing");
		return false;
	}
	m_pStorage->GetBinaryPathAbsolute("amfclient-updater.exe", aUpdaterPath, sizeof(aUpdaterPath));
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	if(!m_pStorage->FileExists(aUpdaterPath, IStorage::TYPE_ABSOLUTE) || !m_pStorage->FileExists(aExePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Updater or client executable is missing");
		return false;
	}
	str_copy(aInstallDir, aExePath, sizeof(aInstallDir));
	StripFilename(aInstallDir);
	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {aPid, aArchivePath, aInstallDir, aExePath};
	if(process_execute(aUpdaterPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch AMF Client updater");
		return false;
	}
	m_pClient->Quit();
	return true;
#else
	SetStatus("Archive updater is available only on Windows");
	return false;
#endif
}

void CUpdater::CheckForUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(!m_pHttp || State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING || m_bApplyLaunched)
		return;
	const int64_t Now = time_get();
	if(m_LastCheckTime != 0 && Now - m_LastCheckTime < CHECK_COOLDOWN_SECONDS * time_freq())
		return;
	m_LastCheckTime = Now;
	m_bHasCheckedForUpdate = false;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	StartReleaseFetch();
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING || m_bApplyLaunched)
		return;
	if((State == IUpdater::VERSION_AVAILABLE || State == IUpdater::FAIL) && m_aArchiveUrl[0] != '\0')
	{
		StartArchiveDownload();
		return;
	}
	CheckForUpdate();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != IUpdater::NEED_RESTART || m_bApplyLaunched)
		return;
	m_bApplyLaunched = true;
	if(!LaunchUpdaterAndQuit())
	{
		m_bApplyLaunched = false;
		SetCurrentState(IUpdater::FAIL);
	}
}

void CUpdater::Update()
{
	if(m_bAutoCheckPending && m_pHttp && GetCurrentState() == CLEAN)
	{
		m_bAutoCheckPending = false;
		CheckForUpdate();
	}
	if(!m_pCurrentTask || !m_pCurrentTask->Done())
	{
		if(m_pCurrentTask && GetCurrentState() == IUpdater::DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}
	if(m_pCurrentTask->State() != EHttpState::DONE || m_pCurrentTask->StatusCode() >= 400)
	{
		const ETaskKind FailedTask = m_TaskKind;
		ResetTask();
		m_bHasCheckedForUpdate |= FailedTask == ETaskKind::FETCH_RELEASE;
		SetStatus(FailedTask == ETaskKind::FETCH_RELEASE ? "Update check failed" : "Update download failed");
		SetCurrentState(IUpdater::FAIL);
		return;
	}
	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		ParseReleaseTask();
		ResetTask();
	}
	else if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		ResetTask();
		SetPercent(100);
		SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "AMF Client update");
		SetCurrentState(IUpdater::NEED_RESTART);
	}
}
