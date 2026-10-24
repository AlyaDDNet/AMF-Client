// Shared, fixed-size state used by DDNet and the single AMF process-priority
// recovery helper. It deliberately contains only process identities, never
// handles or user data.

#ifndef OTHER_HELPERS_AMF_PRIORITY_RESTORE_PROTOCOL_H
#define OTHER_HELPERS_AMF_PRIORITY_RESTORE_PROTOCOL_H

#include <cstdint>

namespace AmfPriorityRestore
{
constexpr std::uint32_t PROTOCOL_MAGIC = 0x414d4650; // "AMFP"
constexpr std::uint32_t PROTOCOL_VERSION = 2;
constexpr std::uint32_t MAX_PROCESSES = 64;
constexpr std::uint32_t TARGET_DISCORD = 1;
constexpr std::uint32_t TARGET_SPOTIFY = 2;
constexpr std::uint32_t TARGET_SOUNDCLOUD = 4;

constexpr std::uint32_t RequestedTargets(bool DiscordEnabled, bool SpotifySoundCloudEnabled)
{
	return (DiscordEnabled ? TARGET_DISCORD : 0) |
		(SpotifySoundCloudEnabled ? TARGET_SPOTIFY | TARGET_SOUNDCLOUD : 0);
}

constexpr bool IsTargetRequested(std::uint32_t Targets, std::uint32_t Target)
{
	return (Targets & Target) != 0;
}

constexpr bool ShouldApplyPriority(std::uint32_t CurrentPriority, std::uint32_t RequestedPriority)
{
	return CurrentPriority != RequestedPriority;
}

constexpr bool ShouldRestorePriority(bool ChangedByAmf, std::uint32_t CurrentPriority, std::uint32_t AppliedPriority)
{
	return ChangedByAmf && CurrentPriority == AppliedPriority;
}

#pragma pack(push, 8)
struct CProcessIdentity
{
	std::uint32_t m_ProcessId;
	std::uint32_t m_Target;
	std::uint64_t m_CreationTime;
};

struct CProcessList
{
	std::uint32_t m_Count;
	std::uint32_t m_Reserved;
	CProcessIdentity m_aProcesses[MAX_PROCESSES];
};

struct CSharedState
{
	std::uint32_t m_Magic;
	std::uint32_t m_Version;
	// DDNet writes the inactive list completely, then atomically flips this
	// index. If it crashes during a write the helper still has the previous,
	// complete list to restore.
	volatile std::int32_t m_ActiveList;
	std::uint32_t m_Reserved;
	CProcessList m_aLists[2];
};
#pragma pack(pop)

static_assert(sizeof(CProcessIdentity) == 16, "AMF priority restore protocol layout changed");
}

#endif
