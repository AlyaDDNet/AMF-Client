/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_SMOOTH_UI_H
#define GAME_CLIENT_SMOOTH_UI_H

#include <base/math.h>

#include <cmath>

inline float SmoothUiClamp01(float Value)
{
	return std::clamp(Value, 0.0f, 1.0f);
}

inline float SmoothUiEaseInOutQuad(float Value)
{
	Value = SmoothUiClamp01(Value);
	if(Value < 0.5f)
		return 2.0f * Value * Value;
	return 1.0f - std::pow(-2.0f * Value + 2.0f, 2.0f) / 2.0f;
}

inline float SmoothUiEaseOutCubic(float Value)
{
	Value = SmoothUiClamp01(Value);
	const float Inverse = 1.0f - Value;
	return 1.0f - Inverse * Inverse * Inverse;
}

/**
 * Frame-rate independent exponential approach for small UI transitions.
 *
 * The caller owns the state and should only call it while the element is
 * visible or changing. This keeps inactive HUD elements on the standard,
 * allocation-free DDNet render path.
 */
inline float SmoothApproach(float Current, float Target, float DeltaTime, float Response)
{
	const float ClampedDeltaTime = std::clamp(DeltaTime, 0.0f, 0.15f);
	const float Factor = 1.0f - std::exp(-Response * ClampedDeltaTime);
	return mix(Current, Target, Factor);
}

/**
 * Reaches the target within the requested UI duration, independent of frame
 * rate. A long frame is bounded and snaps to the target instead of leaving a
 * menu half-open after returning from Alt+Tab.
 */
inline float SmoothApproachDuration(float Current, float Target, float DeltaTime, int DurationMs)
{
	const float Duration = std::max(DurationMs, 1) / 1000.0f;
	if(DeltaTime >= 0.15f)
		return Target;
	// exp(-4.6) is approximately 1%, making the configured duration a useful
	// practical settling time without an overshoot.
	return SmoothApproach(Current, Target, DeltaTime, 4.6f / Duration);
}

/**
 * Reusable state for dependent settings sections. The target changes
 * immediately, so closing sections stop accepting input in the same frame,
 * while their clipped visual height can finish smoothly.
 */
class CSmoothUiSectionAnimation
{
	float m_Value = 0.0f;
	bool m_Initialized = false;
	bool m_TargetOpen = false;

public:
	float Update(bool Open, float DeltaTime, int DurationMs, bool Animate)
	{
		m_TargetOpen = Open;
		if(!m_Initialized)
		{
			m_Value = Open ? 1.0f : 0.0f;
			m_Initialized = true;
		}
		else if(!Animate)
			m_Value = Open ? 1.0f : 0.0f;
		else
		{
			const float Target = Open ? 1.0f : 0.0f;
			if(m_Value != Target)
			{
				const float DurationSeconds = std::max(DurationMs, 1) / 1000.0f;
				if(DeltaTime > 0.0f)
				{
					const float Step = std::min(DeltaTime, DurationSeconds) / DurationSeconds;
					m_Value = Target > m_Value ? std::min(Target, m_Value + Step) : std::max(Target, m_Value - Step);
				}
			}
		}

		if(m_Value < 0.002f)
			m_Value = 0.0f;
		else if(m_Value > 0.998f)
			m_Value = 1.0f;
		return EasedValue();
	}

	float EasedValue() const
	{
		return SmoothUiEaseOutCubic(m_Value);
	}

	bool IsVisible() const { return m_Value > 0.0f || m_TargetOpen; }
	bool AcceptsInput() const { return m_TargetOpen && m_Value >= 0.98f; }
	bool TargetOpen() const { return m_TargetOpen; }
};

#endif
