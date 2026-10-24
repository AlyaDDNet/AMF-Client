/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_BIND_MIGRATIONS_H
#define GAME_CLIENT_COMPONENTS_BIND_MIGRATIONS_H

namespace BindMigrations
{
constexpr int DDRACE_BIND_VERSION_WITH_JUMP_DEFAULT = 3;

inline bool ShouldAssignJumpBind(int BindSetVersion, bool FreeOnly)
{
	// FreeOnly is the post-load migration path. An empty key there may represent
	// an explicit user unbind, so only the explicit defaults/reset pass adds W.
	return !FreeOnly && BindSetVersion < DDRACE_BIND_VERSION_WITH_JUMP_DEFAULT;
}
}

#endif
