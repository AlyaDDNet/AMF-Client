#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_FX_EFFECT_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_FEATURES_FX_EFFECT_POLICY_H

namespace AmfEffectPolicy
{
constexpr bool HideAirJumpParticles(bool AirJumpEnabled, bool HideParticles, bool OtherPlayer)
{
	return AirJumpEnabled && HideParticles && !OtherPlayer;
}

constexpr bool HideHammerHitParticles(bool HammerSlashEnabled, bool HideParticles, bool OtherPlayer)
{
	return HammerSlashEnabled && HideParticles && !OtherPlayer;
}
}

#endif
