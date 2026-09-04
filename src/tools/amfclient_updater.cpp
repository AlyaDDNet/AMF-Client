// Standalone AMF Client updater. It only runs after the client has already
// downloaded a complete portable release archive.

// The normal client disables GDI globally to avoid the Windows ERROR macro.
// This tiny standalone GUI deliberately uses GDI and does not include DDNet
// headers, so it can opt back in locally.
#ifdef NOGDI
#undef NOGDI
#endif
#include <windows.h>
#include <wingdi.h>
#include <winuser.h>
#include <unknwn.h>
#include <oaidl.h>
#include <exdisp.h>
#include <oleauto.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace
{
constexpr int WINDOW_WIDTH = 500;
constexpr int WINDOW_HEIGHT = 190;
constexpr UINT WM_WORKER_TICK = WM_APP + 1;
constexpr UINT WM_WORKER_DONE = WM_APP + 2;

constexpr size_t MAX_ARCHIVE_BYTES = 512u * 1024u * 1024u;
constexpr uint64_t MAX_ENTRY_BYTES = 512ull * 1024ull * 1024ull;
constexpr uint64_t MAX_TOTAL_BYTES = 1024ull * 1024ull * 1024ull;
constexpr size_t MAX_ENTRIES = 100000;

HWND g_hWindow = nullptr;
std::atomic<int> g_Percent{0};
std::atomic<bool> g_Failed{false};
CRITICAL_SECTION g_StatusLock;
wchar_t g_aStatus[256] = L"Starting updater...";

void SetStatus(const wchar_t *pStatus)
{
	EnterCriticalSection(&g_StatusLock);
	wcsncpy_s(g_aStatus, pStatus ? pStatus : L"", _TRUNCATE);
	LeaveCriticalSection(&g_StatusLock);
	if(g_hWindow)
		PostMessageW(g_hWindow, WM_WORKER_TICK, 0, 0);
}

void SetPercent(int Percent)
{
	g_Percent.store(Percent < 0 ? 0 : Percent > 100 ? 100 : Percent);
	if(g_hWindow)
		PostMessageW(g_hWindow, WM_WORKER_TICK, 0, 0);
}

void Fail(const wchar_t *pStatus)
{
	g_Failed.store(true);
	SetStatus(pStatus);
}

std::wstring JoinPath(const std::wstring &Base, const std::wstring &Child)
{
	if(Base.empty())
		return Child;
	if(Child.empty())
		return Base;
	if(Base.back() == L'\\' || Base.back() == L'/')
		return Base + Child;
	return Base + L"\\" + Child;
}

std::wstring FullPath(const wchar_t *pPath)
{
	if(!pPath || !pPath[0])
		return {};
	const DWORD Needed = GetFullPathNameW(pPath, 0, nullptr, nullptr);
	if(Needed == 0)
		return {};
	std::wstring Result(Needed, L'\0');
	const DWORD Written = GetFullPathNameW(pPath, Needed, Result.data(), nullptr);
	if(Written == 0 || Written >= Needed)
		return {};
	Result.resize(Written);
	while(Result.size() > 3 && (Result.back() == L'\\' || Result.back() == L'/'))
		Result.pop_back();
	return Result;
}

bool IsPathWithin(const std::wstring &Base, const std::wstring &Path)
{
	if(Base.empty() || Path.empty())
		return false;
	std::wstring Prefix = Base;
	if(Prefix.back() != L'\\')
		Prefix.push_back(L'\\');
	if(Path.size() < Prefix.size())
		return false;
	return _wcsnicmp(Prefix.c_str(), Path.c_str(), Prefix.size()) == 0;
}

bool FileExists(const std::wstring &Path)
{
	const DWORD Attr = GetFileAttributesW(Path.c_str());
	return Attr != INVALID_FILE_ATTRIBUTES && (Attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring &Path)
{
	const DWORD Attr = GetFileAttributesW(Path.c_str());
	return Attr != INVALID_FILE_ATTRIBUTES && (Attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectories(const std::wstring &Path)
{
	for(size_t i = 0; i < Path.size(); ++i)
	{
		if(Path[i] != L'\\' && Path[i] != L'/')
			continue;
		const std::wstring Prefix = Path.substr(0, i);
		if(Prefix.empty() || (Prefix.size() == 2 && Prefix[1] == L':'))
			continue;
		if(!CreateDirectoryW(Prefix.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
	}
	return true;
}

bool DeleteTree(const std::wstring &Path)
{
	const DWORD Attr = GetFileAttributesW(Path.c_str());
	if(Attr == INVALID_FILE_ATTRIBUTES)
		return true;
	if((Attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return DeleteFileW(Path.c_str()) != FALSE;

	WIN32_FIND_DATAW FindData;
	const std::wstring Search = JoinPath(Path, L"*");
	HANDLE Find = FindFirstFileW(Search.c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		Success &= DeleteTree(JoinPath(Path, FindData.cFileName));
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	if(!RemoveDirectoryW(Path.c_str()) && GetLastError() != ERROR_PATH_NOT_FOUND)
		Success = false;
	return Success;
}

namespace zip
{
constexpr uint32_t SIG_EOCD = 0x06054b50;
constexpr uint32_t SIG_CDIR = 0x02014b50;
constexpr uint32_t SIG_LOCAL = 0x04034b50;

uint16_t Read16(const unsigned char *pData)
{
	return static_cast<uint16_t>(pData[0] | (pData[1] << 8));
}

uint32_t Read32(const unsigned char *pData)
{
	return static_cast<uint32_t>(pData[0]) | (static_cast<uint32_t>(pData[1]) << 8) |
		(static_cast<uint32_t>(pData[2]) << 16) | (static_cast<uint32_t>(pData[3]) << 24);
}

struct SEntry
{
	std::string m_Name;
	uint64_t m_CompressedSize = 0;
	uint64_t m_UncompressedSize = 0;
	uint16_t m_Method = 0;
	uint64_t m_LocalOffset = 0;
};

bool IsSafeEntryName(const std::string &Name)
{
	if(Name.empty() || Name.size() > 512 || Name.front() == '/')
		return false;
	if(Name.size() >= 2 && Name[1] == ':')
		return false;
	size_t Begin = 0;
	while(Begin < Name.size())
	{
		size_t End = Name.find('/', Begin);
		if(End == std::string::npos)
			End = Name.size();
		const std::string Component = Name.substr(Begin, End - Begin);
		if(Component == ".." || Component.find(':') != std::string::npos || Component.find('\0') != std::string::npos)
			return false;
		Begin = End + 1;
	}
	return true;
}

std::wstring Widen(const std::string &Utf8)
{
	if(Utf8.empty())
		return {};
	const int Needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8.c_str(), static_cast<int>(Utf8.size()), nullptr, 0);
	if(Needed <= 0)
		return {};
	std::wstring Result(Needed, L'\0');
	if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Utf8.c_str(), static_cast<int>(Utf8.size()), Result.data(), Needed) != Needed)
		return {};
	return Result;
}

bool ReadFileBytes(const std::wstring &Path, std::vector<unsigned char> &vOutput)
{
	const HANDLE File = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(File == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER Size{};
	if(!GetFileSizeEx(File, &Size) || Size.QuadPart <= 0 || static_cast<uint64_t>(Size.QuadPart) > MAX_ARCHIVE_BYTES)
	{
		CloseHandle(File);
		return false;
	}
	vOutput.resize(static_cast<size_t>(Size.QuadPart));
	size_t Done = 0;
	while(Done < vOutput.size())
	{
		const DWORD Chunk = static_cast<DWORD>(std::min<size_t>(vOutput.size() - Done, 0x100000));
		DWORD Read = 0;
		if(!ReadFile(File, vOutput.data() + Done, Chunk, &Read, nullptr) || Read == 0)
		{
			CloseHandle(File);
			return false;
		}
		Done += Read;
	}
	CloseHandle(File);
	return true;
}

bool ReadCentralDirectory(const std::vector<unsigned char> &vZip, std::vector<SEntry> &vEntries)
{
	if(vZip.size() < 22)
		return false;
	const size_t MaxComment = std::min<size_t>(vZip.size(), 65557);
	size_t Eocd = 0;
	bool Found = false;
	for(size_t Back = 22; Back <= MaxComment; ++Back)
	{
		const size_t Pos = vZip.size() - Back;
		if(Read32(&vZip[Pos]) == SIG_EOCD)
		{
			Eocd = Pos;
			Found = true;
			break;
		}
	}
	if(!Found)
		return false;
	const uint32_t Count = Read16(&vZip[Eocd + 10]);
	const uint32_t DirectorySize = Read32(&vZip[Eocd + 12]);
	const uint32_t DirectoryOffset = Read32(&vZip[Eocd + 16]);
	if(Count == 0xffff || DirectorySize == 0xffffffffu || DirectoryOffset == 0xffffffffu || Count > MAX_ENTRIES ||
		static_cast<uint64_t>(DirectoryOffset) + DirectorySize > vZip.size())
		return false;

	size_t Pos = DirectoryOffset;
	uint64_t TotalUncompressed = 0;
	vEntries.clear();
	vEntries.reserve(Count);
	for(uint32_t i = 0; i < Count; ++i)
	{
		if(Pos + 46 > vZip.size() || Read32(&vZip[Pos]) != SIG_CDIR)
			return false;
		SEntry Entry;
		Entry.m_Method = Read16(&vZip[Pos + 10]);
		Entry.m_CompressedSize = Read32(&vZip[Pos + 20]);
		Entry.m_UncompressedSize = Read32(&vZip[Pos + 24]);
		const uint16_t NameLength = Read16(&vZip[Pos + 28]);
		const uint16_t ExtraLength = Read16(&vZip[Pos + 30]);
		const uint16_t CommentLength = Read16(&vZip[Pos + 32]);
		Entry.m_LocalOffset = Read32(&vZip[Pos + 42]);
		const uint64_t RecordSize = 46ull + NameLength + ExtraLength + CommentLength;
		if(RecordSize > vZip.size() - Pos || Entry.m_UncompressedSize > MAX_ENTRY_BYTES || Entry.m_CompressedSize > MAX_ENTRY_BYTES ||
			(Entry.m_Method != 0 && Entry.m_Method != 8))
			return false;
		Entry.m_Name.assign(reinterpret_cast<const char *>(&vZip[Pos + 46]), NameLength);
		for(char &Character : Entry.m_Name)
			if(Character == '\\')
				Character = '/';
		if(!IsSafeEntryName(Entry.m_Name) || TotalUncompressed > MAX_TOTAL_BYTES - Entry.m_UncompressedSize)
			return false;
		TotalUncompressed += Entry.m_UncompressedSize;
		Pos += static_cast<size_t>(RecordSize);
		vEntries.push_back(std::move(Entry));
	}
	return !vEntries.empty();
}

bool WriteAll(HANDLE File, const unsigned char *pData, size_t Size)
{
	size_t Done = 0;
	while(Done < Size)
	{
		const DWORD Chunk = static_cast<DWORD>(std::min<size_t>(Size - Done, 0x100000));
		DWORD Written = 0;
		if(!WriteFile(File, pData + Done, Chunk, &Written, nullptr) || Written == 0)
			return false;
		Done += Written;
	}
	return true;
}

bool ExtractEntry(const std::vector<unsigned char> &vZip, const SEntry &Entry, const std::wstring &Destination)
{
	if(Entry.m_LocalOffset + 30 > vZip.size() || Read32(&vZip[static_cast<size_t>(Entry.m_LocalOffset)]) != SIG_LOCAL)
		return false;
	const uint16_t LocalNameLength = Read16(&vZip[static_cast<size_t>(Entry.m_LocalOffset) + 26]);
	const uint16_t LocalExtraLength = Read16(&vZip[static_cast<size_t>(Entry.m_LocalOffset) + 28]);
	const uint64_t DataOffset = Entry.m_LocalOffset + 30ull + LocalNameLength + LocalExtraLength;
	if(DataOffset > vZip.size() || Entry.m_CompressedSize > vZip.size() - DataOffset || !EnsureDirectories(Destination))
		return false;
	const HANDLE File = CreateFileW(Destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(File == INVALID_HANDLE_VALUE)
		return false;

	const unsigned char *pSource = &vZip[static_cast<size_t>(DataOffset)];
	bool Success = true;
	if(Entry.m_Method == 0)
	{
		Success = Entry.m_CompressedSize == Entry.m_UncompressedSize && WriteAll(File, pSource, static_cast<size_t>(Entry.m_CompressedSize));
	}
	else
	{
		z_stream Stream{};
		if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
		{
			CloseHandle(File);
			return false;
		}
		Stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(pSource));
		Stream.avail_in = static_cast<uInt>(Entry.m_CompressedSize);
		std::vector<unsigned char> vOutput(0x40000);
		uint64_t Total = 0;
		int Result = Z_OK;
		do
		{
			Stream.next_out = vOutput.data();
			Stream.avail_out = static_cast<uInt>(vOutput.size());
			Result = inflate(&Stream, Z_NO_FLUSH);
			if(Result != Z_OK && Result != Z_STREAM_END && Result != Z_BUF_ERROR)
			{
				Success = false;
				break;
			}
			const size_t Produced = vOutput.size() - Stream.avail_out;
			if(Produced == 0 && Result == Z_BUF_ERROR)
			{
				Success = false;
				break;
			}
			Total += Produced;
			if(Total > Entry.m_UncompressedSize || (Produced > 0 && !WriteAll(File, vOutput.data(), Produced)))
			{
				Success = false;
				break;
			}
		} while(Result != Z_STREAM_END);
		inflateEnd(&Stream);
		Success &= Result == Z_STREAM_END && Total == Entry.m_UncompressedSize;
	}
	CloseHandle(File);
	if(!Success)
		DeleteFileW(Destination.c_str());
	return Success;
}

bool Extract(const std::wstring &Archive, const std::wstring &Destination, const std::function<void(int, int)> &PerEntry)
{
	std::vector<unsigned char> vZip;
	std::vector<SEntry> vEntries;
	if(!ReadFileBytes(Archive, vZip) || !ReadCentralDirectory(vZip, vEntries))
		return false;
	int Done = 0;
	for(const SEntry &Entry : vEntries)
	{
		const std::wstring Relative = Widen(Entry.m_Name);
		if(Relative.empty())
			return false;
		std::wstring Output = JoinPath(Destination, Relative);
		for(wchar_t &Character : Output)
			if(Character == L'/')
				Character = L'\\';
		const bool IsDirectory = !Entry.m_Name.empty() && Entry.m_Name.back() == '/';
		if(IsDirectory)
		{
			if(!EnsureDirectories(Output + L"\\") || (!CreateDirectoryW(Output.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
				return false;
		}
		else if(!ExtractEntry(vZip, Entry, Output))
			return false;
		++Done;
		if(PerEntry)
			PerEntry(Done, static_cast<int>(vEntries.size()));
	}
	return true;
}
} // namespace zip

std::wstring RelativePath(const std::wstring &Root, const std::wstring &Path)
{
	if(Path.size() <= Root.size())
		return {};
	const size_t Start = Root.back() == L'\\' ? Root.size() : Root.size() + 1;
	return Start <= Path.size() ? Path.substr(Start) : std::wstring();
}

bool ShouldSkipUpdateFile(const std::wstring &SourceRoot, const std::wstring &Source)
{
	const std::wstring Relative = RelativePath(SourceRoot, Source);
	return _wcsicmp(Relative.c_str(), L"amfclient-updater.exe") == 0;
}

using FFileCallback = std::function<void()>;

bool CopyTree(const std::wstring &Source, const std::wstring &Destination, const std::wstring &SourceRoot, bool SkipUpdater, const FFileCallback &PerFile = {})
{
	if(!EnsureDirectories(Destination + L"\\") || (!CreateDirectoryW(Destination.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
		return false;
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Source, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring SourcePath = JoinPath(Source, FindData.cFileName);
		const std::wstring DestinationPath = JoinPath(Destination, FindData.cFileName);
		if(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if(!CopyTree(SourcePath, DestinationPath, SourceRoot, SkipUpdater, PerFile))
			{
				Success = false;
				break;
			}
		}
		else if(!SkipUpdater || !ShouldSkipUpdateFile(SourceRoot, SourcePath))
		{
			if(!EnsureDirectories(DestinationPath) || !CopyFileW(SourcePath.c_str(), DestinationPath.c_str(), FALSE))
			{
				Success = false;
				break;
			}
			if(PerFile)
				PerFile();
		}
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Success;
}

bool CopyAny(const std::wstring &Source, const std::wstring &Destination)
{
	const DWORD Attr = GetFileAttributesW(Source.c_str());
	if(Attr == INVALID_FILE_ATTRIBUTES)
		return true;
	if(Attr & FILE_ATTRIBUTE_DIRECTORY)
		return CopyTree(Source, Destination, Source, false);
	return EnsureDirectories(Destination) && CopyFileW(Source.c_str(), Destination.c_str(), FALSE) != FALSE;
}

bool BackupOverwrittenFiles(const std::wstring &Staged, const std::wstring &Install, const std::wstring &Rollback, const std::wstring &SourceRoot)
{
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Staged, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring StagedPath = JoinPath(Staged, FindData.cFileName);
		const std::wstring InstallPath = JoinPath(Install, FindData.cFileName);
		const std::wstring RollbackPath = JoinPath(Rollback, FindData.cFileName);
		if(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if(!BackupOverwrittenFiles(StagedPath, InstallPath, RollbackPath, SourceRoot))
			{
				Success = false;
				break;
			}
		}
		else if(!ShouldSkipUpdateFile(SourceRoot, StagedPath) && FileExists(InstallPath) && !CopyAny(InstallPath, RollbackPath))
		{
			Success = false;
			break;
		}
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Success;
}

bool RemoveNewFiles(const std::wstring &Staged, const std::wstring &Install, const std::wstring &Rollback, const std::wstring &SourceRoot)
{
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Staged, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring StagedPath = JoinPath(Staged, FindData.cFileName);
		const std::wstring InstallPath = JoinPath(Install, FindData.cFileName);
		const std::wstring RollbackPath = JoinPath(Rollback, FindData.cFileName);
		if(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			Success &= RemoveNewFiles(StagedPath, InstallPath, RollbackPath, SourceRoot);
		}
		else if(!ShouldSkipUpdateFile(SourceRoot, StagedPath) && !FileExists(RollbackPath) && FileExists(InstallPath))
		{
			Success &= DeleteFileW(InstallPath.c_str()) != FALSE;
		}
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Success;
}

int CountFiles(const std::wstring &Path, const std::wstring &SourceRoot)
{
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Path, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return 0;
	int Count = 0;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring Child = JoinPath(Path, FindData.cFileName);
		if(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			Count += CountFiles(Child, SourceRoot);
		else if(!ShouldSkipUpdateFile(SourceRoot, Child))
			++Count;
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Count;
}

std::wstring NormalizeRelativePath(std::wstring Path)
{
	std::replace(Path.begin(), Path.end(), L'/', L'\\');
	// The distribution manifest is input to the deletion list. It must contain a
	// strictly relative path: reject rooted paths and traversal *segments*, but
	// keep ordinary file names such as "notes..old.txt" valid.
	if(Path.empty() || Path.front() == L'\\' || Path.find(L':') != std::wstring::npos)
		return {};
	for(size_t Begin = 0; Begin < Path.size();)
	{
		const size_t End = Path.find(L'\\', Begin);
		const size_t Length = (End == std::wstring::npos ? Path.size() : End) - Begin;
		if(Length == 0 || (Length == 1 && Path[Begin] == L'.') ||
			(Length == 2 && Path[Begin] == L'.' && Path[Begin + 1] == L'.'))
			return {};
		if(End == std::wstring::npos)
			break;
		Begin = End + 1;
	}
	std::transform(Path.begin(), Path.end(), Path.begin(), [](wchar_t Character) { return static_cast<wchar_t>(towlower(Character)); });
	return Path;
}

bool CollectRelativeFiles(const std::wstring &Root, const std::wstring &Directory, std::set<std::wstring> &Files)
{
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Directory, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring Path = JoinPath(Directory, FindData.cFileName);
		if(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			Success &= CollectRelativeFiles(Root, Path, Files);
		}
		else
		{
			const std::wstring Relative = NormalizeRelativePath(RelativePath(Root, Path));
			if(Relative.empty())
				Success = false;
			else
				Files.insert(Relative);
		}
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Success;
}

bool CopyUserFilesNotInDistribution(const std::wstring &Source, const std::wstring &Destination, const std::wstring &InstallRoot, const std::set<std::wstring> &DistributionFiles)
{
	const DWORD Attributes = GetFileAttributesW(Source.c_str());
	if(Attributes == INVALID_FILE_ATTRIBUTES)
		return true;
	if((Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		const std::wstring Relative = NormalizeRelativePath(RelativePath(InstallRoot, Source));
		return !Relative.empty() && (DistributionFiles.contains(Relative) || CopyAny(Source, Destination));
	}

	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Source, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return false;
	bool Success = true;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		const std::wstring SourcePath = JoinPath(Source, FindData.cFileName);
		const std::wstring DestinationPath = JoinPath(Destination, FindData.cFileName);
		Success &= CopyUserFilesNotInDistribution(SourcePath, DestinationPath, InstallRoot, DistributionFiles);
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	return Success;
}

const wchar_t *const USER_DATA_FILES[] = {
	L"settings.cfg", L"settings_ddnet.cfg", L"settings_tclient.cfg", L"settings_amfclient.cfg",
	L"amf_hud_layout.cfg", L"amf_asset_layout.cfg", L"tclient_profiles.cfg", L"tclient_chatbinds.cfg", L"tclient_warlist.cfg",
	L"autoexec.cfg", L"autoexec_client.cfg", L"storage.cfg",
};

const wchar_t *const USER_DATA_DIRECTORIES[] = {
	L"data\\assets", L"data\\audio", L"data\\downloadedmaps", L"data\\downloadedskins", L"data\\maps", L"data\\maps7",
	L"data\\skins", L"data\\skins7", L"data\\themes",
	L"demos", L"screenshots", L"editor", L"ghosts", L"downloadedmaps", L"downloadedskins",
};

bool PreserveUserData(const std::wstring &From, const std::wstring &To, const std::set<std::wstring> &DistributionFiles)
{
	for(const wchar_t *pRelative : USER_DATA_FILES)
	{
		const std::wstring Source = JoinPath(From, pRelative);
		if(GetFileAttributesW(Source.c_str()) != INVALID_FILE_ATTRIBUTES && !CopyAny(Source, JoinPath(To, pRelative)))
			return false;
	}
	for(const wchar_t *pRelative : USER_DATA_DIRECTORIES)
	{
		const std::wstring Source = JoinPath(From, pRelative);
		if(!CopyUserFilesNotInDistribution(Source, JoinPath(To, pRelative), From, DistributionFiles))
			return false;
	}
	return true;
}

bool ReadDistributionManifest(const std::wstring &Path, std::set<std::wstring> &Files)
{
	std::vector<unsigned char> vBytes;
	if(!zip::ReadFileBytes(Path, vBytes))
		return false;
	std::wstring Line;
	for(const unsigned char Byte : vBytes)
	{
		if(Byte != '\r' && Byte != '\n')
		{
			Line.push_back(static_cast<wchar_t>(Byte));
			continue;
		}
		if(!Line.empty() && Line.front() != L'#')
		{
			const std::wstring Relative = NormalizeRelativePath(Line);
			if(Relative.empty())
				return false;
			Files.insert(Relative);
		}
		Line.clear();
	}
	if(!Line.empty() && Line.front() != L'#')
	{
		const std::wstring Relative = NormalizeRelativePath(Line);
		if(Relative.empty())
			return false;
		Files.insert(Relative);
	}
	return !Files.empty();
}

std::vector<std::wstring> FindObsoleteDistributionFiles(const std::wstring &Install, const std::set<std::wstring> &NewFiles)
{
	std::set<std::wstring> OldFiles;
	std::vector<std::wstring> Obsolete;
	if(ReadDistributionManifest(JoinPath(Install, L"amf_distribution_manifest.txt"), OldFiles))
	{
		for(const std::wstring &Relative : OldFiles)
		{
			if(!NewFiles.contains(Relative))
				Obsolete.push_back(Relative);
		}
	}
	else if(!NewFiles.contains(L"steam_api.lib"))
	{
		// All known 1.0.x portable archives shipped this build-only import library.
		// It is not user data and is absent from the AMF 1.1 portable package.
		Obsolete.emplace_back(L"steam_api.lib");
	}
	return Obsolete;
}

bool BackupObsoleteDistributionFiles(const std::vector<std::wstring> &Obsolete, const std::wstring &Install, const std::wstring &Rollback)
{
	for(const std::wstring &Relative : Obsolete)
	{
		const std::wstring Source = JoinPath(Install, Relative);
		if(GetFileAttributesW(Source.c_str()) != INVALID_FILE_ATTRIBUTES && !CopyAny(Source, JoinPath(Rollback, Relative)))
			return false;
	}
	return true;
}

bool RemoveObsoleteDistributionFiles(const std::vector<std::wstring> &Obsolete, const std::wstring &Install)
{
	for(const std::wstring &Relative : Obsolete)
	{
		const std::wstring Path = JoinPath(Install, Relative);
		if(GetFileAttributesW(Path.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteTree(Path))
			return false;
	}
	return true;
}

std::wstring ResolveCopyRoot(const std::wstring &Extract)
{
	WIN32_FIND_DATAW FindData;
	HANDLE Find = FindFirstFileW(JoinPath(Extract, L"*").c_str(), &FindData);
	if(Find == INVALID_HANDLE_VALUE)
		return {};
	int Count = 0;
	std::wstring First;
	DWORD FirstAttributes = 0;
	do
	{
		if(wcscmp(FindData.cFileName, L".") == 0 || wcscmp(FindData.cFileName, L"..") == 0)
			continue;
		++Count;
		if(Count == 1)
		{
			First = FindData.cFileName;
			FirstAttributes = FindData.dwFileAttributes;
		}
	} while(FindNextFileW(Find, &FindData));
	FindClose(Find);
	if(Count == 1 && (FirstAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return JoinPath(Extract, First);
	return Extract;
}

struct SWorkerArgs
{
	DWORD m_Pid = 0;
	std::wstring m_Archive;
	std::wstring m_InstallDirectory;
	std::wstring m_ClientExecutable;
	bool m_NoLaunch = false;
};

bool LaunchClient(const SWorkerArgs &Args)
{
	SHELLEXECUTEINFOW ExecuteInfo{};
	ExecuteInfo.cbSize = sizeof(ExecuteInfo);
	ExecuteInfo.lpVerb = L"open";
	ExecuteInfo.lpFile = Args.m_ClientExecutable.c_str();
	ExecuteInfo.lpDirectory = Args.m_InstallDirectory.c_str();
	ExecuteInfo.nShow = SW_SHOWNORMAL;
	return ShellExecuteExW(&ExecuteInfo) != FALSE && reinterpret_cast<INT_PTR>(ExecuteInfo.hInstApp) > 32;
}

std::wstring ParentPath(const std::wstring &Path)
{
	const size_t Separator = Path.find_last_of(L"\\/");
	return Separator == std::wstring::npos ? std::wstring() : Path.substr(0, Separator);
}

std::wstring QuoteArgument(const std::wstring &Argument)
{
	return L"\"" + Argument + L"\"";
}

bool WaitForProcess(DWORD ProcessId)
{
	if(ProcessId == 0)
		return false;
	HANDLE Process = OpenProcess(SYNCHRONIZE, FALSE, ProcessId);
	if(!Process)
		return true;
	WaitForSingleObject(Process, INFINITE);
	CloseHandle(Process);
	return true;
}

bool GetCurrentExecutablePath(std::wstring &Path)
{
	std::vector<wchar_t> vBuffer(32768);
	const DWORD Length = GetModuleFileNameW(nullptr, vBuffer.data(), static_cast<DWORD>(vBuffer.size()));
	if(Length == 0 || Length >= vBuffer.size())
		return false;
	Path.assign(vBuffer.data(), Length);
	return true;
}

bool LaunchDetached(const std::wstring &Executable, const std::wstring &Parameters)
{
	// ShellExecuteExW retains these pointers for the duration of the call. Keep
	// the working-directory string alive instead of passing c_str() from a
	// temporary ParentPath result.
	const std::wstring WorkingDirectory = ParentPath(Executable);
	SHELLEXECUTEINFOW ExecuteInfo{};
	ExecuteInfo.cbSize = sizeof(ExecuteInfo);
	ExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ExecuteInfo.lpVerb = L"open";
	ExecuteInfo.lpFile = Executable.c_str();
	ExecuteInfo.lpParameters = Parameters.c_str();
	ExecuteInfo.lpDirectory = WorkingDirectory.c_str();
	ExecuteInfo.nShow = SW_HIDE;
	const bool Success = ShellExecuteExW(&ExecuteInfo) != FALSE && reinterpret_cast<INT_PTR>(ExecuteInfo.hInstApp) > 32;
	if(ExecuteInfo.hProcess)
		CloseHandle(ExecuteInfo.hProcess);
	return Success;
}

bool ExplorerLocationPath(IWebBrowser2 *pBrowser, std::wstring &Path)
{
	BSTR pLocationUrl = nullptr;
	if(!pBrowser || FAILED(pBrowser->get_LocationURL(&pLocationUrl)) || !pLocationUrl)
		return false;

	DWORD Size = 32768;
	std::vector<wchar_t> vPath(Size);
	const HRESULT Result = PathCreateFromUrlW(pLocationUrl, vPath.data(), &Size, 0);
	SysFreeString(pLocationUrl);
	if(FAILED(Result))
		return false;
	Path = FullPath(vPath.data());
	return !Path.empty();
}

// Close only filesystem Explorer views which point at the old portable
// installation or a child of it. Other shell windows are left untouched.
void CloseExplorerWindowsForInstall(const std::wstring &Install)
{
	const HRESULT InitializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if(FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
		return;

	IShellWindows *pShellWindows = nullptr;
	if(SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&pShellWindows))) && pShellWindows)
	{
		long Count = 0;
		if(SUCCEEDED(pShellWindows->get_Count(&Count)))
		{
			for(long Index = Count - 1; Index >= 0; --Index)
			{
				VARIANT ItemIndex;
				VariantInit(&ItemIndex);
				ItemIndex.vt = VT_I4;
				ItemIndex.lVal = Index;
				IDispatch *pDispatch = nullptr;
				if(SUCCEEDED(pShellWindows->Item(ItemIndex, &pDispatch)) && pDispatch)
				{
					IWebBrowser2 *pBrowser = nullptr;
					if(SUCCEEDED(pDispatch->QueryInterface(IID_PPV_ARGS(&pBrowser))) && pBrowser)
					{
						std::wstring Location;
						if(ExplorerLocationPath(pBrowser, Location) && (_wcsicmp(Location.c_str(), Install.c_str()) == 0 || IsPathWithin(Install, Location)))
							pBrowser->Quit();
						pBrowser->Release();
					}
					pDispatch->Release();
				}
				VariantClear(&ItemIndex);
			}
		}
		pShellWindows->Release();
	}
	if(SUCCEEDED(InitializeResult))
		CoUninitialize();
}

bool IsPortableRename(const std::wstring &Install, const std::wstring &Target, const std::wstring &ClientExecutable)
{
	return DirectoryExists(Install) && !DirectoryExists(Target) &&
		_wcsicmp(ParentPath(Install).c_str(), ParentPath(Target).c_str()) == 0 &&
		FileExists(JoinPath(Install, L"DDNet.exe")) && _wcsicmp(JoinPath(Target, L"DDNet.exe").c_str(), ClientExecutable.c_str()) == 0;
}

int RunInstallMigrationWorker(DWORD ClientPid, DWORD BootstrapPid, const std::wstring &Install, const std::wstring &Target, const std::wstring &TargetClient, bool NoLaunch)
{
	if(!IsPortableRename(Install, Target, TargetClient))
		return 1;
	WaitForProcess(ClientPid);
	WaitForProcess(BootstrapPid);
	CloseExplorerWindowsForInstall(Install);

	bool Renamed = false;
	for(int Attempt = 0; Attempt < 40; ++Attempt)
	{
		if(MoveFileExW(Install.c_str(), Target.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			Renamed = true;
			break;
		}
		if(GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED)
			break;
		Sleep(50);
	}
	if(!Renamed)
		return NoLaunch || LaunchDetached(JoinPath(Install, L"DDNet.exe"), L"") ? 0 : 1;

	// `steam_api.lib` was a 1.0.x package-only build artifact. Delete only this
	// known legacy distribution file; unknown user files are kept intact.
	DeleteFileW(JoinPath(Target, L"steam_api.lib").c_str());
	const std::wstring NewUpdater = JoinPath(Target, L"amfclient-install-helper.exe");
	if(FileExists(NewUpdater))
		CopyFileW(NewUpdater.c_str(), JoinPath(Target, L"amfclient-updater.exe").c_str(), FALSE);
	return NoLaunch || LaunchDetached(TargetClient, L"") ? 0 : 1;
}

int RunInstallMigrationBootstrap(DWORD ClientPid, const std::wstring &Install, const std::wstring &Target, const std::wstring &TargetClient, bool NoLaunch)
{
	if(!IsPortableRename(Install, Target, TargetClient))
		return 1;
	std::wstring CurrentExecutable;
	if(!GetCurrentExecutablePath(CurrentExecutable))
		return 1;
	wchar_t aTempDirectory[MAX_PATH];
	if(GetTempPathW(std::size(aTempDirectory), aTempDirectory) == 0)
		return 1;
	const std::wstring TemporaryHelper = JoinPath(aTempDirectory, L"AMFClient-install-helper-" + std::to_wstring(ClientPid) + L"-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
	DeleteFileW(TemporaryHelper.c_str());
	if(!CopyFileW(CurrentExecutable.c_str(), TemporaryHelper.c_str(), FALSE))
		return 1;
	const std::wstring Parameters = L"--migrate-install-worker " + std::to_wstring(ClientPid) + L" " + std::to_wstring(GetCurrentProcessId()) + L" " +
		QuoteArgument(Install) + L" " + QuoteArgument(Target) + L" " + QuoteArgument(TargetClient) + (NoLaunch ? L" --no-launch" : L"");
	if(LaunchDetached(TemporaryHelper, Parameters))
		return 0;
	DeleteFileW(TemporaryHelper.c_str());
	return 1;
}

DWORD WINAPI WorkerThread(LPVOID pUser)
{
	std::unique_ptr<SWorkerArgs> pArgs(static_cast<SWorkerArgs *>(pUser));
	SetStatus(L"Waiting for AMF Client to close...");
	SetPercent(2);
	if(HANDLE Process = OpenProcess(SYNCHRONIZE, FALSE, pArgs->m_Pid))
	{
		WaitForSingleObject(Process, INFINITE);
		CloseHandle(Process);
	}
	else
	{
		Sleep(300);
	}

	const std::wstring UpdateDirectory = JoinPath(pArgs->m_InstallDirectory, L"update");
	const std::wstring ExtractDirectory = JoinPath(UpdateDirectory, L"amf-extract-" + std::to_wstring(pArgs->m_Pid));
	const std::wstring RollbackDirectory = JoinPath(UpdateDirectory, L"amf-rollback-" + std::to_wstring(pArgs->m_Pid));
	const std::wstring PreserveDirectory = JoinPath(UpdateDirectory, L"amf-preserve-" + std::to_wstring(pArgs->m_Pid));
	if(!IsPathWithin(pArgs->m_InstallDirectory, ExtractDirectory) || !IsPathWithin(pArgs->m_InstallDirectory, RollbackDirectory) || !IsPathWithin(pArgs->m_InstallDirectory, PreserveDirectory))
	{
		Fail(L"Invalid updater working directory.");
		return 1;
	}
	DeleteTree(ExtractDirectory);
	DeleteTree(RollbackDirectory);
	DeleteTree(PreserveDirectory);
	if(!EnsureDirectories(ExtractDirectory + L"\\") || (!CreateDirectoryW(ExtractDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
	{
		Fail(L"Failed to create update staging directory.");
		return 1;
	}

	SetStatus(L"Validating and extracting update...");
	if(!zip::Extract(pArgs->m_Archive, ExtractDirectory, [](int Done, int Total) {
		SetPercent(5 + Done * 40 / (Total > 0 ? Total : 1));
	}))
	{
		DeleteTree(ExtractDirectory);
		Fail(L"Update archive is invalid or corrupted.");
		return 1;
	}
	const std::wstring CopyRoot = ResolveCopyRoot(ExtractDirectory);
	if(CopyRoot.empty() || !FileExists(JoinPath(CopyRoot, L"DDNet.exe")))
	{
		DeleteTree(ExtractDirectory);
		Fail(L"Update archive does not contain AMF Client.");
		return 1;
	}
	std::set<std::wstring> NewDistributionFiles;
	if(!CollectRelativeFiles(CopyRoot, CopyRoot, NewDistributionFiles))
	{
		DeleteTree(ExtractDirectory);
		Fail(L"Could not read the update file manifest.");
		return 1;
	}
	const std::vector<std::wstring> ObsoleteDistributionFiles = FindObsoleteDistributionFiles(pArgs->m_InstallDirectory, NewDistributionFiles);

	SetStatus(L"Protecting AMF Client data...");
	if(!PreserveUserData(pArgs->m_InstallDirectory, PreserveDirectory, NewDistributionFiles) ||
		!BackupOverwrittenFiles(CopyRoot, pArgs->m_InstallDirectory, RollbackDirectory, CopyRoot) ||
		!BackupObsoleteDistributionFiles(ObsoleteDistributionFiles, pArgs->m_InstallDirectory, RollbackDirectory))
	{
		DeleteTree(ExtractDirectory);
		DeleteTree(RollbackDirectory);
		DeleteTree(PreserveDirectory);
		Fail(L"Failed to back up the current installation.");
		return 1;
	}
	SetPercent(50);

	SetStatus(L"Installing AMF Client update...");
	const int TotalFiles = std::max(1, CountFiles(CopyRoot, CopyRoot));
	int DoneFiles = 0;
	const bool Installed = CopyTree(CopyRoot, pArgs->m_InstallDirectory, CopyRoot, true, [&]() {
		++DoneFiles;
		SetPercent(50 + DoneFiles * 35 / TotalFiles);
	});
	if(!Installed)
	{
		SetStatus(L"Restoring previous AMF Client files...");
		RemoveNewFiles(CopyRoot, pArgs->m_InstallDirectory, RollbackDirectory, CopyRoot);
		CopyTree(RollbackDirectory, pArgs->m_InstallDirectory, RollbackDirectory, false);
		PreserveUserData(PreserveDirectory, pArgs->m_InstallDirectory, NewDistributionFiles);
		Fail(L"Could not replace all update files. The previous version was restored.");
		return 1;
	}

	if(!RemoveObsoleteDistributionFiles(ObsoleteDistributionFiles, pArgs->m_InstallDirectory))
	{
		SetStatus(L"Restoring previous AMF Client files...");
		RemoveNewFiles(CopyRoot, pArgs->m_InstallDirectory, RollbackDirectory, CopyRoot);
		CopyTree(RollbackDirectory, pArgs->m_InstallDirectory, RollbackDirectory, false);
		PreserveUserData(PreserveDirectory, pArgs->m_InstallDirectory, NewDistributionFiles);
		Fail(L"Could not clean obsolete update files. The previous version was restored.");
		return 1;
	}

	SetStatus(L"Restoring AMF Client data...");
	if(!PreserveUserData(PreserveDirectory, pArgs->m_InstallDirectory, NewDistributionFiles))
	{
		RemoveNewFiles(CopyRoot, pArgs->m_InstallDirectory, RollbackDirectory, CopyRoot);
		CopyTree(RollbackDirectory, pArgs->m_InstallDirectory, RollbackDirectory, false);
		Fail(L"Could not restore AMF Client data. The previous version was restored.");
		return 1;
	}
	SetPercent(95);

	if(!pArgs->m_NoLaunch && (!FileExists(pArgs->m_ClientExecutable) || !LaunchClient(*pArgs)))
	{
		SetStatus(L"Restoring previous AMF Client files...");
		RemoveNewFiles(CopyRoot, pArgs->m_InstallDirectory, RollbackDirectory, CopyRoot);
		CopyTree(RollbackDirectory, pArgs->m_InstallDirectory, RollbackDirectory, false);
		PreserveUserData(PreserveDirectory, pArgs->m_InstallDirectory, NewDistributionFiles);
		Fail(L"Updated AMF Client could not be launched. The previous version was restored.");
		return 1;
	}

	SetStatus(pArgs->m_NoLaunch ? L"Updater test completed." : L"Starting updated AMF Client...");
	SetPercent(100);
	DeleteFileW(pArgs->m_Archive.c_str());
	DeleteTree(ExtractDirectory);
	DeleteTree(RollbackDirectory);
	DeleteTree(PreserveDirectory);
	Sleep(250);
	if(g_hWindow)
		PostMessageW(g_hWindow, WM_WORKER_DONE, 0, 0);
	return 0;
}

COLORREF Lerp(COLORREF From, COLORREF To, float Amount)
{
	return RGB(static_cast<int>(GetRValue(From) + (GetRValue(To) - GetRValue(From)) * Amount),
		static_cast<int>(GetGValue(From) + (GetGValue(To) - GetGValue(From)) * Amount),
		static_cast<int>(GetBValue(From) + (GetBValue(To) - GetBValue(From)) * Amount));
}

void DrawGradient(HDC DeviceContext, const RECT &Rect, COLORREF From, COLORREF To)
{
	const int Width = Rect.right - Rect.left;
	for(int X = 0; X < Width; ++X)
	{
		const float Amount = Width > 1 ? static_cast<float>(X) / (Width - 1) : 0.0f;
		HBRUSH Brush = CreateSolidBrush(Lerp(From, To, Amount));
		const RECT Column{Rect.left + X, Rect.top, Rect.left + X + 1, Rect.bottom};
		FillRect(DeviceContext, &Column, Brush);
		DeleteObject(Brush);
	}
}

HFONT MakeFont(int Size, bool Bold)
{
	return CreateFontW(Size, 0, 0, 0, Bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void Paint(HWND Window)
{
	PAINTSTRUCT PaintStruct;
	HDC DeviceContext = BeginPaint(Window, &PaintStruct);
	RECT ClientRect;
	GetClientRect(Window, &ClientRect);
	HDC MemoryContext = CreateCompatibleDC(DeviceContext);
	HBITMAP Bitmap = CreateCompatibleBitmap(DeviceContext, ClientRect.right, ClientRect.bottom);
	HGDIOBJ OldBitmap = SelectObject(MemoryContext, Bitmap);
	HBRUSH Background = CreateSolidBrush(RGB(17, 28, 39));
	FillRect(MemoryContext, &ClientRect, Background);
	DeleteObject(Background);
	const RECT Accent{0, 0, ClientRect.right, 5};
	DrawGradient(MemoryContext, Accent, RGB(73, 180, 255), RGB(124, 84, 255));
	SetBkMode(MemoryContext, TRANSPARENT);

	HFONT TitleFont = MakeFont(27, true);
	HGDIOBJ OldFont = SelectObject(MemoryContext, TitleFont);
	SetTextColor(MemoryContext, RGB(236, 244, 255));
	RECT TitleRect{0, 20, ClientRect.right, 60};
	DrawTextW(MemoryContext, L"AMF Client Updater", -1, &TitleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
	SelectObject(MemoryContext, OldFont);
	DeleteObject(TitleFont);

	const RECT BarRect{40, 83, ClientRect.right - 40, 108};
	HBRUSH BarBackground = CreateSolidBrush(RGB(42, 57, 71));
	FillRect(MemoryContext, &BarRect, BarBackground);
	DeleteObject(BarBackground);
	const int Percent = g_Percent.load();
	if(Percent > 0)
	{
		RECT Fill = BarRect;
		Fill.right = BarRect.left + (BarRect.right - BarRect.left) * Percent / 100;
		DrawGradient(MemoryContext, Fill, g_Failed.load() ? RGB(215, 70, 70) : RGB(73, 180, 255), g_Failed.load() ? RGB(215, 70, 70) : RGB(124, 84, 255));
	}

	HFONT StatusFont = MakeFont(14, false);
	OldFont = SelectObject(MemoryContext, StatusFont);
	SetTextColor(MemoryContext, g_Failed.load() ? RGB(255, 130, 130) : RGB(183, 205, 224));
	wchar_t aStatus[256];
	EnterCriticalSection(&g_StatusLock);
	wcscpy_s(aStatus, g_aStatus);
	LeaveCriticalSection(&g_StatusLock);
	RECT StatusRect{40, 120, ClientRect.right - 40, 148};
	DrawTextW(MemoryContext, aStatus, -1, &StatusRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	if(g_Failed.load())
	{
		HFONT HintFont = MakeFont(11, false);
		SelectObject(MemoryContext, HintFont);
		SetTextColor(MemoryContext, RGB(140, 160, 180));
		RECT HintRect{40, 153, ClientRect.right - 40, 177};
		DrawTextW(MemoryContext, L"Press Esc to close. Your existing client files were kept or restored.", -1, &HintRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
		SelectObject(MemoryContext, StatusFont);
		DeleteObject(HintFont);
	}
	SelectObject(MemoryContext, OldFont);
	DeleteObject(StatusFont);
	SelectObject(MemoryContext, OldBitmap);
	BitBlt(DeviceContext, 0, 0, ClientRect.right, ClientRect.bottom, MemoryContext, 0, 0, SRCCOPY);
	DeleteObject(Bitmap);
	DeleteDC(MemoryContext);
	EndPaint(Window, &PaintStruct);
}

LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	switch(Message)
	{
	case WM_PAINT: Paint(Window); return 0;
	case WM_ERASEBKGND: return 1;
	case WM_WORKER_TICK: InvalidateRect(Window, nullptr, FALSE); return 0;
	case WM_WORKER_DONE: DestroyWindow(Window); return 0;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	case WM_NCHITTEST:
		return DefWindowProcW(Window, Message, WParam, LParam) == HTCLIENT ? HTCAPTION : DefWindowProcW(Window, Message, WParam, LParam);
	case WM_KEYDOWN:
		if(WParam == VK_ESCAPE && g_Failed.load())
			DestroyWindow(Window);
		return 0;
	}
	return DefWindowProcW(Window, Message, WParam, LParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE Instance, HINSTANCE, LPSTR, int)
{
	int ArgumentCount = 0;
	LPWSTR *ppArguments = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
	if(ppArguments && (ArgumentCount == 6 || (ArgumentCount == 7 && _wcsicmp(ppArguments[6], L"--no-launch") == 0)) && _wcsicmp(ppArguments[1], L"--migrate-install") == 0)
	{
		const int Result = RunInstallMigrationBootstrap(
			static_cast<DWORD>(_wtol(ppArguments[2])),
			FullPath(ppArguments[3]),
			FullPath(ppArguments[4]),
			FullPath(ppArguments[5]),
			ArgumentCount == 7);
		LocalFree(ppArguments);
		return Result;
	}
	if(ppArguments && (ArgumentCount == 7 || (ArgumentCount == 8 && _wcsicmp(ppArguments[7], L"--no-launch") == 0)) && _wcsicmp(ppArguments[1], L"--migrate-install-worker") == 0)
	{
		const int Result = RunInstallMigrationWorker(
			static_cast<DWORD>(_wtol(ppArguments[2])),
			static_cast<DWORD>(_wtol(ppArguments[3])),
			FullPath(ppArguments[4]),
			FullPath(ppArguments[5]),
			FullPath(ppArguments[6]),
			ArgumentCount == 8);
		std::wstring CurrentExecutable;
		if(GetCurrentExecutablePath(CurrentExecutable))
			MoveFileExW(CurrentExecutable.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
		LocalFree(ppArguments);
		return Result;
	}

	InitializeCriticalSection(&g_StatusLock);
	if(!ppArguments || ArgumentCount < 5)
	{
		MessageBoxW(nullptr, L"Usage: amfclient-updater.exe <pid> <archive> <install_dir> <client_exe> [--no-launch]", L"AMF Client Updater", MB_ICONERROR);
		if(ppArguments)
			LocalFree(ppArguments);
		DeleteCriticalSection(&g_StatusLock);
		return 1;
	}

	auto *pArgs = new SWorkerArgs;
	pArgs->m_Pid = static_cast<DWORD>(_wtol(ppArguments[1]));
	pArgs->m_Archive = FullPath(ppArguments[2]);
	pArgs->m_InstallDirectory = FullPath(ppArguments[3]);
	pArgs->m_ClientExecutable = FullPath(ppArguments[4]);
	pArgs->m_NoLaunch = ArgumentCount >= 6 && _wcsicmp(ppArguments[5], L"--no-launch") == 0;
	LocalFree(ppArguments);
	const size_t ClientNameSeparator = pArgs->m_ClientExecutable.find_last_of(L"\\/");
	const bool ValidClientName = ClientNameSeparator != std::wstring::npos && _wcsicmp(pArgs->m_ClientExecutable.c_str() + ClientNameSeparator + 1, L"DDNet.exe") == 0;
	if(!DirectoryExists(pArgs->m_InstallDirectory) || !FileExists(pArgs->m_Archive) || !IsPathWithin(pArgs->m_InstallDirectory, pArgs->m_Archive) ||
		!IsPathWithin(pArgs->m_InstallDirectory, pArgs->m_ClientExecutable) || !ValidClientName)
	{
		MessageBoxW(nullptr, L"The updater received invalid paths.", L"AMF Client Updater", MB_ICONERROR);
		delete pArgs;
		DeleteCriticalSection(&g_StatusLock);
		return 1;
	}

	WNDCLASSEXW WindowClass{};
	WindowClass.cbSize = sizeof(WindowClass);
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.hInstance = Instance;
	WindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	WindowClass.lpszClassName = L"AMFClientUpdater";
	WindowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&WindowClass);
	const int X = (GetSystemMetrics(SM_CXSCREEN) - WINDOW_WIDTH) / 2;
	const int Y = (GetSystemMetrics(SM_CYSCREEN) - WINDOW_HEIGHT) / 2;
	g_hWindow = CreateWindowExW(WS_EX_APPWINDOW, WindowClass.lpszClassName, L"AMF Client Updater", WS_POPUP | WS_VISIBLE, X, Y, WINDOW_WIDTH, WINDOW_HEIGHT, nullptr, nullptr, Instance, nullptr);
	if(!g_hWindow)
	{
		delete pArgs;
		DeleteCriticalSection(&g_StatusLock);
		return 1;
	}
	HANDLE Thread = CreateThread(nullptr, 0, WorkerThread, pArgs, 0, nullptr);
	if(Thread)
		CloseHandle(Thread);
	else
	{
		delete pArgs;
		Fail(L"Failed to start updater worker.");
	}

	MSG Message;
	while(GetMessageW(&Message, nullptr, 0, 0))
	{
		TranslateMessage(&Message);
		DispatchMessageW(&Message);
	}
	DeleteCriticalSection(&g_StatusLock);
	return 0;
}
