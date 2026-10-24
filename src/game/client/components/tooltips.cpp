#include "tooltips.h"

#include <base/time.h>

#include <engine/shared/config.h>

#include <game/client/ui.h>

#include <algorithm>

CTooltips::CTooltips()
{
	CTooltips::OnReset();
}

void CTooltips::OnReset()
{
	m_HoverTime = -1;
	m_Tooltips.clear();
	ClearActiveTooltip();
}

void CTooltips::SetActiveTooltip(CTooltip &Tooltip)
{
	if(!m_ActiveTooltip.has_value() || m_ActiveTooltip.value().get().m_pId != Tooltip.m_pId)
		Tooltip.m_HasAnchor = false;
	m_ActiveTooltip.emplace(Tooltip);
}

inline void CTooltips::ClearActiveTooltip()
{
	m_ActiveTooltip.reset();
	m_PreviousTooltip.reset();
}

void CTooltips::SetFadeTime(const void *pId, float Time)
{
	const auto It = m_Tooltips.find(reinterpret_cast<uintptr_t>(pId));
	if(It != m_Tooltips.end())
		It->second.m_FadeTime = Time;
}

void CTooltips::DoToolTip(const void *pId, const CUIRect *pNearRect, const char *pText, float WidthHint)
{
	uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto &[Entry, WasInserted] = m_Tooltips.emplace(Id, CTooltip{
									  pId,
									  *pNearRect,
									  pText,
									  WidthHint,
									  false});
	CTooltip &Tooltip = Entry->second;

	if(!WasInserted)
	{
		if(Tooltip.m_UseColoredLines || Tooltip.m_pText != pText || Tooltip.m_WidthHint != WidthHint)
			Tooltip.m_HasAnchor = false;
		Tooltip.m_Rect = *pNearRect; // update in case of window resize
		Tooltip.m_pText = pText; // update in case of language change
		Tooltip.m_WidthHint = WidthHint;
		Tooltip.m_vColoredLines.clear();
		Tooltip.m_UseColoredLines = false;
		Tooltip.m_FontSize = 14.0f;
	}

	Tooltip.m_OnScreen = true;

	if(Ui()->HotItem() == Tooltip.m_pId)
	{
		SetActiveTooltip(Tooltip);
	}
}

void CTooltips::DoColoredToolTip(const void *pId, const CUIRect *pNearRect, const std::vector<STooltipColoredLine> &vLines, float FontSize)
{
	const uintptr_t Id = reinterpret_cast<uintptr_t>(pId);
	const auto &[Entry, WasInserted] = m_Tooltips.emplace(Id, CTooltip{
									  pId,
									  *pNearRect,
									  "",
									  -1.0f,
									  false});
	CTooltip &Tooltip = Entry->second;
	if(!WasInserted)
	{
		bool LinesChanged = !Tooltip.m_UseColoredLines || Tooltip.m_FontSize != FontSize || Tooltip.m_vColoredLines.size() != vLines.size();
		for(size_t i = 0; !LinesChanged && i < vLines.size(); ++i)
		{
			const STooltipColoredLine &OldLine = Tooltip.m_vColoredLines[i];
			const STooltipColoredLine &NewLine = vLines[i];
			LinesChanged = OldLine.m_Text != NewLine.m_Text ||
				OldLine.m_Color.r != NewLine.m_Color.r || OldLine.m_Color.g != NewLine.m_Color.g ||
				OldLine.m_Color.b != NewLine.m_Color.b || OldLine.m_Color.a != NewLine.m_Color.a;
		}
		if(LinesChanged)
		{
			Tooltip.m_HasAnchor = false;
			Tooltip.m_vColoredLines = vLines;
		}
		Tooltip.m_Rect = *pNearRect;
		Tooltip.m_pText = "";
		Tooltip.m_WidthHint = -1.0f;
		Tooltip.m_UseColoredLines = true;
		Tooltip.m_FontSize = FontSize;
	}
	else
	{
		Tooltip.m_vColoredLines = vLines;
		Tooltip.m_UseColoredLines = true;
		Tooltip.m_FontSize = FontSize;
	}

	Tooltip.m_OnScreen = true;
	if(Ui()->HotItem() == Tooltip.m_pId)
		SetActiveTooltip(Tooltip);
}

void CTooltips::OnRender()
{
	if(m_ActiveTooltip.has_value())
	{
		CTooltip &Tooltip = m_ActiveTooltip.value();

		if(Ui()->HotItem() != Tooltip.m_pId || !Tooltip.m_Rect.Inside(Ui()->MousePos()))
		{
			Tooltip.m_OnScreen = false;
			ClearActiveTooltip();
			return;
		}
		if(!Tooltip.m_OnScreen)
			return;

		// Reset hover time if a different tooltip is active.
		// Only reset hover time when rendering, because multiple tooltips can be
		// activated in the same frame, but only the last one should be rendered.
		if(!m_PreviousTooltip.has_value() || m_PreviousTooltip.value().get().m_pId != Tooltip.m_pId)
			m_HoverTime = time_get();
		m_PreviousTooltip.emplace(Tooltip);

		// Delay tooltip until 1 second passed. Start fade-in in the last 0.25 seconds.
		const float SecondsBeforeFadeIn = Tooltip.m_FadeTime;
		const float SecondsSinceActivation = (time_get() - m_HoverTime) / (float)time_freq();
		if(SecondsSinceActivation < SecondsBeforeFadeIn)
			return;
		const float SecondsFadeIn = g_Config.m_AmfSmoothHud && Kernel()->RequestInterface<IEngineGraphics>()->WindowActive() && g_Config.m_AmfAnimMenu ?
			std::clamp(g_Config.m_AmfAnimDuration / 1000.0f, 0.06f, 0.30f) :
			0.25f;
		const float AlphaFactor = SecondsSinceActivation < SecondsBeforeFadeIn + SecondsFadeIn ? (SecondsSinceActivation - SecondsBeforeFadeIn) / SecondsFadeIn : 1.0f;

		const float FontSize = Tooltip.m_UseColoredLines ? Tooltip.m_FontSize : 14.0f;
		constexpr float Margin = 5.0f;
		constexpr float Padding = 5.0f;

		CUIRect Rect;
		Rect.w = 0.0f;
		Rect.h = 0.0f;
		if(Tooltip.m_UseColoredLines)
		{
			for(const STooltipColoredLine &Line : Tooltip.m_vColoredLines)
				Rect.w = std::max(Rect.w, TextRender()->TextWidth(FontSize, Line.m_Text.c_str()));
			Rect.h = static_cast<float>(Tooltip.m_vColoredLines.size()) * (FontSize + 2.0f);
			Rect.w += 2.0f * Padding;
			Rect.h += 2.0f * Padding;
		}
	else
		{
			const STextBoundingBox BoundingBox = TextRender()->TextBoundingBox(FontSize, Tooltip.m_pText, -1, Tooltip.m_WidthHint);
			Rect.w = BoundingBox.m_W + 2 * Padding;
			Rect.h = BoundingBox.m_H + 2 * Padding;
		}

		const CUIRect *pScreen = Ui()->Screen();
		Rect.w = std::min(Rect.w, pScreen->w - 2 * Margin);
		Rect.h = std::min(Rect.h, pScreen->h - 2 * Margin);

		if(!Tooltip.m_HasAnchor)
		{
			const float CenterX = Tooltip.m_Rect.x + Tooltip.m_Rect.w * 0.5f;
			const float CenterY = Tooltip.m_Rect.y + Tooltip.m_Rect.h * 0.5f;
			if(Tooltip.m_Rect.y - Rect.h - Margin > pScreen->y)
			{
				Rect.x = std::clamp(CenterX - Rect.w / 2.0f, Margin, pScreen->w - Rect.w - Margin);
				Rect.y = Tooltip.m_Rect.y - Rect.h - Margin;
			}
			else if(Tooltip.m_Rect.y + Tooltip.m_Rect.h + Margin < pScreen->h)
			{
				Rect.x = std::clamp(CenterX - Rect.w / 2.0f, Margin, pScreen->w - Rect.w - Margin);
				Rect.y = Tooltip.m_Rect.y + Tooltip.m_Rect.h + Margin;
			}
			else if(Tooltip.m_Rect.x + Tooltip.m_Rect.w + Margin + Rect.w < pScreen->w)
			{
				Rect.x = Tooltip.m_Rect.x + Tooltip.m_Rect.w + Margin;
				Rect.y = std::clamp(CenterY - Rect.h / 2.0f, Margin, pScreen->h - Rect.h - Margin);
			}
			else
			{
				Rect.x = std::clamp(Tooltip.m_Rect.x - Rect.w - Margin, Margin, pScreen->w - Rect.w - Margin);
				Rect.y = std::clamp(CenterY - Rect.h / 2.0f, Margin, pScreen->h - Rect.h - Margin);
			}
			Tooltip.m_AnchorRect = Rect;
			Tooltip.m_HasAnchor = true;
		}
		else
			Rect = Tooltip.m_AnchorRect;

		Rect.Draw(ColorRGBA(0.2f, 0.2f, 0.2f, 0.8f * AlphaFactor), IGraphics::CORNER_ALL, Padding);
		Rect.Margin(Padding, &Rect);

		if(Tooltip.m_UseColoredLines)
		{
			for(const STooltipColoredLine &ColoredLine : Tooltip.m_vColoredLines)
			{
				CUIRect LineRect;
				Rect.HSplitTop(FontSize + 2.0f, &LineRect, &Rect);
				SLabelProperties Props;
				Props.SetColor(ColoredLine.m_Color.WithMultipliedAlpha(AlphaFactor));
				Ui()->DoLabel(&LineRect, ColoredLine.m_Text.c_str(), FontSize, TEXTALIGN_ML, Props);
			}
		}
		else
		{
			CTextCursor Cursor;
			Cursor.SetPosition(Rect.TopLeft());
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = Tooltip.m_WidthHint;

			STextContainerIndex TextContainerIndex;
			const unsigned OldRenderFlags = TextRender()->GetRenderFlags();
			TextRender()->SetRenderFlags(OldRenderFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
			TextRender()->CreateTextContainer(TextContainerIndex, &Cursor, Tooltip.m_pText);
			TextRender()->SetRenderFlags(OldRenderFlags);

			if(TextContainerIndex.Valid())
			{
				ColorRGBA TextColor = TextRender()->DefaultTextColor();
				TextColor.a *= AlphaFactor;
				ColorRGBA OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= AlphaFactor;
				TextRender()->RenderTextContainer(TextContainerIndex, TextColor, OutlineColor);
			}

			TextRender()->DeleteTextContainer(TextContainerIndex);
		}

		Tooltip.m_OnScreen = false;
	}
}
