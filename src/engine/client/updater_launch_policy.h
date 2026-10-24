#ifndef ENGINE_CLIENT_UPDATER_LAUNCH_POLICY_H
#define ENGINE_CLIENT_UPDATER_LAUNCH_POLICY_H

#include <array>

namespace UpdaterLaunchPolicy
{
inline bool ShouldLaunchUpdateTransaction(bool UpdateReady, bool AlreadyLaunched)
{
	return UpdateReady && !AlreadyLaunched;
}

inline std::array<const char *, 4> MakeUpdateTransactionArguments(const char *pPid, const char *pArchive, const char *pInstallDirectory, const char *pClientExecutable)
{
	return {pPid, pArchive, pInstallDirectory, pClientExecutable};
}
}

#endif
