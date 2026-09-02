// Shared, fixed-size state used by DDNet and the one-shot Discord priority
// recovery helper. It deliberately contains only process identities, never
// handles or user data.

#ifndef OTHER_HELPERS_AMF_DISCORD_PRIORITY_RESTORE_PROTOCOL_H
#define OTHER_HELPERS_AMF_DISCORD_PRIORITY_RESTORE_PROTOCOL_H

#include <cstdint>

namespace AmfDiscordPriorityRestore
{
constexpr std::uint32_t PROTOCOL_MAGIC = 0x414d4650; // "AMFP"
constexpr std::uint32_t PROTOCOL_VERSION = 1;
constexpr std::uint32_t MAX_PROCESSES = 64;

#pragma pack(push, 8)
struct CProcessIdentity
{
	std::uint32_t m_ProcessId;
	std::uint32_t m_Reserved;
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

static_assert(sizeof(CProcessIdentity) == 16, "Discord priority restore protocol layout changed");
}

#endif
