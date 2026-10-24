/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */

#include "amf_config_compat.h"

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <base/log.h>

#include <algorithm>

CAmfConfigCompatibility::CAmfConfigCompatibility() :
	m_aLegacyVariables{{
		{this, "tc_input_mode", &g_Config.m_AmfInputMode, 0, 8, false},
		{this, "tc_best_input_amount", &g_Config.m_AmfBestInputAmount, 0, 1000, false},
		{this, "tc_best_input_smoothing", &g_Config.m_AmfBestInputSmoothing, 0, 100, false},
		{this, "tc_best_input_latency_comp", &g_Config.m_AmfBestInputLatencyComp, 0, 50, false},
		{this, "tc_best_input_interpolation", &g_Config.m_AmfBestInputInterpolation, 1, 3, false},
		{this, "tc_best_input_others", &g_Config.m_AmfBestInputOthers, 0, 1, false},
		{this, "tc_saiko_input_amount", &g_Config.m_AmfSaikoInputAmount, 0, 500, false},
		{this, "tc_saiko_input_others", &g_Config.m_AmfSaikoInputOthers, 0, 1, false},
		{this, "tc_delta_input_amount", &g_Config.m_AmfDeltaInputAmount, 0, 500, false},
		{this, "tc_delta_input_others", &g_Config.m_AmfDeltaInputOthers, 0, 1, false},
		{this, "tc_f_input_amount", &g_Config.m_AmfFInputAmount, 0, 5000, false},
		{this, "tc_f_input_others", &g_Config.m_AmfFInputOthers, 0, 1, false},
		{this, "tc_cloud_input_amount", &g_Config.m_AmfCloudInputAmount, 0, 500, false},
		{this, "tc_cloud_input_others", &g_Config.m_AmfCloudInputOthers, 0, 1, false},
		{this, "tc_show_client_info_on_start", nullptr, 0, 0, false},
		{this, "tc_auto_margin", nullptr, 0, 0, false},
		{this, "gfx_motion_blur", &g_Config.m_AmfMotionBlur, 0, 100, false},
		{this, "tc_amf_input_correction", nullptr, 0, 0, false},
		{this, "tc_amf_input_interpolation", nullptr, 0, 0, false},
		{this, "amf_smooth_hud_plus", &g_Config.m_AmfSmoothHud, 0, 1, true},	}} {}

void CAmfConfigCompatibility::ConMigrateLegacyConfig(IConsole::IResult *pResult, void *pUserData)
{
	auto *pVariable = static_cast<SLegacyConfigVariable *>(pUserData);
	auto *pSelf = pVariable->m_pOwner;
	if((!pSelf->m_MigrateLegacyAmfConfig && !pVariable->m_AlwaysMigrate) || pVariable->m_pTarget == nullptr || pResult->NumArguments() == 0)
		return;
	*pVariable->m_pTarget = std::clamp(pResult->GetInteger(0), pVariable->m_Min, pVariable->m_Max);
	if(pVariable->m_AlwaysMigrate)
		pSelf->m_RenamedAmfConfigMigrated = true;
}

void CAmfConfigCompatibility::ConMigrateLegacyDiscordRpc(IConsole::IResult *pResult, void *pUserData)
{
	if(pResult->NumArguments() == 0)
		return;
	auto *pSelf = static_cast<CAmfConfigCompatibility *>(pUserData);
	pSelf->m_LegacyAmfDiscordRpcSeen = true;
	pSelf->m_LegacyAmfDiscordRpc = std::clamp(pResult->GetInteger(0), 0, 1);
}

void CAmfConfigCompatibility::ConchainAmfDiscordRpc(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() != 0)
		static_cast<CAmfConfigCompatibility *>(pUserData)->m_AmfDiscordRpcLoaded = true;
}

void CAmfConfigCompatibility::RegisterCommands(IConsole *pConsole)
{
	for(auto &Variable : m_aLegacyVariables)
		pConsole->Register(Variable.m_pName, "?i[value]", CFGFLAG_CLIENT, ConMigrateLegacyConfig, &Variable, "Deprecated AMF Client config migration alias");
	pConsole->Register("tc_discord_rpc", "?i[value]", CFGFLAG_CLIENT, ConMigrateLegacyDiscordRpc, this, "Deprecated AMF Client Discord setting migration alias");
	pConsole->Chain("amf_discord_rpc", ConchainAmfDiscordRpc, this);
}

void CAmfConfigCompatibility::BeginLoad(IStorage *pStorage)
{
	m_MigrateLegacyAmfConfig = !pStorage->FileExists(s_aConfigDomains[ConfigDomain::AMFCLIENT].m_aConfigPath, IStorage::TYPE_ALL);
	m_RenamedAmfConfigMigrated = false;
	m_LegacyAmfDiscordRpcSeen = false;
	m_AmfDiscordRpcLoaded = false;
	m_LegacyAmfDiscordRpc = 1;
}

void CAmfConfigCompatibility::FinishLoad(IConfigManager *pConfigManager)
{
	bool MovingTilesModeMigrated = false;
	if(g_Config.m_AmfMovingTilesMode < 0)
	{
		g_Config.m_AmfMovingTilesMode = g_Config.m_TcMovingTilesEntities ? 1 : 0;
		MovingTilesModeMigrated = true;
	}
	const bool MigrateLegacyAmfDiscordRpc = m_LegacyAmfDiscordRpcSeen && !m_AmfDiscordRpcLoaded;
	if(MigrateLegacyAmfDiscordRpc)
		g_Config.m_AmfDiscordRPC = m_LegacyAmfDiscordRpc;
	if(m_RenamedAmfConfigMigrated || MigrateLegacyAmfDiscordRpc || MovingTilesModeMigrated)
	{
		if(!pConfigManager->SaveDomain(ConfigDomain::AMFCLIENT))
			log_warn("amf-config", "failed to save migrated AMF setting");
	}
	if(m_MigrateLegacyAmfConfig)
	{
		g_Config.m_AmfFastInputAmount = std::clamp(g_Config.m_TcFastInputAmount, 1, 100);
		g_Config.m_AmfFastInputOthers = g_Config.m_TcFastInputOthers != 0;
	}
	m_MigrateLegacyAmfConfig = false;
	m_RenamedAmfConfigMigrated = false;
	m_LegacyAmfDiscordRpcSeen = false;
	m_AmfDiscordRpcLoaded = false;
}
