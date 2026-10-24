/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_COMPONENTS_PLAYER_RENDER_STATE_H
#define GAME_CLIENT_COMPONENTS_PLAYER_RENDER_STATE_H

namespace PlayerRenderState
{
constexpr bool ShouldUsePredictedFreezeState(bool DemoPlayback, bool IsLocalPlayer)
{
	return !DemoPlayback && IsLocalPlayer;
}

constexpr bool HasVisibleServerTee(bool CharacterActive, bool PreviousPlayerInfoAvailable, bool PlayerInfoAvailable)
{
	return CharacterActive && PreviousPlayerInfoAvailable && PlayerInfoAvailable;
}

constexpr bool HasVisibleWorldTee(bool SnapshotTeeVisible, bool OtherTeam, int OthersAlphaPercent)
{
	return SnapshotTeeVisible && (!OtherTeam || OthersAlphaPercent > 0);
}
}

#endif
