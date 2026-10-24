#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MOVING_TILES_COLLISION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MOVING_TILES_COLLISION_H

#include <base/vmath.h>

#include <game/mapitems.h>

#include <array>
#include <cmath>

namespace MovingTilesCollision
{
struct SOverride
{
	int m_CurrentTile = TILE_AIR;
	bool m_HasCurrentTile = false;
	bool m_RemoveFreezeAtSource = false;
	bool m_RemoveUnfreezeAtSource = false;
};

inline std::array<vec2, 4> TransformQuad(const std::array<vec2, 4> &Points, vec2 Pivot, vec2 Offset, float Rotation)
{
	std::array<vec2, 4> Result = Points;
	if(Rotation != 0.0f)
	{
		const float Sin = std::sin(Rotation);
		const float Cos = std::cos(Rotation);
		for(vec2 &Point : Result)
		{
			const vec2 Relative = Point - Pivot;
			Point = vec2(Relative.x * Cos - Relative.y * Sin, Relative.x * Sin + Relative.y * Cos) + Pivot;
		}
	}
	for(vec2 &Point : Result)
		Point += Offset;
	return Result;
}

inline bool PointInQuad(const std::array<vec2, 4> &Points, vec2 Position)
{
	// CQuad::m_aPoints uses the map/freeform order TL, TR, BL, BR, not
	// perimeter order. Match BestClient's QFr/QUnFr test exactly: its edges
	// are 0->1->3->2->0. Treating 0,1,2,3 as a polygon creates a bow-tie.
	const vec2 &T0 = Points[0];
	const vec2 &T1 = Points[1];
	const vec2 &T2 = Points[2];
	const vec2 &T3 = Points[3];
	auto IsLeft = [](const vec2 &A, const vec2 &B, const vec2 &P) {
		return ((B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x)) >= 0.0f;
	};
	return IsLeft(T0, T1, Position) &&
		IsLeft(T1, T3, Position) &&
		IsLeft(T3, T2, Position) &&
		IsLeft(T2, T0, Position);
}

inline bool SameQuad(const std::array<vec2, 4> &Left, const std::array<vec2, 4> &Right)
{
	constexpr float PositionEpsilon = 0.0001f;
	for(size_t Index = 0; Index < Left.size(); Index++)
	{
		if(std::abs(Left[Index].x - Right[Index].x) > PositionEpsilon || std::abs(Left[Index].y - Right[Index].y) > PositionEpsilon)
			return false;
	}
	return true;
}

inline void Accumulate(int Tile, const std::array<vec2, 4> &InitialQuad, const std::array<vec2, 4> &CurrentQuad, vec2 Position, bool ResolveCurrentPosition, SOverride &Override)
{
	if(Tile != TILE_FREEZE && Tile != TILE_UNFREEZE)
		return;

	const bool InsideCurrent = PointInQuad(CurrentQuad, Position);
	if(ResolveCurrentPosition && InsideCurrent)
	{
		// Match HandleTiles' native precedence when different moving quads overlap.
		if(Tile == TILE_FREEZE || !Override.m_HasCurrentTile)
		{
			Override.m_CurrentTile = Tile;
			Override.m_HasCurrentTile = true;
		}
		return;
	}

	if(!SameQuad(InitialQuad, CurrentQuad) && PointInQuad(InitialQuad, Position))
	{
		if(Tile == TILE_FREEZE)
			Override.m_RemoveFreezeAtSource = true;
		else
			Override.m_RemoveUnfreezeAtSource = true;
	}
}

inline void Apply(bool PracticeActive, const SOverride &Override, int &GameTile, int &FrontTile)
{
	if(!PracticeActive)
		return;

	if(Override.m_HasCurrentTile)
	{
		// The named quad is the effective semantic tile at its animated position.
		// Move restrictions/solid collision continue to come from the real map layers.
		GameTile = Override.m_CurrentTile;
		FrontTile = Override.m_CurrentTile;
		return;
	}

	if(Override.m_RemoveFreezeAtSource)
	{
		if(GameTile == TILE_FREEZE)
			GameTile = TILE_AIR;
		if(FrontTile == TILE_FREEZE)
			FrontTile = TILE_AIR;
	}
	if(Override.m_RemoveUnfreezeAtSource)
	{
		if(GameTile == TILE_UNFREEZE)
			GameTile = TILE_AIR;
		if(FrontTile == TILE_UNFREEZE)
			FrontTile = TILE_AIR;
	}
}
} // namespace MovingTilesCollision

#endif
