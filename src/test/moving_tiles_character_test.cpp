#include <base/vmath.h>

#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/tclient/moving_tiles_collision.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/layers.h>
#include <game/mapbugs.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
using SQuad = std::array<vec2, 4>;

SQuad MakeQuad(vec2 Center, vec2 HalfSize = vec2(16.0f, 16.0f))
{
	// Match CQuad::m_aPoints map order: TL, TR, BL, BR.
	return {{Center + vec2(-HalfSize.x, -HalfSize.y), Center + vec2(HalfSize.x, -HalfSize.y), Center + vec2(-HalfSize.x, HalfSize.y), Center + vec2(HalfSize.x, HalfSize.y)}};
}

struct SPracticeMovingTile
{
	SQuad m_Initial;
	SQuad m_Current;
	int m_Tile = TILE_FREEZE;
	int m_LastPracticeGameTick = -1;
	bool m_Active = true;
};

void ApplyPracticeMovingTile(void *pUser, int PracticeGameTick, vec2 Position, bool ResolveCurrentPosition, int &GameTile, int &FrontTile)
{
	auto *pTile = static_cast<SPracticeMovingTile *>(pUser);
	if(!pTile || !pTile->m_Active)
		return;
	pTile->m_LastPracticeGameTick = PracticeGameTick;

	MovingTilesCollision::SOverride Override;
	MovingTilesCollision::Accumulate(pTile->m_Tile, pTile->m_Initial, pTile->m_Current, Position, ResolveCurrentPosition, Override);
	MovingTilesCollision::Apply(true, Override, GameTile, FrontTile);
}

class MovingTilesCharacterTest : public ::testing::Test
{
protected:
	std::unique_ptr<IStorage> m_pStorage;
	std::unique_ptr<IMap> m_pMap;
	CLayers m_Layers;
	CCollision m_Collision;
	CMapBugs m_MapBugs;
	CTuningParams m_Tuning;

	void SetUp() override
	{
		m_pStorage = CreateLocalStorage();
		ASSERT_NE(m_pStorage, nullptr);
		m_pMap = CreateMap();
		ASSERT_NE(m_pMap, nullptr);
		ASSERT_TRUE(m_pMap->Load(m_pStorage.get(), "data/maps/coverage.map", IStorage::TYPE_ALL));
		m_Layers.Init(m_pMap.get(), false, false);
		m_Collision.Init(&m_Layers);
	}

	void InitWorld(CGameWorld &World)
	{
		World.Init(&m_Collision, &m_Tuning, &m_MapBugs);
		World.m_WorldConfig = {};
		World.m_WorldConfig.m_IsDDRace = true;
		World.m_WorldConfig.m_PredictFreeze = true;
		World.m_WorldConfig.m_PredictTiles = true;
		World.m_WorldConfig.m_PredictDDRace = true;
		World.m_GameTick = 100;
	}

	bool FindAirCellPair(vec2 &Source, vec2 &Destination) const
	{
		for(int y = 1; y < m_Collision.GetHeight() - 1; y++)
		{
			for(int x = 1; x < m_Collision.GetWidth() - 3; x++)
			{
				const vec2 CandidateSource(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				const vec2 CandidateDestination = CandidateSource + vec2(64.0f, 0.0f);
				if(m_Collision.GetMapIndex(CandidateSource) < 0 && m_Collision.GetMapIndex(CandidateDestination) < 0)
				{
					Source = CandidateSource;
					Destination = CandidateDestination;
					return true;
				}
			}
		}
		return false;
	}

	static void SetCharacterPosition(CCharacter *pCharacter, vec2 Position, vec2 PreviousPosition)
	{
		CCharacterCore Core = pCharacter->GetCore();
		Core.m_Pos = Position;
		Core.m_Vel = vec2(0.0f, 0.0f);
		pCharacter->SetCore(Core);
		pCharacter->m_Pos = Position;
		pCharacter->m_PrevPos = PreviousPosition;
		pCharacter->m_PrevPrevPos = PreviousPosition;
	}

	static void Tick(CGameWorld &World)
	{
		World.m_GameTick++;
		World.Tick();
	}

	CCharacter *CreatePracticeCharacter(CGameWorld &SnapshotWorld, CGameWorld &PracticeWorld, SPracticeMovingTile &MovingTile, vec2 Position)
	{
		InitWorld(SnapshotWorld);
		SnapshotWorld.SetPracticeTileOverrideCallback(&MovingTile, ApplyPracticeMovingTile);

		CNetObj_Character NetCharacter{};
		NetCharacter.m_X = round_to_int(Position.x);
		NetCharacter.m_Y = round_to_int(Position.y);
		NetCharacter.m_HookedPlayer = -1;
		NetCharacter.m_HookState = HOOK_RETRACTED;
		NetCharacter.m_Weapon = WEAPON_HAMMER;
		NetCharacter.m_Emote = EMOTE_NORMAL;
		SnapshotWorld.InsertEntity(new CCharacter(&SnapshotWorld, 0, &NetCharacter));

		PracticeWorld.CopyWorldClean(&SnapshotWorld);
		CCharacter *pCharacter = PracticeWorld.GetCharacterById(0);
		if(pCharacter)
			SetCharacterPosition(pCharacter, Position, Position);
		return pCharacter;
	}
};
} // namespace

TEST_F(MovingTilesCharacterTest, PracticeCallbackUsesPrivateWorldTick)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));

	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(Destination)};
	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, Source);
	ASSERT_NE(pCharacter, nullptr);
	EXPECT_EQ(MovingTile.m_LastPracticeGameTick, -1);

	Tick(PracticeWorld);
	EXPECT_EQ(MovingTile.m_LastPracticeGameTick, PracticeWorld.GameTick());
}

TEST_F(MovingTilesCharacterTest, PracticeCharacterTickAppliesMovedFreezeAndUnfreeze)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));
	// Model the ordinary map freeze tile at the quad's source. The moved quad
	// must remove this native source semantics while supplying it at its new cell.
	m_Collision.SetCollisionAt(Source.x, Source.y, TILE_FREEZE);

	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(Destination)};
	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pPracticeCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, Source);
	ASSERT_NE(pPracticeCharacter, nullptr);
	EXPECT_EQ(PracticeWorld.Collision(), &m_Collision);
	EXPECT_EQ(pPracticeCharacter->Collision(), &m_Collision);

	const int OldFreezeDelay = g_Config.m_SvFreezeDelay;
	g_Config.m_SvFreezeDelay = 3;
	struct CRestoreFreezeDelay
	{
		int m_OldValue;
		~CRestoreFreezeDelay() { g_Config.m_SvFreezeDelay = m_OldValue; }
	} Restore{OldFreezeDelay};

	Tick(PracticeWorld);
	EXPECT_EQ(pPracticeCharacter->m_FreezeTime, 0) << "moved QFr must not remain effective at its source";

	// Traverse from the source's real map tile to the blank destination. The
	// static anti-skip list contains the source but omits the blank destination.
	SetCharacterPosition(pPracticeCharacter, Destination, Source);
	Tick(PracticeWorld);
	EXPECT_GT(pPracticeCharacter->m_FreezeTime, 0) << "the actual predicted CCharacter must freeze at the moved QFr";

	MovingTile.m_Tile = TILE_UNFREEZE;
	Tick(PracticeWorld);
	EXPECT_EQ(pPracticeCharacter->m_TileIndex, TILE_UNFREEZE) << "pos=" << pPracticeCharacter->m_Pos.x << ',' << pPracticeCharacter->m_Pos.y;
	EXPECT_EQ(pPracticeCharacter->m_TileFIndex, TILE_UNFREEZE) << "pos=" << pPracticeCharacter->m_Pos.x << ',' << pPracticeCharacter->m_Pos.y;
	EXPECT_FALSE(pPracticeCharacter->Core()->m_DeepFrozen);
	EXPECT_EQ(pPracticeCharacter->m_FreezeTime, 0) << "moved QUnFr must use the native Unfreeze path";

	// Practice reset restores the unmoved geometry and the original native tile.
	MovingTile.m_Tile = TILE_FREEZE;
	MovingTile.m_Current = MovingTile.m_Initial;
	PracticeWorld.CopyWorldClean(&SnapshotWorld);
	pPracticeCharacter = PracticeWorld.GetCharacterById(0);
	ASSERT_NE(pPracticeCharacter, nullptr);
	Tick(PracticeWorld);
	EXPECT_GT(pPracticeCharacter->m_FreezeTime, 0) << "reset geometry must restore the source map freeze";

	// With Practice disabled, the callback is inert and the blank destination is
	// unaffected by the moved practice tile.
	MovingTile.m_Active = false;
	MovingTile.m_Current = MakeQuad(Destination);
	SetCharacterPosition(pPracticeCharacter, Destination, Destination);
	pPracticeCharacter->Unfreeze();
	Tick(PracticeWorld);
	EXPECT_EQ(pPracticeCharacter->m_FreezeTime, 0) << "Practice OFF must not apply the virtual tile";
}

TEST_F(MovingTilesCharacterTest, PracticeCharacterFreezesAtFractionalQuadPositionAwayFromCellCenter)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));
	m_Collision.SetCollisionAt(Source.x, Source.y, TILE_FREEZE);

	const vec2 TeePosition = Destination + vec2(14.0f, 0.0f);
	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(TeePosition, vec2(10.0f, 16.0f))};
	ASSERT_FALSE(MovingTilesCollision::PointInQuad(MovingTile.m_Current, Destination))
		<< "the old cell-center query must miss this valid fractional overlap";
	ASSERT_TRUE(MovingTilesCollision::PointInQuad(MovingTile.m_Current, TeePosition));

	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, TeePosition);
	ASSERT_NE(pCharacter, nullptr);

	const int OldFreezeDelay = g_Config.m_SvFreezeDelay;
	g_Config.m_SvFreezeDelay = 3;
	struct CRestoreFreezeDelay
	{
		int m_OldValue;
		~CRestoreFreezeDelay() { g_Config.m_SvFreezeDelay = m_OldValue; }
	} Restore{OldFreezeDelay};

	Tick(PracticeWorld);
	EXPECT_EQ(pCharacter->m_TileIndex, TILE_FREEZE);
	EXPECT_GT(pCharacter->m_FreezeTime, 0) << "real CCharacter::HandleTiles must query the Tee position, not its cell center";
	EXPECT_EQ(pCharacter->m_FreezeTime, 3 * PracticeWorld.GameTickSpeed());
}

TEST_F(MovingTilesCharacterTest, StationaryAndMultiTickOverlapRemainsFrozen)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));
	const vec2 TeePosition = Destination + vec2(14.0f, 0.0f);
	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(TeePosition, vec2(10.0f, 16.0f))};

	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, TeePosition);
	ASSERT_NE(pCharacter, nullptr);

	const int OldFreezeDelay = g_Config.m_SvFreezeDelay;
	g_Config.m_SvFreezeDelay = 3;
	struct CRestoreFreezeDelay
	{
		int m_OldValue;
		~CRestoreFreezeDelay() { g_Config.m_SvFreezeDelay = m_OldValue; }
	} Restore{OldFreezeDelay};

	for(int TickIndex = 0; TickIndex < 8; TickIndex++)
	{
		SetCharacterPosition(pCharacter, TeePosition, TeePosition);
		Tick(PracticeWorld);
		EXPECT_EQ(pCharacter->m_TileIndex, TILE_FREEZE) << "tick " << TickIndex;
		EXPECT_GT(pCharacter->m_FreezeTime, 0) << "stationary QFr must remain effective on tick " << TickIndex;
	}
}

TEST_F(MovingTilesCharacterTest, MovingFreezeEnteringAndPassingThroughTeeUsesNativeTick)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));
	const vec2 TeePosition = Destination + vec2(14.0f, 0.0f);
	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(Destination + vec2(-20.0f, 0.0f), vec2(8.0f, 16.0f))};

	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, TeePosition);
	ASSERT_NE(pCharacter, nullptr);
	const int OldFreezeDelay = g_Config.m_SvFreezeDelay;
	g_Config.m_SvFreezeDelay = 3;
	struct CRestoreFreezeDelay
	{
		int m_OldValue;
		~CRestoreFreezeDelay() { g_Config.m_SvFreezeDelay = m_OldValue; }
	} Restore{OldFreezeDelay};

	Tick(PracticeWorld);
	EXPECT_EQ(pCharacter->m_FreezeTime, 0) << "QFr outside the Tee must not freeze";

	MovingTile.m_Current = MakeQuad(TeePosition, vec2(10.0f, 16.0f));
	SetCharacterPosition(pCharacter, TeePosition, TeePosition);
	ASSERT_TRUE(MovingTilesCollision::PointInQuad(MovingTile.m_Current, pCharacter->m_Pos));
	Tick(PracticeWorld);
	EXPECT_GT(pCharacter->m_FreezeTime, 0)
		<< "a moving QFr entering the Tee must freeze through native HandleTiles; tile=" << pCharacter->m_TileIndex
		<< " pos=" << pCharacter->m_Pos.x << ',' << pCharacter->m_Pos.y
		<< " core-pos=" << pCharacter->Core()->m_Pos.x << ',' << pCharacter->Core()->m_Pos.y;

	MovingTile.m_Current = MakeQuad(Destination + vec2(40.0f, 0.0f), vec2(8.0f, 16.0f));
	SetCharacterPosition(pCharacter, TeePosition, TeePosition);
	Tick(PracticeWorld);
	EXPECT_GT(pCharacter->m_FreezeTime, 0) << "leaving the quad does not erase the native freeze timer immediately";
}

TEST_F(MovingTilesCharacterTest, FractionalUnfreezeUsesNativeCharacterPath)
{
	vec2 Source;
	vec2 Destination;
	ASSERT_TRUE(FindAirCellPair(Source, Destination));
	const vec2 TeePosition = Destination + vec2(14.0f, 0.0f);
	SPracticeMovingTile MovingTile{MakeQuad(Source), MakeQuad(TeePosition, vec2(10.0f, 16.0f)), TILE_UNFREEZE};

	CGameWorld SnapshotWorld;
	CGameWorld PracticeWorld;
	CCharacter *pCharacter = CreatePracticeCharacter(SnapshotWorld, PracticeWorld, MovingTile, TeePosition);
	ASSERT_NE(pCharacter, nullptr);
	ASSERT_TRUE(pCharacter->Freeze(3));

	Tick(PracticeWorld);
	EXPECT_EQ(pCharacter->m_TileIndex, TILE_UNFREEZE);
	EXPECT_EQ(pCharacter->m_FreezeTime, 0) << "fractional QUnFr must call native Unfreeze through HandleTiles";
}
