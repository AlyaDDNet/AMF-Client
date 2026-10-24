// One-shot crash cleanup helper for AMF process-priority settings.
// One instance belongs to one DDNet session. DDNet publishes the identities of
// processes it lowered before changing priority; this helper waits until that
// DDNet process exits or explicitly cancels it.

#ifdef NOGDI
#undef NOGDI
#endif
#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>

#include "amf_priority_restore_protocol.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <limits>

#pragma comment(lib, "shell32.lib")

namespace
{
uint64_t FileTimeValue(const FILETIME &Time)
{
	ULARGE_INTEGER Value{};
	Value.LowPart = Time.dwLowDateTime;
	Value.HighPart = Time.dwHighDateTime;
	return Value.QuadPart;
}

bool HasExpectedCreationTime(HANDLE Process, uint64_t ExpectedCreationTime)
{
	FILETIME Creation{}, Exit{}, Kernel{}, User{};
	return GetProcessTimes(Process, &Creation, &Exit, &Kernel, &User) && FileTimeValue(Creation) == ExpectedCreationTime;
}

bool HasExpectedPackageFamily(HANDLE Process, const wchar_t *pExpectedFamily)
{
	wchar_t aPackageFamily[PACKAGE_FAMILY_NAME_MAX_LENGTH] = {};
	UINT32 PackageFamilyLength = PACKAGE_FAMILY_NAME_MAX_LENGTH;
	return GetPackageFamilyName(Process, &PackageFamilyLength, aPackageFamily) == ERROR_SUCCESS &&
		_wcsicmp(aPackageFamily, pExpectedFamily) == 0;
}

bool IsExpectedAmfPriorityProcess(HANDLE Process, const AmfPriorityRestore::CProcessIdentity &Identity)
{
	if(!HasExpectedCreationTime(Process, Identity.m_CreationTime))
		return false;

	wchar_t aImagePath[32768] = {};
	DWORD ImagePathLength = sizeof(aImagePath) / sizeof(aImagePath[0]);
	if(!QueryFullProcessImageNameW(Process, 0, aImagePath, &ImagePathLength))
		return false;
	const wchar_t *pFileName = wcsrchr(aImagePath, L'\\');
	if(!pFileName)
		pFileName = wcsrchr(aImagePath, L'/');
	if(!pFileName)
		return false;
	++pFileName;
	switch(Identity.m_Target)
	{
	case AmfPriorityRestore::TARGET_DISCORD:
		return _wcsicmp(pFileName, L"Discord.exe") == 0;
	case AmfPriorityRestore::TARGET_SPOTIFY:
		return _wcsicmp(pFileName, L"Spotify.exe") == 0;
	case AmfPriorityRestore::TARGET_SOUNDCLOUD:
		return _wcsicmp(pFileName, L"soundcouch.exe") == 0 &&
			HasExpectedPackageFamily(Process, L"SoundcloudLtd.SoundCloud-MusicAudio_2xc63xn306dnw");
	default:
		return false;
	}
}

bool ParseUnsigned(const wchar_t *pString, uint64_t &Value)
{
	if(!pString || !pString[0] || pString[0] == L'-')
		return false;
	errno = 0;
	wchar_t *pEnd = nullptr;
	const unsigned long long Parsed = wcstoull(pString, &pEnd, 10);
	if(errno == ERANGE || !pEnd || *pEnd != L'\0')
		return false;
	Value = static_cast<uint64_t>(Parsed);
	return true;
}

bool ParseProcessId(const wchar_t *pString, DWORD &ProcessId)
{
	uint64_t Parsed = 0;
	return ParseUnsigned(pString, Parsed) && Parsed != 0 && Parsed <= std::numeric_limits<DWORD>::max() && (ProcessId = static_cast<DWORD>(Parsed), true);
}

bool IsRestoreObjectName(const wchar_t *pName)
{
	static constexpr const wchar_t *PREFIX = L"Local\\AMFPriorityRestore-";
	return pName && wcsncmp(pName, PREFIX, wcslen(PREFIX)) == 0;
}

void RestoreTrackedProcesses(const AmfPriorityRestore::CSharedState *pState)
{
	// DDNet only flips this index after completing the inactive list. Once the
	// parent process is signaled there can be no concurrent writer anymore.
	const std::int32_t ActiveList = pState->m_ActiveList;
	if(ActiveList < 0 || ActiveList > 1)
		return;
	const auto &List = pState->m_aLists[ActiveList];
	if(List.m_Count > AmfPriorityRestore::MAX_PROCESSES)
		return;

	for(std::uint32_t Index = 0; Index < List.m_Count; ++Index)
	{
		const auto &Identity = List.m_aProcesses[Index];
		if(Identity.m_ProcessId == 0 || Identity.m_CreationTime == 0 ||
			(Identity.m_Target != AmfPriorityRestore::TARGET_DISCORD &&
				Identity.m_Target != AmfPriorityRestore::TARGET_SPOTIFY &&
				Identity.m_Target != AmfPriorityRestore::TARGET_SOUNDCLOUD))
			continue;
		const HANDLE TargetProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, Identity.m_ProcessId);
		if(!TargetProcess)
			continue;
		if(IsExpectedAmfPriorityProcess(TargetProcess, Identity) &&
			AmfPriorityRestore::ShouldRestorePriority(true, GetPriorityClass(TargetProcess), BELOW_NORMAL_PRIORITY_CLASS))
			SetPriorityClass(TargetProcess, NORMAL_PRIORITY_CLASS);
		CloseHandle(TargetProcess);
	}
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int ArgumentCount = 0;
	LPWSTR *ppArguments = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
	if(!ppArguments || ArgumentCount != 6)
	{
		if(ppArguments)
			LocalFree(ppArguments);
		return 1;
	}

	DWORD ParentProcessId = 0;
	uint64_t ParentCreationTime = 0;
	const bool ValidArguments = ParseProcessId(ppArguments[1], ParentProcessId) &&
		ParseUnsigned(ppArguments[2], ParentCreationTime) &&
		IsRestoreObjectName(ppArguments[3]) && IsRestoreObjectName(ppArguments[4]) && IsRestoreObjectName(ppArguments[5]);
	if(!ValidArguments)
	{
		LocalFree(ppArguments);
		return 1;
	}

	const HANDLE ParentProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ParentProcessId);
	if(!ParentProcess || !HasExpectedCreationTime(ParentProcess, ParentCreationTime))
	{
		if(ParentProcess)
			CloseHandle(ParentProcess);
		LocalFree(ppArguments);
		return 0;
	}
	const HANDLE StateMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, ppArguments[3]);
	const HANDLE CancelEvent = OpenEventW(SYNCHRONIZE, FALSE, ppArguments[4]);
	const HANDLE ReadyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, ppArguments[5]);
	LocalFree(ppArguments);
	if(!StateMapping || !CancelEvent || !ReadyEvent)
	{
		if(StateMapping)
			CloseHandle(StateMapping);
		if(CancelEvent)
			CloseHandle(CancelEvent);
		if(ReadyEvent)
			CloseHandle(ReadyEvent);
		CloseHandle(ParentProcess);
		return 0;
	}
	const auto *pState = static_cast<const AmfPriorityRestore::CSharedState *>(MapViewOfFile(StateMapping, FILE_MAP_READ, 0, 0, sizeof(AmfPriorityRestore::CSharedState)));
	if(!pState || pState->m_Magic != AmfPriorityRestore::PROTOCOL_MAGIC || pState->m_Version != AmfPriorityRestore::PROTOCOL_VERSION)
	{
		if(pState)
			UnmapViewOfFile(pState);
		CloseHandle(ReadyEvent);
		CloseHandle(CancelEvent);
		CloseHandle(StateMapping);
		CloseHandle(ParentProcess);
		return 0;
	}

	SetEvent(ReadyEvent);
	CloseHandle(ReadyEvent);
	const HANDLE aWaitHandles[] = {ParentProcess, CancelEvent};
	const DWORD WaitResult = WaitForMultipleObjects(2, aWaitHandles, FALSE, INFINITE);
	if(WaitResult == WAIT_OBJECT_0)
		RestoreTrackedProcesses(pState);
	UnmapViewOfFile(pState);
	CloseHandle(CancelEvent);
	CloseHandle(StateMapping);
	CloseHandle(ParentProcess);
	return 0;
}
