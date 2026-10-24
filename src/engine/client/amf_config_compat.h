/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
#ifndef ENGINE_CLIENT_AMF_CONFIG_COMPAT_H
#define ENGINE_CLIENT_AMF_CONFIG_COMPAT_H

#include <array>

#include <engine/console.h>

class IConfigManager;
class IConsole;
class IStorage;

class CAmfConfigCompatibility
{
	struct SLegacyConfigVariable
	{
		class CAmfConfigCompatibility *m_pOwner;
		const char *m_pName;
		int *m_pTarget;
		int m_Min;
		int m_Max;
		bool m_AlwaysMigrate;
	};

	std::array<SLegacyConfigVariable, 20> m_aLegacyVariables;
	bool m_MigrateLegacyAmfConfig = false;
	bool m_RenamedAmfConfigMigrated = false;
	bool m_LegacyAmfDiscordRpcSeen = false;
	bool m_AmfDiscordRpcLoaded = false;
	int m_LegacyAmfDiscordRpc = 1;

	static void ConMigrateLegacyConfig(IConsole::IResult *pResult, void *pUserData);
	static void ConMigrateLegacyDiscordRpc(IConsole::IResult *pResult, void *pUserData);
	static void ConchainAmfDiscordRpc(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

public:
	CAmfConfigCompatibility();
	void RegisterCommands(IConsole *pConsole);
	void BeginLoad(IStorage *pStorage);
	void FinishLoad(IConfigManager *pConfigManager);
};

#endif
