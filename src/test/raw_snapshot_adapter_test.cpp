#include <game/client/raw_snapshot_adapter.h>

#include <gtest/gtest.h>

namespace
{
CNetObj_Character MakeCharacter(int Tick, int X, int Y, int VelX, int VelY)
{
	CNetObj_Character Character{};
	Character.m_Tick = Tick;
	Character.m_X = X;
	Character.m_Y = Y;
	Character.m_VelX = VelX;
	Character.m_VelY = VelY;
	Character.m_Weapon = WEAPON_HAMMER;
	return Character;
}

void CaptureCharacter(CRawAuthoritativeSnapshotAdapter &Adapter, int Connection, int64_t ServerTick,
	int ClientId, const CNetObj_Character &Current, const CNetObj_Character *pPrevious = nullptr,
	bool DummySwapped = false, int TeamId = TEAM_RED)
{
	Adapter.BeginSnapshot(Connection, DummySwapped, ServerTick, ServerTick - 1);
	Adapter.ObservePlayerInfo(ClientId, TeamId, false);
	Adapter.ObserveCharacter(ClientId, Current, pPrevious, TeamId);
}
} // namespace

TEST(RawSnapshotAdapter, FirstObservationIsCurrentAndHasNoAdapterPrevious)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(100, 1200, -300, 17, -23);
	CaptureCharacter(Adapter, 0, 101, 4, Character);

	const auto &Current = Adapter.Current()[4];
	EXPECT_TRUE(Current.m_Valid);
	EXPECT_TRUE(Current.m_FromRawCurrentSnapshot);
	EXPECT_FALSE(Current.m_FromRawPreviousSnapshot);
	EXPECT_EQ(Current.m_ClientId, 4);
	EXPECT_EQ(Current.m_SnapshotSequence, 1u);
	EXPECT_EQ(Current.m_SnapshotServerTick, 101);
	EXPECT_EQ(Current.m_ObjectTick, 100);
	EXPECT_EQ(Current.m_Character.m_X, 1200);
	EXPECT_EQ(Current.m_Character.m_Y, -300);
	EXPECT_EQ(Current.m_Character.m_VelX, 17);
	EXPECT_EQ(Current.m_Character.m_VelY, -23);
	EXPECT_FALSE(Adapter.Previous()[4].m_Valid);
}

TEST(RawSnapshotAdapter, SecondObservationUsesSnapPreviousObjectAndTracksSequenceContinuity)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character First = MakeCharacter(200, 10, 20, 30, 40);
	const CNetObj_Character Second = MakeCharacter(201, 11, 22, 31, 42);
	CaptureCharacter(Adapter, 0, 201, 7, First);
	CaptureCharacter(Adapter, 0, 202, 7, Second, &First);

	const auto &Previous = Adapter.Previous()[7];
	const auto &Current = Adapter.Current()[7];
	EXPECT_TRUE(Previous.m_Valid);
	EXPECT_TRUE(Previous.m_FromRawPreviousSnapshot);
	EXPECT_TRUE(Previous.m_SequenceKnown);
	EXPECT_EQ(Previous.m_SnapshotSequence, 1u);
	EXPECT_EQ(Previous.m_SnapshotServerTick, 201);
	EXPECT_EQ(Previous.m_ObjectTick, 200);
	EXPECT_EQ(Previous.m_Character.m_X, 10);
	EXPECT_EQ(Current.m_SnapshotSequence, 2u);
	EXPECT_EQ(Current.m_ObjectTick, 201);
	EXPECT_EQ(Current.m_Character.m_X, 11);
}

TEST(RawSnapshotAdapter, SameClientIdOnConsecutiveTicksKeepsRawVelocityAndPosition)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character First = MakeCharacter(300, 100, 200, -5, 6);
	const CNetObj_Character Second = MakeCharacter(301, 95, 206, -5, 6);
	CaptureCharacter(Adapter, 0, 301, 12, First);
	CaptureCharacter(Adapter, 0, 302, 12, Second, &First);

	EXPECT_EQ(Adapter.Previous()[12].m_Character.m_X, 100);
	EXPECT_EQ(Adapter.Previous()[12].m_Character.m_VelX, -5);
	EXPECT_EQ(Adapter.Current()[12].m_Character.m_X, 95);
	EXPECT_EQ(Adapter.Current()[12].m_Character.m_VelY, 6);
}

TEST(RawSnapshotAdapter, MissingClientIdClearsCurrentButCarriesLastObservationAsPrevious)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(400, 1, 2, 3, 4);
	CaptureCharacter(Adapter, 0, 401, 9, Character);
	Adapter.BeginSnapshot(0, false, 402, 401);

	EXPECT_FALSE(Adapter.Current()[9].m_Valid);
	EXPECT_FALSE(Adapter.Current()[9].m_Present);
	EXPECT_TRUE(Adapter.Previous()[9].m_Valid);
	EXPECT_TRUE(Adapter.Previous()[9].m_FromRawCurrentSnapshot);
}

TEST(RawSnapshotAdapter, ReturningClientWithoutSnapPreviousHasNoFabricatedPrevious)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character First = MakeCharacter(500, 10, 20, 0, 0);
	const CNetObj_Character Returned = MakeCharacter(503, 90, 80, 1, -1);
	CaptureCharacter(Adapter, 0, 501, 10, First);
	Adapter.BeginSnapshot(0, false, 502, 501); // absent snapshot
	Adapter.BeginSnapshot(0, false, 503, 502);
	Adapter.ObserveCharacter(10, Returned, nullptr, TEAM_RED); // slot returned, SNAP_PREV absent

	EXPECT_TRUE(Adapter.Current()[10].m_Valid);
	EXPECT_FALSE(Adapter.Previous()[10].m_Valid);
	EXPECT_EQ(Adapter.Current()[10].m_ObjectTick, 503);
}

TEST(RawSnapshotAdapter, ClientId64IsWithinTheNativeClientRange)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(600, 64, 0, 0, 0);
	CaptureCharacter(Adapter, 0, 601, 64, Character);

	EXPECT_TRUE(Adapter.Current()[64].m_Valid);
	EXPECT_EQ(Adapter.Current()[64].m_ClientId, 64);
}

TEST(RawSnapshotAdapter, MaximumSupportedClientIdIsCapturedWithoutSixtyFourSlotAssumptions)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const int ClientId = MAX_CLIENTS - 1;
	const CNetObj_Character Character = MakeCharacter(700, 127, 1, 2, 3);
	CaptureCharacter(Adapter, 0, 701, ClientId, Character);

	EXPECT_EQ(MAX_CLIENTS, 128);
	EXPECT_TRUE(Adapter.Current()[ClientId].m_Valid);
	EXPECT_EQ(Adapter.Current()[ClientId].m_ClientId, ClientId);
}

TEST(RawSnapshotAdapter, MainConnectionIsExplicitlyRecorded)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(800, 0, 0, 0, 0);
	CaptureCharacter(Adapter, 0, 801, 3, Character);

	EXPECT_EQ(Adapter.CurrentContext().m_Connection, 0);
	EXPECT_EQ(Adapter.Current()[3].m_SourceConnection, 0);
	EXPECT_EQ(Adapter.Current()[3].m_SequenceConnection, 0);
}

TEST(RawSnapshotAdapter, DummyConnectionIsTaggedWithoutCreatingASecondLegacySeries)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Main = MakeCharacter(900, 10, 0, 0, 0);
	const CNetObj_Character Dummy = MakeCharacter(900, 20, 0, 0, 0);
	CaptureCharacter(Adapter, 0, 901, 2, Main);
	CaptureCharacter(Adapter, 1, 901, 2, Dummy, &Main, true);

	EXPECT_EQ(Adapter.CurrentContext().m_Connection, 1);
	EXPECT_TRUE(Adapter.CurrentContext().m_DummySwapped);
	EXPECT_EQ(Adapter.Current()[2].m_SourceConnection, 1);
	EXPECT_EQ(Adapter.Previous()[2].m_SourceConnection, 1);
	EXPECT_TRUE(Adapter.Previous()[2].m_SequenceKnown);
	EXPECT_EQ(Adapter.Previous()[2].m_SequenceConnection, 0);
	EXPECT_EQ(Adapter.Current()[2].m_Character.m_X, 20);
}

TEST(RawSnapshotAdapter, DummySwapReprocessingSameTickStillAdvancesLegacySequence)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1000, 5, 6, 7, 8);
	CaptureCharacter(Adapter, 0, 1001, 1, Character);
	CaptureCharacter(Adapter, 0, 1001, 1, Character, &Character, true);

	EXPECT_EQ(Adapter.Sequence(), 2u);
	EXPECT_EQ(Adapter.CurrentContext().m_Sequence, 2u);
	EXPECT_TRUE(Adapter.CurrentContext().m_DummySwapped);
	EXPECT_EQ(Adapter.Current()[1].m_SnapshotSequence, 2u);
}

TEST(RawSnapshotAdapter, DummyDisconnectDoesNotResetGlobalSeriesWhenSlotIsAbsent)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character DummyCharacter = MakeCharacter(1050, 50, 0, 0, 0);
	CaptureCharacter(Adapter, 1, 1051, 5, DummyCharacter);

	// 19.9 OnDummyDisconnect changes routing/local-id state but does not reset
	// these arrays. If the next main snapshot has no character for the slot,
	// BeginSnapshot shifts the dummy capture into previous and leaves current empty.
	Adapter.BeginSnapshot(0, false, 1052, 1051);

	EXPECT_EQ(Adapter.Previous()[5].m_SourceConnection, 1);
	EXPECT_EQ(Adapter.Previous()[5].m_Character.m_X, 50);
	EXPECT_TRUE(Adapter.Previous()[5].m_Valid);
	EXPECT_FALSE(Adapter.Current()[5].m_Valid);
	EXPECT_EQ(Adapter.CurrentContext().m_Connection, 0);
}

TEST(RawSnapshotAdapter, SlotReusePreservesRawSnapPreviousWithoutInventingGeneration)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character OldSlotUser = MakeCharacter(1100, 100, 200, 1, 2);
	const CNetObj_Character NewSlotUser = MakeCharacter(2, 900, 800, -3, -4);
	CaptureCharacter(Adapter, 0, 1101, 6, OldSlotUser);
	CaptureCharacter(Adapter, 0, 1102, 6, NewSlotUser, &OldSlotUser);

	EXPECT_EQ(Adapter.Current()[6].m_ClientId, 6);
	EXPECT_EQ(Adapter.Current()[6].m_ObjectTick, 2);
	EXPECT_EQ(Adapter.Current()[6].m_Character.m_X, 900);
	EXPECT_EQ(Adapter.Previous()[6].m_ObjectTick, 1100);
	EXPECT_TRUE(Adapter.Previous()[6].m_SequenceKnown);
}

TEST(RawSnapshotAdapter, ExtendedFreezeEvidenceIsCopiedFromCurrentAndPreviousObjects)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	CNetObj_Character First = MakeCharacter(1200, 0, 0, 0, 0);
	CNetObj_Character Second = MakeCharacter(1201, 1, 0, 1, 0);
	CNetObj_DDNetCharacter FirstExtended{};
	FirstExtended.m_Flags = 0x20;
	FirstExtended.m_FreezeEnd = 1210;
	CNetObj_DDNetCharacter SecondExtended{};
	SecondExtended.m_Flags = 0x40;
	SecondExtended.m_FreezeEnd = 1205;

	CaptureCharacter(Adapter, 0, 1201, 8, First);
	Adapter.ObserveExtendedCharacter(8, FirstExtended, nullptr);
	CaptureCharacter(Adapter, 0, 1202, 8, Second, &First);
	Adapter.ObserveExtendedCharacter(8, SecondExtended, &FirstExtended);

	EXPECT_TRUE(Adapter.Current()[8].m_HasExtendedData);
	EXPECT_EQ(Adapter.Current()[8].m_ExtendedData.m_Flags, 0x40);
	EXPECT_EQ(Adapter.Current()[8].m_ExtendedData.m_FreezeEnd, 1205);
	EXPECT_TRUE(Adapter.Previous()[8].m_HasExtendedData);
	EXPECT_EQ(Adapter.Previous()[8].m_ExtendedData.m_Flags, 0x20);
	EXPECT_EQ(Adapter.Previous()[8].m_ExtendedData.m_FreezeEnd, 1210);
}

TEST(RawSnapshotAdapter, SpectatorTeamSetsLegacyAliveObservationFalse)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1300, 1, 2, 3, 4);
	CaptureCharacter(Adapter, 0, 1301, 11, Character, nullptr, false, TEAM_SPECTATORS);

	EXPECT_TRUE(Adapter.Current()[11].m_Valid);
	EXPECT_TRUE(Adapter.Current()[11].m_Present);
	EXPECT_FALSE(Adapter.Current()[11].m_Alive);
	EXPECT_EQ(Adapter.Current()[11].m_TeamId, TEAM_SPECTATORS);
}

TEST(RawSnapshotAdapter, CurrentContextCarriesLocalClientAndConnectionIdentity)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1400, 0, 0, 0, 0);
	Adapter.BeginSnapshot(1, false, 1401, 1400);
	Adapter.ObservePlayerInfo(13, TEAM_RED, true);
	Adapter.ObserveCharacter(13, Character, nullptr, TEAM_RED);

	EXPECT_EQ(Adapter.CurrentContext().m_LocalClientId, 13);
	EXPECT_EQ(Adapter.CurrentContext().m_Connection, 1);
	EXPECT_EQ(Adapter.Current()[13].m_SourceConnection, 1);
}

TEST(RawSnapshotAdapter, DemoPlaybackDoesNotClaimALiveNetworkConnection)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1450, 0, 0, 0, 0);
	Adapter.BeginSnapshot(0, false, 1451, 1450, true);
	Adapter.ObservePlayerInfo(15, TEAM_RED, true);
	Adapter.ObserveCharacter(15, Character, nullptr, TEAM_RED);

	EXPECT_TRUE(Adapter.CurrentContext().m_DemoPlayback);
	EXPECT_EQ(Adapter.CurrentContext().m_Connection, -1);
	EXPECT_EQ(Adapter.Current()[15].m_SourceConnection, -1);
}

TEST(RawSnapshotAdapter, ResetClearsObservationsAndSequenceAtMapOrServerBoundary)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1500, 1, 1, 1, 1);
	CaptureCharacter(Adapter, 0, 1501, 1, Character);
	Adapter.Reset();

	EXPECT_EQ(Adapter.Sequence(), 0u);
	EXPECT_FALSE(Adapter.CurrentContext().m_Valid);
	EXPECT_FALSE(Adapter.Current()[1].m_Valid);
	EXPECT_FALSE(Adapter.Previous()[1].m_Valid);
	CaptureCharacter(Adapter, 0, 1, 1, MakeCharacter(1, 2, 2, 2, 2));
	EXPECT_EQ(Adapter.CurrentContext().m_Sequence, 1u);
}

TEST(RawSnapshotAdapter, InvalidClientIdsAreIgnored)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Character = MakeCharacter(1600, 1, 2, 3, 4);
	Adapter.BeginSnapshot(0, false, 1601, 1600);
	Adapter.ObservePlayerInfo(-1, TEAM_RED, false);
	Adapter.ObserveCharacter(-1, Character, nullptr, TEAM_RED);
	Adapter.ObserveCharacter(MAX_CLIENTS, Character, nullptr, TEAM_RED);

	EXPECT_EQ(Adapter.CurrentContext().m_LocalClientId, -1);
	for(const auto &Observation : Adapter.Current())
		EXPECT_FALSE(Observation.m_Valid);
}

TEST(RawSnapshotAdapter, LegacyGoldenSequenceMatchesNormalizedRawContract)
{
	CRawAuthoritativeSnapshotAdapter Adapter;
	const CNetObj_Character Before = MakeCharacter(1700, -70, 80, 9, -10);
	const CNetObj_Character After = MakeCharacter(1701, -61, 70, 9, -10);
	CaptureCharacter(Adapter, 0, 1701, 14, Before);
	CaptureCharacter(Adapter, 0, 1702, 14, After, &Before);

	const auto &Previous = Adapter.Previous()[14];
	const auto &Current = Adapter.Current()[14];
	EXPECT_TRUE(Previous.m_Valid);
	EXPECT_TRUE(Previous.m_FromRawPreviousSnapshot);
	EXPECT_TRUE(Previous.m_SequenceKnown);
	EXPECT_EQ(Previous.m_ClientId, 14);
	EXPECT_EQ(Previous.m_SnapshotSequence, 1u);
	EXPECT_EQ(Previous.m_SnapshotServerTick, 1701);
	EXPECT_EQ(Previous.m_ObjectTick, 1700);
	EXPECT_EQ(Previous.m_Character.m_X, -70);
	EXPECT_EQ(Previous.m_Character.m_VelY, -10);
	EXPECT_EQ(Current.m_SnapshotSequence, 2u);
	EXPECT_EQ(Current.m_SnapshotServerTick, 1702);
	EXPECT_EQ(Current.m_ObjectTick, 1701);
	EXPECT_EQ(Current.m_Character.m_X, -61);
	EXPECT_EQ(Current.m_Character.m_VelX, 9);
}
