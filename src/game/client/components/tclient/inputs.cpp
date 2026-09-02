#include "inputs.h"

#include <base/math.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>

namespace
{
	float FastModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_FAST || g_Config.m_AmfFastInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfFastInputAmount / 20.0f;
	}

	float BestModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_BEST || g_Config.m_AmfBestInputAmount <= 0)
			return 0.0f;
		float Offset = g_Config.m_AmfBestInputAmount / 100.0f;
		if(g_Config.m_AmfBestInputSmoothing > 0)
			Offset *= 1.0f - (g_Config.m_AmfBestInputSmoothing / 200.0f);
		if(g_Config.m_AmfBestInputLatencyComp > 0)
			Offset *= 1.0f + (g_Config.m_AmfBestInputLatencyComp / 100.0f);
		return Offset;
	}

	float SaikoModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_SAIKO || g_Config.m_AmfSaikoInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfSaikoInputAmount / 100.0f;
	}

	float SaikoPlusModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_SAIKO_PLUS || g_Config.m_AmfSaikoPlusInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfSaikoPlusInputAmount / 100.0f;
	}

	float DeltaModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_DELTA || g_Config.m_AmfDeltaInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfDeltaInputAmount / 100.0f;
	}

	float FModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_F || g_Config.m_AmfFInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfFInputAmount / 1000.0f;
	}

	float MeowModeOffsetTicks()
	{
		if(g_Config.m_AmfInputMode != AMF_INPUTS_MEOW || g_Config.m_AmfMeowInputAmount <= 0)
			return 0.0f;
		return g_Config.m_AmfMeowInputAmount / 100.0f;
	}

} // namespace

float TClientInputs::EffectiveOffsetTicks()
{
	switch(g_Config.m_AmfInputMode)
	{
	case AMF_INPUTS_FAST: return FastModeOffsetTicks();
	case AMF_INPUTS_BEST: return BestModeOffsetTicks();
	case AMF_INPUTS_SAIKO: return SaikoModeOffsetTicks();
	case AMF_INPUTS_SAIKO_PLUS: return SaikoPlusModeOffsetTicks();
	case AMF_INPUTS_DELTA: return DeltaModeOffsetTicks();
	case AMF_INPUTS_F: return FModeOffsetTicks();
	case AMF_INPUTS_MEOW: return MeowModeOffsetTicks();
	default: return 0.0f;
	}
}

int TClientInputs::PredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	// BestClient's Saiko+ path predicts one additional local tick so the
	// selected input is visible immediately. Other modes retain their
	// established rounding semantics.
	if(g_Config.m_AmfInputMode == AMF_INPUTS_SAIKO_PLUS)
		return (int)std::ceil(OffsetTicks + 1.0f);
	return (int)std::ceil(OffsetTicks);
}

int TClientInputs::PredictionTicksOthers(float OffsetTicks)
{
	if(g_Config.m_AmfInputMode == AMF_INPUTS_SAIKO_PLUS)
		return OffsetTicks <= 0.0f ? 0 : (int)std::ceil(OffsetTicks);
	return PredictionTicks(OffsetTicks);
}

void TClientInputs::ApplyOffset(float OffsetTicks, int &Tick, float &Intra)
{
	if(OffsetTicks <= 0.0f)
		return;
	const int WholeTicks = (int)OffsetTicks;
	const float OffsetIntra = OffsetTicks - WholeTicks;
	const float CombinedIntra = Intra + OffsetIntra;
	const int CarryOverTicks = (int)CombinedIntra;
	Tick += WholeTicks + CarryOverTicks;
	Intra = CombinedIntra - CarryOverTicks;
}

float TClientInputs::BestInterpolationAmount(float Fraction, float DeltaLength, bool Enable)
{
	if(!Enable)
		return Fraction;
	const float T = std::clamp(Fraction, 0.0f, 1.0f);
	const float T2 = T * T;
	const float CubicT = 3.0f * T2 - 2.0f * T2 * T;
	switch(std::clamp(g_Config.m_AmfBestInputInterpolation, 1, 3))
	{
	case 2:
		return CubicT;
	case 3:
		return mix(T, CubicT, std::clamp(DeltaLength / 1000.0f, 0.0f, 1.0f));
	default:
		return T;
	}
}

vec2 TClientInputs::BestInterpolate(vec2 PrevPos, vec2 CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, length(CurPos - PrevPos), Enable));
}

float TClientInputs::BestInterpolate(float PrevPos, float CurPos, float Fraction, bool Enable)
{
	return mix(PrevPos, CurPos, BestInterpolationAmount(Fraction, absolute(CurPos - PrevPos), Enable));
}

bool TClientInputs::FastOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_FAST && g_Config.m_AmfFastInputOthers != 0;
}

bool TClientInputs::BestOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_BEST && g_Config.m_AmfBestInputOthers != 0;
}

bool TClientInputs::SaikoOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_SAIKO && g_Config.m_AmfSaikoInputOthers != 0;
}

bool TClientInputs::SaikoPlusOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_SAIKO_PLUS && g_Config.m_AmfSaikoPlusInputOthers != 0;
}

bool TClientInputs::DeltaOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_DELTA && g_Config.m_AmfDeltaInputOthers != 0;
}

bool TClientInputs::FOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_F && g_Config.m_AmfFInputOthers != 0;
}

bool TClientInputs::CloudOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_CLOUD && g_Config.m_AmfCloudInputOthers != 0;
}

bool TClientInputs::MeowOthers()
{
	return g_Config.m_AmfInputMode == AMF_INPUTS_MEOW && g_Config.m_AmfMeowInputOthers != 0;
}

bool TClientInputs::AnyOthers()
{
	return FastOthers() || BestOthers() || SaikoOthers() || SaikoPlusOthers() || DeltaOthers() || FOthers() || CloudOthers() || MeowOthers();
}

bool TClientInputs::ImmediateOthers()
{
	return BestOthers() || SaikoOthers() || SaikoPlusOthers() || DeltaOthers() || MeowOthers();
}
