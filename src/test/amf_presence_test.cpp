#include <game/client/components/tclient/amf_presence_policy.h>

#include <gtest/gtest.h>

#include <initializer_list>

TEST(AmfPresence, BrowserSnapshotRequestedWhenBrowserOpens)
{
	EXPECT_TRUE(AmfPresence::RequestBrowserSnapshotOnOpen(false, true, true));
	EXPECT_FALSE(AmfPresence::RequestBrowserSnapshotOnOpen(false, true, false));
	EXPECT_FALSE(AmfPresence::RequestBrowserSnapshotOnOpen(true, true, true));
	EXPECT_FALSE(AmfPresence::RequestBrowserSnapshotOnOpen(false, false, true));
}

TEST(AmfPresence, RoomIdentityUsesStableCanonicalEndpoint)
{
	NETADDR First{};
	NETADDR Same{};
	NETADDR OtherPort{};
	ASSERT_EQ(net_addr_from_str(&First, "127.0.0.1:8303"), 0);
	ASSERT_EQ(net_addr_from_str(&Same, "127.0.0.1:8303"), 0);
	ASSERT_EQ(net_addr_from_str(&OtherPort, "127.0.0.1:8304"), 0);
	EXPECT_EQ(AmfPresence::RoomIdForServer(First), AmfPresence::RoomIdForServer(Same));
	EXPECT_NE(AmfPresence::RoomIdForServer(First), AmfPresence::RoomIdForServer(OtherPort));
}

TEST(AmfPresence, BrowserBadgeShowsCatAndCompactCount)
{
	EXPECT_EQ(AmfPresence::BROWSER_BADGE_COUNT_COLOR, ColorRGBA(1.0f, 0.9f, 0.55f, 1.0f));
	constexpr float ColumnWidth = 38.0f;
	const auto NoUsers = AmfPresence::BrowserBadgeLayout(ColumnWidth, 0.0f, 0);
	EXPECT_FALSE(NoUsers.m_ShowIcon);
	EXPECT_FALSE(NoUsers.m_ShowCount);

	const auto OneUser = AmfPresence::BrowserBadgeLayout(ColumnWidth, 5.0f, 1);
	EXPECT_TRUE(OneUser.m_ShowIcon);
	EXPECT_FALSE(OneUser.m_ShowCount);
	EXPECT_FLOAT_EQ(OneUser.m_IconX, (ColumnWidth - AmfPresence::BROWSER_BADGE_ICON_WIDTH) / 2.0f);

	// Backend count is already installation/user-level (main + dummy = one).
	// Verify the visual layout for the requested boundary values without adding
	// a client-side +1 or counting connections in the UI.
	for(const int Count : {2, 3, 9, 10, 25})
	{
		const float TextWidth = Count < 10 ? 5.5f : 11.0f;
		const auto Layout = AmfPresence::BrowserBadgeLayout(ColumnWidth, TextWidth, Count);
		EXPECT_TRUE(Layout.m_ShowIcon) << "count=" << Count;
		EXPECT_TRUE(Layout.m_ShowCount) << "count=" << Count;
		EXPECT_GE(Layout.m_IconX, 0.0f) << "count=" << Count;
		const float IconCenter = Layout.m_IconX + AmfPresence::BROWSER_BADGE_ICON_WIDTH / 2.0f;
		const float CountCenter = Layout.m_CountX + Layout.m_CountWidth / 2.0f;
		EXPECT_FLOAT_EQ(CountCenter, IconCenter) << "count=" << Count;
		EXPECT_LE(Layout.m_CountWidth, AmfPresence::BROWSER_BADGE_ICON_WIDTH) << "count=" << Count;
	}
}

TEST(AmfPresence, LocalMainAndConnectedDummyArePublishedWithoutSnapshotActivity)
{
	constexpr int MaxClients = 64;
	// The production predicate intentionally has no m_Active/snapshot argument:
	// stable local connection IDs define local participation through transient
	// PLAYERINFO gaps.
	EXPECT_TRUE(AmfPresence::LocalConnectionIsPublishable(true, true, 0, MaxClients));
	EXPECT_TRUE(AmfPresence::LocalConnectionIsPublishable(true, true, 1, MaxClients));
	EXPECT_FALSE(AmfPresence::LocalConnectionIsPublishable(false, true, 0, MaxClients));
	EXPECT_FALSE(AmfPresence::LocalConnectionIsPublishable(true, false, 1, MaxClients));
	EXPECT_FALSE(AmfPresence::LocalConnectionIsPublishable(true, true, -1, MaxClients));
	EXPECT_FALSE(AmfPresence::LocalConnectionIsPublishable(true, true, MaxClients, MaxClients));
}

TEST(AmfPresence, TransientMissingPresenceKeepsConfirmedClaimWithinLease)
{
	EXPECT_TRUE(AmfPresence::KeepConfirmedClaim(true, true, true, false, true)); // complete snapshot omitted it
	EXPECT_TRUE(AmfPresence::KeepConfirmedClaim(true, true, true, false, true)); // following snapshot still resolves same slot
	EXPECT_TRUE(AmfPresence::KeepConfirmedClaim(true, true, true, false, true)); // websocket reconnect gap
	EXPECT_FALSE(AmfPresence::KeepConfirmedClaim(true, true, true, false, false)); // bounded lease expired
}

TEST(AmfPresence, DefinitiveInvalidationsRemoveOnlyTheOldIdentity)
{
	EXPECT_FALSE(AmfPresence::KeepConfirmedClaim(true, true, true, true, true)); // explicit disconnect
	EXPECT_FALSE(AmfPresence::KeepConfirmedClaim(true, false, true, false, true)); // slot reused by another name/generation
	EXPECT_FALSE(AmfPresence::KeepConfirmedClaim(false, true, true, false, true)); // server/port changed
	EXPECT_FALSE(AmfPresence::KeepConfirmedClaim(true, true, false, false, true)); // presence ID now belongs to another installation
	EXPECT_TRUE(AmfPresence::KeepConfirmedClaim(true, true, true, false, true));
}

TEST(AmfPresence, PublishedClaimLeaseHasBoundedLifetime)
{
	constexpr int64_t Frequency = 1000;
	constexpr int64_t LastConfirmed = 5000;
	EXPECT_TRUE(AmfPresence::PublishedClaimLeaseIsValid(LastConfirmed, LastConfirmed, Frequency));
	EXPECT_TRUE(AmfPresence::PublishedClaimLeaseIsValid(LastConfirmed + AmfPresence::PUBLISHED_CLAIM_LEASE_MS - 1, LastConfirmed, Frequency));
	EXPECT_FALSE(AmfPresence::PublishedClaimLeaseIsValid(LastConfirmed + AmfPresence::PUBLISHED_CLAIM_LEASE_MS, LastConfirmed, Frequency));
	EXPECT_FALSE(AmfPresence::PublishedClaimLeaseIsValid(LastConfirmed - 1, LastConfirmed, Frequency));
}

TEST(AmfPresence, BrowserBadgeUsesAuthoritativeCountWithoutClientAddition)
{
	// The UI consumes the backend's unique-installation count unchanged: it
	// doesn't infer connection count or add the local player a second time.
	// Canonical totals 1 (main+dummy), 2 (local+remote) and 4 (local+three remote)
	// therefore remain represented by those same backend counts.
	EXPECT_FALSE(AmfPresence::BrowserBadgeLayout(38.0f, 5.0f, 1).m_ShowCount); // main + dummy = one user
	EXPECT_TRUE(AmfPresence::BrowserBadgeLayout(38.0f, 5.5f, 2).m_ShowCount); // local + one remote
	EXPECT_TRUE(AmfPresence::BrowserBadgeLayout(38.0f, 5.5f, 4).m_ShowCount); // local + three remote
}
