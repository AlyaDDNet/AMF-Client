/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_CONFIG_MIGRATIONS_H
#define ENGINE_CLIENT_CONFIG_MIGRATIONS_H

#include <base/io.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdio>

namespace ClientConfigMigrations
{
constexpr int CURRENT_CONFIG_VERSION = 5;
constexpr int MIN_PREDICTION_MARGIN_MS = 1;
constexpr int MAX_PREDICTION_MARGIN_MS = 300;
constexpr int DEFAULT_PREDICTION_MARGIN_MS = 10;

struct SPersistedPredictionMargins
{
	bool m_HasPredictionMargin = false;
	int m_PredictionMargin = 0;
	bool m_HasAutoMarginManualValue = false;
	int m_AutoMarginManualValue = 0;
};

inline SPersistedPredictionMargins ReadPersistedPredictionMargins(IStorage *pStorage)
{
	SPersistedPredictionMargins Result;
	for(ConfigDomain Domain = ConfigDomain::START; Domain < ConfigDomain::NUM; ++Domain)
	{
		if(!s_aConfigDomains[Domain].m_HasVars)
			continue;

		CLineReader LineReader;
		if(!LineReader.OpenFile(pStorage->OpenFile(s_aConfigDomains[Domain].m_aConfigPath, IOFLAG_READ, IStorage::TYPE_ALL)))
			continue;

		for(const char *pLine = LineReader.Get(); pLine; pLine = LineReader.Get())
		{
			char aName[128];
			int Value;
			if(std::sscanf(pLine, " %127s %d", aName, &Value) != 2)
				continue;

			if(str_comp(aName, "cl_prediction_margin") == 0)
			{
				Result.m_HasPredictionMargin = true;
				Result.m_PredictionMargin = Value;
			}
			else if(str_comp(aName, "amf_auto_margin_manual_value") == 0)
			{
				Result.m_HasAutoMarginManualValue = true;
				Result.m_AutoMarginManualValue = Value;
			}
		}
	}
	return Result;
}

inline int ClampManualMargin(int Margin)
{
	return std::clamp(Margin, MIN_PREDICTION_MARGIN_MS, MAX_PREDICTION_MARGIN_MS);
}

inline int TenthsOfMillisecondsToMilliseconds(int Margin)
{
	// Clamp before adding the rounding offset to avoid integer overflow on
	// malformed or hand-edited config values.
	return ClampManualMargin((std::clamp(Margin, 1, MAX_PREDICTION_MARGIN_MS * 10) + 5) / 10);
}

inline int EffectivePredictionMargin(bool SyncWeaponInput, int ManualMargin, bool AutoMarginEnabled, int RuntimeAutoMargin)
{
	if(!SyncWeaponInput)
		return DEFAULT_PREDICTION_MARGIN_MS;
	if(AutoMarginEnabled && RuntimeAutoMargin >= 0)
		return ClampManualMargin(RuntimeAutoMargin);
	return ClampManualMargin(ManualMargin);
}

inline void MigratePredictionMargins(CConfig &Config, const SPersistedPredictionMargins &Persisted = {})
{
	const int PreviousVersion = Config.m_ClConfigVersion;
	if(PreviousVersion > CURRENT_CONFIG_VERSION)
		return; // Do not rewrite a config produced by a newer client.

	if(Persisted.m_HasPredictionMargin)
		Config.m_ClPredictionMargin = Persisted.m_PredictionMargin;
	if(Persisted.m_HasAutoMarginManualValue)
		Config.m_AmfAutoMarginManualValue = Persisted.m_AutoMarginManualValue;

	if(PreviousVersion < CURRENT_CONFIG_VERSION)
	{
		// Config versions 0/1 stored whole milliseconds. The historical value
		// 100 was the old default/ambiguous sentinel and was interpreted as the
		// new 10 ms default by the original one-time migration.
		if(PreviousVersion <= 1)
		{
			if(Config.m_ClPredictionMargin == 100)
				Config.m_ClPredictionMargin = DEFAULT_PREDICTION_MARGIN_MS;
			else
				Config.m_ClPredictionMargin = ClampManualMargin(Config.m_ClPredictionMargin);
			Config.m_AmfAutoMarginManualValue = std::clamp(Config.m_AmfAutoMarginManualValue, 0, MAX_PREDICTION_MARGIN_MS);
		}
		else
		{
			// Version 2 stored the DDNet value in tenths of a millisecond; its
			// AMF manual backup was still whole milliseconds. Versions 3 and 4
			// stored both values in tenths.
			if(PreviousVersion == 3 && Config.m_AmfAutoMargin && Config.m_AmfAutoMarginManualValue > 0)
				Config.m_ClPredictionMargin = Config.m_AmfAutoMarginManualValue;

			// When the old file omitted the setting, the current vanilla default
			// is already canonical milliseconds; never reinterpret that default
			// as a saved legacy tenths value.
			if(Persisted.m_HasPredictionMargin || (PreviousVersion == 3 && Config.m_AmfAutoMargin && Config.m_AmfAutoMarginManualValue > 0))
				Config.m_ClPredictionMargin = TenthsOfMillisecondsToMilliseconds(Config.m_ClPredictionMargin);
			else
				Config.m_ClPredictionMargin = ClampManualMargin(Config.m_ClPredictionMargin);
			if(PreviousVersion >= 3 && Persisted.m_HasAutoMarginManualValue)
				Config.m_AmfAutoMarginManualValue = Config.m_AmfAutoMarginManualValue > 0 ? TenthsOfMillisecondsToMilliseconds(Config.m_AmfAutoMarginManualValue) : 0;
			else
				Config.m_AmfAutoMarginManualValue = std::clamp(Config.m_AmfAutoMarginManualValue, 0, MAX_PREDICTION_MARGIN_MS);
		}
	}

	Config.m_ClPredictionMargin = ClampManualMargin(Config.m_ClPredictionMargin);
	Config.m_AmfAutoMarginManualValue = std::clamp(Config.m_AmfAutoMarginManualValue, 0, MAX_PREDICTION_MARGIN_MS);
	Config.m_ClConfigVersion = CURRENT_CONFIG_VERSION;
}

inline void ApplyFreshServerBrowserDefaults(CConfig &Config, bool ExistingBrowserConfig)
{
	if(ExistingBrowserConfig)
		return;
	// AMF's first-run browser should not silently exclude non-Novice or
	// login-required servers. These non-upstream values are saved on first exit.
	Config.m_BrFilterString[0] = '\0';
	Config.m_BrFilterLogin = 0;
}
}

#endif
