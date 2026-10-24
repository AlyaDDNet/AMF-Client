#include <game/client/components/tclient/moving_tiles_collision.h>

#include <gtest/gtest.h>

namespace
{
	using MovingTilesCollision::SOverride;
	using SQuad = std::array<vec2, 4>;

	SQuad MakeQuad(float Left, float Top)
	{
		// Match CQuad::m_aPoints map order: TL, TR, BL, BR.
		return {{{Left, Top}, {Left + 32.0f, Top}, {Left, Top + 32.0f}, {Left + 32.0f, Top + 32.0f}}};
	}
}

TEST(MovingTiles, PointInQuadUsesDonorMapVertexOrder)
{
	const SQuad Quad = MakeQuad(0.0f, 0.0f);
	EXPECT_TRUE(MovingTilesCollision::PointInQuad(Quad, vec2(8.0f, 8.0f)));
	EXPECT_TRUE(MovingTilesCollision::PointInQuad(Quad, vec2(24.0f, 8.0f)));
	EXPECT_TRUE(MovingTilesCollision::PointInQuad(Quad, vec2(8.0f, 24.0f)));
	EXPECT_TRUE(MovingTilesCollision::PointInQuad(Quad, vec2(24.0f, 24.0f)));
	EXPECT_FALSE(MovingTilesCollision::PointInQuad(Quad, vec2(-1.0f, 16.0f)));
	EXPECT_FALSE(MovingTilesCollision::PointInQuad(Quad, vec2(33.0f, 16.0f)));
}

TEST(MovingTiles, PracticeFreezeMovesFromSourceToAnimatedPosition)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	const SQuad Moved = MakeQuad(64.0f, 0.0f);

	SOverride AtSource;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, vec2(16.0f, 16.0f), true, AtSource);
	int SourceGameTile = TILE_FREEZE;
	int SourceFrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtSource, SourceGameTile, SourceFrontTile);
	EXPECT_EQ(SourceGameTile, TILE_AIR);
	EXPECT_EQ(SourceFrontTile, TILE_AIR);

	SOverride AtDestination;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, vec2(80.0f, 16.0f), true, AtDestination);
	int DestinationGameTile = TILE_AIR;
	int DestinationFrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtDestination, DestinationGameTile, DestinationFrontTile);
	EXPECT_EQ(DestinationGameTile, TILE_FREEZE);
	EXPECT_EQ(DestinationFrontTile, TILE_FREEZE);
}

TEST(MovingTiles, PracticeResetRestoresSourceTile)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	SOverride AtResetPosition;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Original, vec2(16.0f, 16.0f), true, AtResetPosition);
	int GameTile = TILE_FREEZE;
	int FrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtResetPosition, GameTile, FrontTile);
	EXPECT_EQ(GameTile, TILE_FREEZE);
	EXPECT_EQ(FrontTile, TILE_FREEZE);
	EXPECT_TRUE(AtResetPosition.m_HasCurrentTile);
}

TEST(MovingTiles, FractionalQuadUsesQueriedCharacterPositionNotCellCenter)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	const SQuad Moved = {{{22.0f, 0.0f}, {42.0f, 0.0f}, {22.0f, 32.0f}, {42.0f, 32.0f}}};
	const vec2 CellCenter(16.0f, 16.0f);
	const vec2 CharacterPosition(31.0f, 16.0f);
	ASSERT_FALSE(MovingTilesCollision::PointInQuad(Moved, CellCenter));
	ASSERT_TRUE(MovingTilesCollision::PointInQuad(Moved, CharacterPosition));

	SOverride AtCharacter;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, CharacterPosition, true, AtCharacter);
	int GameTile = TILE_AIR;
	int FrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtCharacter, GameTile, FrontTile);
	EXPECT_EQ(GameTile, TILE_FREEZE);
	EXPECT_EQ(FrontTile, TILE_FREEZE);
}

TEST(MovingTiles, HistoricalCellResolutionDoesNotApplyCurrentQuadAtCharacter)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	const SQuad Moved = MakeQuad(64.0f, 0.0f);

	SOverride HistoricalCell;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, vec2(80.0f, 16.0f), false, HistoricalCell);
	int GameTile = TILE_AIR;
	int FrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, HistoricalCell, GameTile, FrontTile);
	EXPECT_FALSE(HistoricalCell.m_HasCurrentTile);
	EXPECT_EQ(GameTile, TILE_AIR);
	EXPECT_EQ(FrontTile, TILE_AIR);
}

TEST(MovingTiles, RenderAndCollisionShareTheSameEnvelopeTransform)
{
	const SQuad Local = {{{0.0f, 0.0f}, {32.0f, 0.0f}, {32.0f, 32.0f}, {0.0f, 32.0f}}};
	const vec2 Pivot(16.0f, 16.0f);
	const vec2 Offset(19.25f, -7.5f);
	const float Rotation = 0.37f;
	const SQuad RenderGeometry = MovingTilesCollision::TransformQuad(Local, Pivot, Offset, Rotation);
	const SQuad CollisionGeometry = MovingTilesCollision::TransformQuad(Local, Pivot, Offset, Rotation);
	for(size_t Index = 0; Index < RenderGeometry.size(); Index++)
	{
		EXPECT_FLOAT_EQ(RenderGeometry[Index].x, CollisionGeometry[Index].x);
		EXPECT_FLOAT_EQ(RenderGeometry[Index].y, CollisionGeometry[Index].y);
	}
}

TEST(MovingTiles, PracticeOffDoesNotApplyMovedTileOverride)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	const SQuad Moved = MakeQuad(64.0f, 0.0f);

	SOverride AtSource;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, vec2(16.0f, 16.0f), true, AtSource);
	int GameTile = TILE_FREEZE;
	int FrontTile = TILE_AIR;
	MovingTilesCollision::Apply(false, AtSource, GameTile, FrontTile);
	EXPECT_EQ(GameTile, TILE_FREEZE);
	EXPECT_EQ(FrontTile, TILE_AIR);

	SOverride AtDestination;
	MovingTilesCollision::Accumulate(TILE_FREEZE, Original, Moved, vec2(80.0f, 16.0f), true, AtDestination);
	GameTile = TILE_AIR;
	FrontTile = TILE_AIR;
	MovingTilesCollision::Apply(false, AtDestination, GameTile, FrontTile);
	EXPECT_EQ(GameTile, TILE_AIR);
	EXPECT_EQ(FrontTile, TILE_AIR);
}

TEST(MovingTiles, UnfreezeQuadMovesAndRemovesSourceSemantics)
{
	const SQuad Original = MakeQuad(0.0f, 0.0f);
	const SQuad Moved = MakeQuad(64.0f, 0.0f);

	SOverride AtSource;
	MovingTilesCollision::Accumulate(TILE_UNFREEZE, Original, Moved, vec2(16.0f, 16.0f), true, AtSource);
	int SourceGameTile = TILE_UNFREEZE;
	int SourceFrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtSource, SourceGameTile, SourceFrontTile);
	EXPECT_EQ(SourceGameTile, TILE_AIR);

	SOverride AtDestination;
	MovingTilesCollision::Accumulate(TILE_UNFREEZE, Original, Moved, vec2(80.0f, 16.0f), true, AtDestination);
	int DestinationGameTile = TILE_FREEZE;
	int DestinationFrontTile = TILE_AIR;
	MovingTilesCollision::Apply(true, AtDestination, DestinationGameTile, DestinationFrontTile);
	EXPECT_EQ(DestinationGameTile, TILE_UNFREEZE);
	EXPECT_EQ(DestinationFrontTile, TILE_UNFREEZE);
}
