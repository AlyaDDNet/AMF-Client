#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_CONTEXT_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_IMPACT_IMPACT_CONTEXT_POLICY_H

/*
 * Copyright (c) 2026 alya?
 * Original AMF Impact / Run Statistics code, developed for AMF Client.
 * Licensed under licenses/AMF-Impact-LICENSE.txt.
 */

#include <engine/client.h>

namespace AmfImpact
{
	// A demo contains recorded gameplay but has no live network endpoint. Only
	// the online client state guarantees that ServerAddress() is formatable.
	constexpr bool HasLiveServerAddress(IClient::EClientState State)
	{
		return State == IClient::STATE_ONLINE;
	}
}

#endif
