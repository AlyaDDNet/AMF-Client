#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_POLICY_H

#include <base/hash.h>
#include <base/net.h>
#include <base/color.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace AmfPresence
{
constexpr int64_t PRESENCE_REFRESH_INTERVAL_MS = 90000;
constexpr int64_t PUBLISHED_CLAIM_LEASE_MS = PRESENCE_REFRESH_INTERVAL_MS * 3;

constexpr float BROWSER_BADGE_ICON_WIDTH = 12.0f;
constexpr ColorRGBA BROWSER_BADGE_COUNT_COLOR = ColorRGBA(1.0f, 0.9f, 0.55f, 1.0f);

struct SBrowserBadgeLayout
{
	bool m_ShowIcon = false;
	bool m_ShowCount = false;
	float m_IconX = 0.0f;
	float m_CountX = 0.0f;
	float m_CountWidth = 0.0f;
};

inline SBrowserBadgeLayout BrowserBadgeLayout(float ColumnWidth, float CountWidth, int Count)
{
	SBrowserBadgeLayout Layout;
	Layout.m_ShowIcon = Count > 0;
	Layout.m_ShowCount = Count > 1;
	if(!Layout.m_ShowIcon)
		return Layout;

	Layout.m_CountWidth = Layout.m_ShowCount ? std::max(0.0f, CountWidth) : 0.0f;
	Layout.m_IconX = std::max(0.0f, (ColumnWidth - BROWSER_BADGE_ICON_WIDTH) / 2.0f);
	// Like BestClient 3.0, draw the count centered over the cat badge rather
	// than allocating a separate horizontal slot beside it.
	Layout.m_CountX = std::max(0.0f, (ColumnWidth - Layout.m_CountWidth) / 2.0f);
	return Layout;
}

constexpr bool LocalConnectionIsPublishable(bool Online, bool ConnectionAvailable, int ClientId, int MaxClients)
{
	return Online && ConnectionAvailable && ClientId >= 0 && ClientId < MaxClients;
}

constexpr bool PublishedClaimLeaseIsValid(int64_t Now, int64_t LastConfirmed, int64_t Frequency)
{
	return Frequency > 0 && Now >= LastConfirmed && Now - LastConfirmed < Frequency * PUBLISHED_CLAIM_LEASE_MS / 1000;
}

constexpr bool KeepConfirmedClaim(bool SameServer, bool SameSlotIdentity, bool PresenceIdentityCompatible, bool ExplicitlyDisconnected, bool LeaseValid)
{
	return SameServer && SameSlotIdentity && PresenceIdentityCompatible && !ExplicitlyDisconnected && LeaseValid;
}

// A browser open starts a new snapshot session. The request is consumed by the
// transport only after the current local presence update has been acknowledged.
constexpr bool RequestBrowserSnapshotOnOpen(bool WasEnabled, bool Enabled, bool LocalPresenceRequired)
{
	return Enabled && !WasEnabled && LocalPresenceRequired;
}

// BestClient keys live presence by the connected game endpoint. Using the
// resolved host:port also keeps AMF's existing endpoint-v1 room stable when
// server info, map name, or map CRC changes after the socket is established.
inline std::string RoomIdForServer(const NETADDR &Address)
{
	char aAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Address, aAddress, sizeof(aAddress), true);
	const std::string RoomSource = std::string("endpoint-v1|") + aAddress;
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(RoomSource.data(), RoomSource.size()), aHash, sizeof(aHash));
	return std::string("ddnet:") + aHash;
}

}

#endif
