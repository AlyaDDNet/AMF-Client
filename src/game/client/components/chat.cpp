/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"

#include <base/color.h>
#include <base/io.h>
#include <base/log.h>
#include <base/time.h>

#include <engine/editor.h>
#include <engine/friends.h>
#include <engine/external/regex.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/components/censor.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/smooth_ui.h>
#include <game/localization.h>

char CChat::ms_aDisplayText[CChat::MAX_LINE_LENGTH] = "";

static constexpr int CHAT_TYPING_ANIM_MAX_TEXT_BYTES = 16;

static bool ChatTypingAnimSupportsText(const char *pText)
{
	for(const char *pScan = pText; *pScan;)
	{
		const char *pBefore = pScan;
		const int Codepoint = str_utf8_decode(&pScan);
		if(Codepoint < 0 || pScan <= pBefore)
			return false;
	}
	return true;
}

CChat::CLine::CLine()
{
	m_TextContainerIndex.Reset();
	m_QuadContainerIndex = -1;
}

void CChat::CLine::Reset(CChat &This)
{
	This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
	m_Initialized = false;
	m_Time = 0;
	m_aText[0] = '\0';
	m_aName[0] = '\0';
	m_Friend = false;
	m_TimesRepeated = 0;
	m_pManagedTeeRenderInfo = nullptr;
	m_pTranslateResponse = nullptr;
}

CChat::CChat()
{
	m_Mode = MODE_NONE;
	m_vSmoothHudTypingGlyphAnims.reserve(32);
	m_vSmoothHudTypedCharacterColorSplits.reserve(32);

	m_Input.SetCalculateOffsetCallback([this]() { return m_IsInputCensored; });
	m_Input.SetDisplayTextCallback([this](char *pStr, size_t NumChars) {
		m_IsInputCensored = false;
		if(
			g_Config.m_ClStreamerMode &&
			(str_startswith(pStr, "/login ") ||
				str_startswith(pStr, "/register ") ||
				str_startswith(pStr, "/code ") ||
				str_startswith(pStr, "/timeout ") ||
				str_startswith(pStr, "/save ") ||
				str_startswith(pStr, "/load ")))
		{
			bool Censor = false;
			const size_t NumLetters = std::min(NumChars, sizeof(ms_aDisplayText) - 1);
			for(size_t i = 0; i < NumLetters; ++i)
			{
				if(Censor)
					ms_aDisplayText[i] = '*';
				else
					ms_aDisplayText[i] = pStr[i];
				if(pStr[i] == ' ')
				{
					Censor = true;
					m_IsInputCensored = true;
				}
			}
			ms_aDisplayText[NumLetters] = '\0';
			return ms_aDisplayText;
		}
		return pStr;
	});
}

bool CChat::ChatInteractionActive() const
{
	return Kernel()->RequestInterface<IEngineGraphics>()->WindowActive() && m_Mode != MODE_NONE && Client()->State() == IClient::STATE_ONLINE;
}

void CChat::ResetSmoothHudState(bool RestoreMouseMode)
{
	m_SmoothHudPendingMessageClick = false;
	m_SmoothHudContextMenuOpen = false;
	m_SmoothHudHoveredLine = -1;
	m_SmoothHudSelectedLine = -1;
	m_SmoothHudHoverAlpha = 0.0f;
	m_SmoothHudSelectedAlpha = 0.0f;
	m_SmoothHudContextAlpha = 0.0f;
	m_SmoothHudHistoryScroll = 0.0f;
	m_SmoothHudHistoryScrollTarget = 0.0f;
	m_SmoothHudHistoryScrollMax = 0.0f;
	m_SmoothHudContextX = 0.0f;
	m_SmoothHudContextY = 0.0f;
	m_SmoothHudContextW = 0.0f;
	m_SmoothHudContextH = 0.0f;
	m_SmoothHudCaretPos = vec2(0.0f, 0.0f);
	m_SmoothHudCaretInitialized = false;
	m_SmoothHudChatOpenedTime = 0;
	ResetSmoothHudTypingAnimation();
	m_aSmoothHudPreviousInputText[0] = '\0';
	m_aSmoothHudNotification[0] = '\0';
	m_SmoothHudNotificationTime = 0;
	m_Input.SetHideCaret(false);

	if(m_SmoothHudCursorActive)
	{
		m_SmoothHudCursorActive = false;
		if(RestoreMouseMode)
			Input()->MouseModeRelative();
	}
}

void CChat::ResetSmoothHudTypingAnimation()
{
	m_vSmoothHudTypingGlyphAnims.clear();
	m_vSmoothHudTypedCharacterColorSplits.clear();
}

void CChat::SyncSmoothHudTypingAnimationBaseline()
{
	ResetSmoothHudTypingAnimation();
	str_copy(m_aSmoothHudPreviousInputText, m_Input.GetDisplayedString());
}

void CChat::RefreshSmoothHudTypingAnimation()
{
	const bool WindowActive = Kernel()->RequestInterface<IEngineGraphics>()->WindowActive();
	if(m_Mode == MODE_NONE || !g_Config.m_AmfSmoothHud || !WindowActive || !g_Config.m_AmfAnimText || m_Input.HasSelection() || Input()->HasComposition())
	{
		SyncSmoothHudTypingAnimationBaseline();
		return;
	}

	const char *pCurrent = m_Input.GetDisplayedString();
	const size_t CurrentLen = str_length(pCurrent);
	const size_t PreviousLen = str_length(m_aSmoothHudPreviousInputText);

	if(!ChatTypingAnimSupportsText(pCurrent) || !ChatTypingAnimSupportsText(m_aSmoothHudPreviousInputText))
	{
		SyncSmoothHudTypingAnimationBaseline();
		return;
	}

	if(str_comp(pCurrent, m_aSmoothHudPreviousInputText) == 0)
		return;

	if(CurrentLen == 0)
	{
		SyncSmoothHudTypingAnimationBaseline();
		return;
	}

	// Determine the edited range at UTF-8 codepoint boundaries. This is the
	// same state model used by Best Client, so existing glyphs keep their
	// animation state when another glyph is inserted.
	size_t PrefixBytes = 0;
	{
		const char *pCurScan = pCurrent;
		const char *pPrevScan = m_aSmoothHudPreviousInputText;
		while(*pCurScan && *pPrevScan)
		{
			const char *pCurBefore = pCurScan;
			const char *pPrevBefore = pPrevScan;
			const int CurCp = str_utf8_decode(&pCurScan);
			const int PrevCp = str_utf8_decode(&pPrevScan);
			if(CurCp != PrevCp)
			{
				pCurScan = pCurBefore;
				pPrevScan = pPrevBefore;
				break;
			}
			PrefixBytes = (size_t)(pCurScan - pCurrent);
		}
	}

	size_t SuffixBytesCur = 0;
	size_t SuffixBytesPrev = 0;
	{
		int CurCursor = (int)CurrentLen;
		int PrevCursor = (int)PreviousLen;
		while(CurCursor > (int)PrefixBytes && PrevCursor > (int)PrefixBytes)
		{
			const int CurBefore = CurCursor;
			const int PrevBefore = PrevCursor;
			CurCursor = str_utf8_rewind(pCurrent, CurCursor);
			PrevCursor = str_utf8_rewind(m_aSmoothHudPreviousInputText, PrevCursor);

			const char *pCurCpPtr = pCurrent + CurCursor;
			const char *pPrevCpPtr = m_aSmoothHudPreviousInputText + PrevCursor;
			const int CurCp = str_utf8_decode(&pCurCpPtr);
			const int PrevCp = str_utf8_decode(&pPrevCpPtr);
			if(CurCp != PrevCp)
			{
				CurCursor = CurBefore;
				PrevCursor = PrevBefore;
				break;
			}

			SuffixBytesCur = CurrentLen - (size_t)CurCursor;
			SuffixBytesPrev = PreviousLen - (size_t)PrevCursor;
		}
	}

	const size_t RemovedBytes = PreviousLen - PrefixBytes - SuffixBytesPrev;
	const size_t InsertedBytes = CurrentLen - PrefixBytes - SuffixBytesCur;
	const int EditOldEndByte = (int)(PrefixBytes + RemovedBytes);
	const int DeltaBytes = (int)InsertedBytes - (int)RemovedBytes;

	for(auto It = m_vSmoothHudTypingGlyphAnims.begin(); It != m_vSmoothHudTypingGlyphAnims.end();)
	{
		const int AnimEndByte = It->m_ByteIndex + It->m_ByteLength;
		if(It->m_ByteIndex >= (int)PrefixBytes && AnimEndByte <= EditOldEndByte)
		{
			It = m_vSmoothHudTypingGlyphAnims.erase(It);
			continue;
		}
		if(It->m_ByteIndex >= EditOldEndByte)
			It->m_ByteIndex += DeltaBytes;

		if(It->m_ByteIndex < 0 || It->m_ByteLength <= 0 || It->m_ByteIndex + It->m_ByteLength > (int)CurrentLen ||
			str_length(It->m_aText) != It->m_ByteLength ||
			str_comp_num(It->m_aText, pCurrent + It->m_ByteIndex, It->m_ByteLength) != 0)
		{
			It = m_vSmoothHudTypingGlyphAnims.erase(It);
			continue;
		}
		++It;
	}

	if(InsertedBytes > 0)
	{
		for(int ByteIndex = (int)PrefixBytes; ByteIndex < (int)(PrefixBytes + InsertedBytes);)
		{
			const int NextByteIndex = str_utf8_forward(pCurrent, ByteIndex);
			const int GlyphBytes = std::min(NextByteIndex - ByteIndex, CHAT_TYPING_ANIM_MAX_TEXT_BYTES - 1);
			if(GlyphBytes > 0)
			{
				STypingGlyphAnim Anim;
				Anim.m_StartTime = time_get();
				Anim.m_ByteIndex = ByteIndex;
				Anim.m_ByteLength = GlyphBytes;
				str_truncate(Anim.m_aText, sizeof(Anim.m_aText), pCurrent + ByteIndex, GlyphBytes);
				m_vSmoothHudTypingGlyphAnims.push_back(Anim);
			}
			ByteIndex = NextByteIndex;
		}
	}

	str_copy(m_aSmoothHudPreviousInputText, pCurrent);
}

void CChat::UpdateSmoothHudMousePosition(float Width, float Height)
{
	const vec2 NativeMousePos = Input()->NativeMousePos();
	const float WindowWidth = std::max((float)Graphics()->WindowWidth(), 1.0f);
	const float WindowHeight = std::max((float)Graphics()->WindowHeight(), 1.0f);
	m_SmoothHudMousePos = vec2(
		NativeMousePos.x * Width / WindowWidth,
		NativeMousePos.y * Height / WindowHeight);
}

bool CChat::SmoothHudContextContainsMouse() const
{
	return m_SmoothHudContextMenuOpen &&
		m_SmoothHudMousePos.x >= m_SmoothHudContextX && m_SmoothHudMousePos.x <= m_SmoothHudContextX + m_SmoothHudContextW &&
		m_SmoothHudMousePos.y >= m_SmoothHudContextY && m_SmoothHudMousePos.y <= m_SmoothHudContextY + m_SmoothHudContextH;
}

bool CChat::SmoothHudLineHasPlayerAuthor(const CLine &Line) const
{
	return Line.m_Initialized && Line.m_ClientId >= 0 && Line.m_ClientId < MAX_CLIENTS &&
		GameClient()->m_aClients[Line.m_ClientId].m_Active && GameClient()->m_aClients[Line.m_ClientId].m_aName[0] != '\0';
}

bool CChat::SmoothHudContextActionEnabled(int Action) const
{
	if(m_SmoothHudSelectedLine < 0 || m_SmoothHudSelectedLine >= MAX_LINES)
		return false;
	const CLine &Line = m_aLines[m_SmoothHudSelectedLine];
	if(!Line.m_Initialized)
		return false;
	if(Action == 0)
		return true; // Copy is valid for every visible chat line.
	if(!SmoothHudLineHasPlayerAuthor(Line))
		return false;
	if(Action == 1)
		return true;
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	return Line.m_ClientId != LocalClientId && Line.m_ClientId != GameClient()->m_aLocalIds[1];
}

void CChat::ExecuteSmoothHudContextAction(int Action)
{
	if(Action < 0 || Action > 3 || !SmoothHudContextActionEnabled(Action))
		return;

	CLine &Line = m_aLines[m_SmoothHudSelectedLine];

	if(Action == 0) // Copy
	{
		char aText[static_cast<int>(MAX_NAME_LENGTH) + MAX_LINE_LENGTH + 4];
		str_format(aText, sizeof(aText), "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);
		Input()->SetClipboardText(aText);
		str_copy(m_aSmoothHudNotification, TCLocalize("Message copied", "AMF Client"), sizeof(m_aSmoothHudNotification));
		m_SmoothHudNotificationTime = time_get();
	}
	else if(Action == 1) // Reply
	{
		const char *pPlayerName = GameClient()->m_aClients[Line.m_ClientId].m_aName;
		char aPrefix[MAX_NAME_LENGTH + 3];
		str_format(aPrefix, sizeof(aPrefix), "%s: ", pPlayerName);
		const size_t CursorOffset = m_Input.GetCursorOffset();
		const size_t PrefixLength = str_length(aPrefix);
		const char *pInput = m_Input.GetString();
		if(CursorOffset < PrefixLength || str_comp_num(pInput + CursorOffset - PrefixLength, aPrefix, PrefixLength) != 0)
			m_Input.Insert(aPrefix, CursorOffset);
	}
	else if(Action == 2) // Mute / unmute
	{
		const char *pPlayerName = GameClient()->m_aClients[Line.m_ClientId].m_aName;
		const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
		if(Line.m_ClientId >= 0 && Line.m_ClientId != LocalClientId && Line.m_ClientId != GameClient()->m_aLocalIds[1])
		{
			// This is the direct equivalent of TClient's !mute path. Passing the
			// name as data instead of constructing a console line prevents command
			// injection through a player name.
			const bool IsMuted = GameClient()->Foes()->IsFriend(pPlayerName, "", true);
			if(IsMuted)
			{
				GameClient()->Foes()->RemoveFriend(pPlayerName, "");
				GameClient()->m_aClients[Line.m_ClientId].m_Foe = false;
				str_copy(m_aSmoothHudNotification, TCLocalize("Player unmuted", "AMF Client"), sizeof(m_aSmoothHudNotification));
			}
			else
			{
				GameClient()->Foes()->AddFriend(pPlayerName, "");
				GameClient()->m_aClients[Line.m_ClientId].m_Foe = true;
				str_copy(m_aSmoothHudNotification, TCLocalize("Player muted", "AMF Client"), sizeof(m_aSmoothHudNotification));
			}
			m_SmoothHudNotificationTime = time_get();
		}
	}
	else if(Action == 3) // Block / unblock through the real war list
	{
		const char *pPlayerName = GameClient()->m_aClients[Line.m_ClientId].m_aName;
		const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
		if(Line.m_ClientId >= 0 && Line.m_ClientId != LocalClientId && Line.m_ClientId != GameClient()->m_aLocalIds[1])
		{
			// Index 1 is the existing built-in "enemy" group used by !war.
			// Use the war-list API directly, so this never becomes a public chat
			// command and does not rely on a temporary UI-side flag.
			const bool IsBlocked = GameClient()->m_WarList.FindWarEntry(pPlayerName, "", "enemy") != nullptr;
			if(IsBlocked)
			{
				GameClient()->m_WarList.RemoveWarEntryInGame(1, pPlayerName, false);
				str_copy(m_aSmoothHudNotification, TCLocalize("Player unblocked", "AMF Client"), sizeof(m_aSmoothHudNotification));
			}
			else
			{
				GameClient()->m_WarList.AddWarEntryInGame(1, pPlayerName, "", false);
				str_copy(m_aSmoothHudNotification, TCLocalize("Player blocked", "AMF Client"), sizeof(m_aSmoothHudNotification));
			}
			GameClient()->m_WarList.UpdateWarPlayers();
			m_SmoothHudNotificationTime = time_get();
		}
	}

	// Keep the menu open for stateful actions. The next render reads the real
	// Foes/WarList state and immediately updates the action label.
	if(Action <= 1)
	{
		m_SmoothHudContextMenuOpen = false;
		m_SmoothHudContextAlpha = 0.0f;
	}
}

void CChat::RenderSmoothHudContextMenu(float Width, float Height)
{
	if(!m_SmoothHudContextMenuOpen || m_SmoothHudSelectedLine < 0 || m_SmoothHudSelectedLine >= MAX_LINES)
		return;

	const CLine &Line = m_aLines[m_SmoothHudSelectedLine];
	if(!Line.m_Initialized)
	{
		m_SmoothHudContextMenuOpen = false;
		return;
	}
	const bool HasPlayerAuthor = SmoothHudLineHasPlayerAuthor(Line);
	const bool IsOwnMessage = HasPlayerAuthor && (Line.m_ClientId == GameClient()->m_Snap.m_LocalClientId || Line.m_ClientId == GameClient()->m_aLocalIds[1]);
	const int ActionCount = 4;
	constexpr float ItemHeight = 12.0f;
	constexpr float Padding = 2.0f;
	const char *pPlayerName = HasPlayerAuthor ? GameClient()->m_aClients[Line.m_ClientId].m_aName : "";
	const bool IsMuted = HasPlayerAuthor && !IsOwnMessage && GameClient()->Foes()->IsFriend(pPlayerName, "", true);
	const bool IsBlocked = HasPlayerAuthor && !IsOwnMessage && GameClient()->m_WarList.FindWarEntry(pPlayerName, "", "enemy") != nullptr;

	const char *apActions[] = {
		TCLocalize("Copy", "AMF Client"),
		TCLocalize("Reply", "AMF Client"),
		TCLocalize(IsMuted ? "Unmute" : "Mute", "AMF Client"),
		TCLocalize(IsBlocked ? "Unblock" : "Block", "AMF Client")};
	const bool aEnabled[] = {true, HasPlayerAuthor, HasPlayerAuthor && !IsOwnMessage, HasPlayerAuthor && !IsOwnMessage};
	constexpr float FontSize = 8.0f;
	constexpr float TextPadding = 3.0f;
	float MenuWidth = 0.0f;
	for(int Action = 0; Action < ActionCount; ++Action)
		MenuWidth = std::max(MenuWidth, TextRender()->TextWidth(FontSize, apActions[Action]));
	// The label width is derived from the current localization. The upper
	// bound and ellipsis below still keep the popup usable on very small UI
	// layouts instead of allowing a translated action to escape the screen.
	MenuWidth = std::clamp(MenuWidth + TextPadding * 2.0f, 38.0f, std::max(38.0f, Width - 4.0f));
	const float MenuHeight = ActionCount * ItemHeight + Padding * 2.0f;

	m_SmoothHudContextW = MenuWidth;
	m_SmoothHudContextH = MenuHeight;
	m_SmoothHudContextX = std::clamp(m_SmoothHudContextX, 2.0f, std::max(2.0f, Width - MenuWidth - 2.0f));
	m_SmoothHudContextY = std::clamp(m_SmoothHudContextY, 2.0f, std::max(2.0f, Height - MenuHeight - 2.0f));
	const bool AnimateContextMenu = g_Config.m_AmfSmoothHud && Kernel()->RequestInterface<IEngineGraphics>()->WindowActive() && g_Config.m_AmfAnimMenu;
	if(AnimateContextMenu)
		m_SmoothHudContextAlpha = SmoothApproachDuration(m_SmoothHudContextAlpha, 1.0f, Client()->RenderFrameTime(), std::max(g_Config.m_AmfAnimDuration, 60));
	else
		m_SmoothHudContextAlpha = 1.0f;
	const float Alpha = m_SmoothHudContextAlpha;

	CUIRect MenuRect = {m_SmoothHudContextX, m_SmoothHudContextY, MenuWidth, MenuHeight};
	if(g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimBlur && g_Config.m_AmfAnimBlurRadius > 0)
	{
		// A deliberately inexpensive visual analogue of backdrop blur. It is a
		// single dimmed container behind an animated popup, never a framebuffer
		// pass, so it cannot blur text, the caret, or the DDNet UI cursor.
		CUIRect Backdrop = MenuRect;
		const float Spread = std::min((float)g_Config.m_AmfAnimBlurRadius * 0.25f, 3.0f);
		Backdrop.x -= Spread;
		Backdrop.y -= Spread;
		Backdrop.w += Spread * 2.0f;
		Backdrop.h += Spread * 2.0f;
		Backdrop.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.035f * g_Config.m_AmfAnimBlurRadius * Alpha), IGraphics::CORNER_ALL, 3.0f);
	}
	MenuRect.Draw(ColorRGBA(0.28f, 0.40f, 0.64f, 0.78f * Alpha), IGraphics::CORNER_ALL, 2.0f);
	CUIRect MenuInner;
	MenuRect.Margin(1.0f, &MenuInner);
	MenuInner.Draw(ColorRGBA(0.055f, 0.07f, 0.11f, 0.97f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	for(int Action = 0; Action < ActionCount; ++Action)
	{
		CUIRect Item = {MenuRect.x + Padding, MenuRect.y + Padding + Action * ItemHeight, MenuRect.w - Padding * 2.0f, ItemHeight};
		if(aEnabled[Action] && Item.Inside(m_SmoothHudMousePos))
			Item.Draw(ColorRGBA(0.22f, 0.35f, 0.58f, 0.50f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		Item.VMargin(TextPadding, &Item);
		SLabelProperties ActionProps;
		ActionProps.m_MaxWidth = Item.w;
		ActionProps.m_EllipsisAtEnd = true;
		ActionProps.SetColor(aEnabled[Action] ? ColorRGBA(0.94f, 0.96f, 1.0f, Alpha) : ColorRGBA(0.52f, 0.56f, 0.62f, 0.62f * Alpha));
		Ui()->DoLabel(&Item, apActions[Action], FontSize, TEXTALIGN_ML, ActionProps);
	}
}

void CChat::RegisterCommand(const char *pName, const char *pParams, const char *pHelpText)
{
	// Don't allow duplicate commands.
	for(const auto &Command : m_vServerCommands)
		if(str_comp(Command.m_aName, pName) == 0)
			return;

	m_vServerCommands.emplace_back(pName, pParams, pHelpText);
	m_ServerCommandsNeedSorting = true;
}

void CChat::UnregisterCommand(const char *pName)
{
	m_vServerCommands.erase(std::remove_if(m_vServerCommands.begin(), m_vServerCommands.end(), [pName](const CCommand &Command) { return str_comp(Command.m_aName, pName) == 0; }), m_vServerCommands.end());
}

void CChat::RebuildChat()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized)
			continue;
		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		// recalculate sizes
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}
}

void CChat::ClearLines()
{
	for(auto &Line : m_aLines)
		Line.Reset(*this);
	m_PrevScoreBoardShowed = false;
	m_PrevShowChat = false;
}

void CChat::OnWindowResize()
{
	RebuildChat();
	if(m_SmoothHudCursorActive)
	{
		m_SmoothHudHistoryScroll = 0.0f;
		m_SmoothHudHistoryScrollTarget = 0.0f;
	}
}

void CChat::Reset()
{
	ClearLines();

	m_Show = false;
	m_CompletionUsed = false;
	m_CompletionChosen = -1;
	m_aCompletionBuffer[0] = 0;
	m_PlaceholderOffset = 0;
	m_PlaceholderLength = 0;
	m_pHistoryEntry = nullptr;
	m_PendingChatCounter = 0;
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_ServerSupportsCommandInfo = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	DisableMode();
	ResetSmoothHudState(false);
	m_vServerCommands.clear();

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::OnRelease()
{
	m_Show = false;
	if(m_Mode == MODE_NONE)
		ResetSmoothHudState(true);
}

void CChat::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChat::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(0, pResult->GetString(0));
}

void CChat::ConSayTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(1, pResult->GetString(0));
}

void CChat::ConChat(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMode = pResult->GetString(0);
	if(str_comp(pMode, "all") == 0)
		((CChat *)pUserData)->EnableMode(0);
	else if(str_comp(pMode, "team") == 0)
		((CChat *)pUserData)->EnableMode(1);
	else
		log_error("chat", "expected all or team as mode");

	if(pResult->GetString(1)[0] || g_Config.m_ClChatReset)
		((CChat *)pUserData)->m_Input.Set(pResult->GetString(1));
}

void CChat::ConShowChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->m_Show = pResult->GetInteger(0) != 0;
}

void CChat::ConEcho(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->Echo(pResult->GetString(0));
}

void CChat::ConClearChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->ClearLines();
}

void CChat::ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CChat *)pUserData)->RebuildChat();
}

void CChat::ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentWidth();
	pChat->RebuildChat();
}

void CChat::ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentFontSize();
	pChat->RebuildChat();
}

void CChat::Echo(const char *pString)
{
	AddLine(CLIENT_MSG, 0, pString);
}

void CChat::OnConsoleInit()
{
	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("chat", "s['team'|'all'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
}

bool CChat::OnInput(const IInput::CEvent &Event)
{
	if(m_Mode == MODE_NONE)
		return false;

	// The chat owns TAB presses for the original completion state machine.
	// CGameClient deliberately forwards release events to every component, so
	// an already held scoreboard still closes on the physical TAB release.
	// Chat interaction must not alter either part of that ordering.
	const bool ChatInteraction = ChatInteractionActive();

	if(ChatInteraction && (Event.m_Flags & IInput::FLAG_PRESS))
	{
		const float Height = 300.0f;
		UpdateSmoothHudMousePosition(Height * Graphics()->ScreenAspect(), Height);

		if(Event.m_Key == KEY_ESCAPE && m_SmoothHudContextMenuOpen)
		{
			m_SmoothHudContextMenuOpen = false;
			m_SmoothHudContextAlpha = 0.0f;
			m_SmoothHudPendingMessageClick = false;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_SmoothHudHistoryScrollTarget = std::clamp(m_SmoothHudHistoryScrollTarget + FontSize() * 3.0f, 0.0f, m_SmoothHudHistoryScrollMax);
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			m_SmoothHudHistoryScrollTarget = std::clamp(m_SmoothHudHistoryScrollTarget - FontSize() * 3.0f, 0.0f, m_SmoothHudHistoryScrollMax);
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_2)
		{
			// Only the right mouse button opens a message context menu. A second
			// right-click outside it closes the menu or selects another line.
			if(m_SmoothHudContextMenuOpen)
			{
				m_SmoothHudContextMenuOpen = false;
				m_SmoothHudContextAlpha = 0.0f;
				m_SmoothHudPendingMessageClick = true;
			}
			else
				m_SmoothHudPendingMessageClick = true;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_1)
		{
			if(m_SmoothHudContextMenuOpen)
			{
				if(SmoothHudContextContainsMouse())
				{
					constexpr float ItemHeight = 12.0f;
					constexpr float Padding = 2.0f;
					const int Action = (int)((m_SmoothHudMousePos.y - m_SmoothHudContextY - Padding) / ItemHeight);
					constexpr int ActionCount = 4;
					if(Action >= 0 && Action < ActionCount)
						ExecuteSmoothHudContextAction(Action);
				}
				else
				{
					m_SmoothHudContextMenuOpen = false;
					m_SmoothHudContextAlpha = 0.0f;
				}
			}
			return true;
		}
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		DisableMode();
		GameClient()->OnRelease();
		if(g_Config.m_ClChatReset)
		{
			m_Input.Clear();
			m_pHistoryEntry = nullptr;
		}
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		if(m_ServerCommandsNeedSorting)
		{
			std::sort(m_vServerCommands.begin(), m_vServerCommands.end());
			m_ServerCommandsNeedSorting = false;
		}

		if(GameClient()->m_BindChat.ChatDoBinds(m_Input.GetString()))
			; // Do nothing as bindchat was executed
		else if(GameClient()->m_TClient.ChatDoSpecId(m_Input.GetString()))
			; // Do nothing as specid was executed
		else
			SendChatQueued(m_Input.GetString());
		m_pHistoryEntry = nullptr;
		DisableMode();
		GameClient()->OnRelease();
		m_Input.Clear();
	}
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_TAB)
	{
		const bool ShiftPressed = Input()->ShiftIsPressed();

		// fill the completion buffer
		if(!m_CompletionUsed)
		{
			const char *pCursor = m_Input.GetString() + m_Input.GetCursorOffset();
			for(size_t Count = 0; Count < m_Input.GetCursorOffset() && *(pCursor - 1) != ' '; --pCursor, ++Count)
				;
			m_PlaceholderOffset = pCursor - m_Input.GetString();

			for(m_PlaceholderLength = 0; *pCursor && *pCursor != ' '; ++pCursor)
				++m_PlaceholderLength;

			str_truncate(m_aCompletionBuffer, sizeof(m_aCompletionBuffer), m_Input.GetString() + m_PlaceholderOffset, m_PlaceholderLength);
		}

		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/')
		{
			// Create the completion list of player names through which the player can iterate
			const char *PlayerName, *FoundInput;
			m_PlayerCompletionListLength = 0;
			for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
			{
				if(PlayerInfo)
				{
					PlayerName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
					FoundInput = str_utf8_find_nocase(PlayerName, m_aCompletionBuffer);
					if(FoundInput != nullptr)
					{
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_ClientId = PlayerInfo->m_ClientId;
						// The score for suggesting a player name is determined by the distance of the search input to the beginning of the player name
						m_aPlayerCompletionList[m_PlayerCompletionListLength].m_Score = (int)(FoundInput - PlayerName);
						m_PlayerCompletionListLength++;
					}
				}
			}
			std::stable_sort(m_aPlayerCompletionList, m_aPlayerCompletionList + m_PlayerCompletionListLength,
				[](const CRateablePlayer &Player1, const CRateablePlayer &Player2) -> bool {
					return Player1.m_Score < Player2.m_Score;
				});
		}

		if(GameClient()->m_BindChat.ChatDoAutocomplete(ShiftPressed))
		{
		}
		else if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())
		{
			CCommand *pCompletionCommand = nullptr;

			const size_t NumCommands = m_vServerCommands.size();

			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen + 2 * NumCommands) % (2 * NumCommands);

			m_CompletionUsed = true;

			const char *pCommandStart = m_aCompletionBuffer + 1;
			for(size_t i = 0; i < 2 * NumCommands; ++i)
			{
				int SearchType;
				int Index;

				if(ShiftPressed)
				{
					SearchType = ((m_CompletionChosen - i + 2 * NumCommands) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen - i + NumCommands) % NumCommands;
				}
				else
				{
					SearchType = ((m_CompletionChosen + i) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen + i) % NumCommands;
				}

				auto &Command = m_vServerCommands[Index];

				if(str_startswith_nocase(Command.m_aName, pCommandStart))
				{
					pCompletionCommand = &Command;
					m_CompletionChosen = Index + SearchType * NumCommands;
					break;
				}
			}

			// insert the command
			if(pCompletionCommand)
			{
		char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// add the command
				str_append(aBuf, "/");
				str_append(aBuf, pCompletionCommand->m_aName);

				// add separator
				const char *pSeparator = pCompletionCommand->m_aParams[0] == '\0' ? "" : " ";
				str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionCommand->m_aName) + 1;
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else
		{
			// find next possible name
			const char *pCompletionString = nullptr;
			if(m_PlayerCompletionListLength > 0)
			{
				// We do this in a loop, if a player left the game during the repeated pressing of Tab, they are skipped
				CGameClient::CClientData *pCompletionClientData;
				for(int i = 0; i < m_PlayerCompletionListLength; ++i)
				{
					if(ShiftPressed && m_CompletionUsed)
					{
						m_CompletionChosen--;
					}
					else if(!ShiftPressed)
					{
						m_CompletionChosen++;
					}
					if(m_CompletionChosen < 0)
					{
						m_CompletionChosen += m_PlayerCompletionListLength;
					}
					m_CompletionChosen %= m_PlayerCompletionListLength;
					m_CompletionUsed = true;

					pCompletionClientData = &GameClient()->m_aClients[m_aPlayerCompletionList[m_CompletionChosen].m_ClientId];
					if(!pCompletionClientData->m_Active)
					{
						continue;
					}

					pCompletionString = pCompletionClientData->m_aName;
					break;
				}
			}

			// insert the name
			if(pCompletionString)
			{
		char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// quote the name
				char aQuoted[128];
				if((m_Input.GetString()[0] == '/' || GameClient()->m_BindChat.CheckBindChat(m_Input.GetString())) && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
				{
					// escape the name
					str_copy(aQuoted, "\"");
					char *pDst = aQuoted + str_length(aQuoted);
					str_escape(&pDst, pCompletionString, aQuoted + sizeof(aQuoted));
					str_append(aQuoted, "\"");

					pCompletionString = aQuoted;
				}

				// add the name
				str_append(aBuf, pCompletionString);

				// add separator
				const char *pSeparator = "";
				if(*(m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength) != ' ')
					pSeparator = m_PlaceholderOffset == 0 ? ": " : " ";
				else if(m_PlaceholderOffset == 0)
					pSeparator = ":";
				if(*pSeparator)
					str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionString);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
	}
	else
	{
		// reset name completion process
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
		}

		m_Input.ProcessInput(Event);
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_UP)
	{
		if(m_EditingNewLine)
		{
			str_copy(m_aCurrentInputText, m_Input.GetString());
			m_EditingNewLine = false;
		}

		if(m_pHistoryEntry)
		{
			CHistoryEntry *pTest = m_History.Prev(m_pHistoryEntry);

			if(pTest)
				m_pHistoryEntry = pTest;
		}
		else
		{
			m_pHistoryEntry = m_History.Last();
		}

		if(m_pHistoryEntry)
			m_Input.Set(m_pHistoryEntry->m_aText);
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_DOWN)
	{
		if(m_pHistoryEntry)
			m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
		else if(!m_EditingNewLine)
		{
			m_Input.Set(m_aCurrentInputText);
			m_EditingNewLine = true;
		}
	}

	RefreshSmoothHudTypingAnimation();
	return true;
}

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		m_Input.Activate(EInputPriority::CHAT);
		SyncSmoothHudTypingAnimationBaseline();
		m_SmoothHudChatOpenedTime = g_Config.m_AmfSmoothHud ? time_get() : 0;
		Input()->MouseModeAbsolute();
		// SDL normally makes the OS cursor visible in absolute mode. The chat
		// deliberately uses DDNet's own cursor renderer instead, so there is
		// never a second Windows cursor over the interactive history.
		Input()->SetSystemCursorVisible(false);
		m_SmoothHudCursorActive = true;
	}
}

void CChat::DisableMode()
{
	if(m_Mode != MODE_NONE)
	{
		m_Mode = MODE_NONE;
		m_Input.Deactivate();
		ResetSmoothHudState(true);
	}
}

bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	// While chat interaction is active, the mouse is absolute and handled by the
	// chat's own hit testing. Consuming a possible controller cursor here also
	// prevents it from moving the in-game aim.
	return ChatInteractionActive();
}

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

		auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
		if(Re.error().empty() && Re.test(pMsg->m_pMessage))
			return;

		/*
		if(g_Config.m_ClCensorChat)
		{
		char aMessage[MAX_LINE_LENGTH];
			str_copy(aMessage, pMsg->m_pMessage);
			GameClient()->m_Censor.CensorMessage(aMessage);
			AddLine(pMsg->m_ClientId, pMsg->m_Team, aMessage);
		}
		else
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
		*/

		AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pMsg->m_ClientId == SERVER_MSG)
		{
			StoreSave(pMsg->m_pMessage);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(!m_ServerSupportsCommandInfo)
		{
			m_vServerCommands.clear();
			m_ServerSupportsCommandInfo = true;
		}
		RegisterCommand(pMsg->m_pName, pMsg->m_pArgsFormat, pMsg->m_pHelpText);
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		UnregisterCommand(pMsg->m_pName);
	}
}

bool CChat::LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHit = str_utf8_find_nocase(pLine, pName);

	while(pHit)
	{
		int Length = str_length(pName);

		if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
			return true;

		pHit = str_utf8_find_nocase(pHit + 1, pName);
	}

	return false;
}

static constexpr const char *SAVES_HEADER[] = {
	"Time",
	"Player",
	"Map",
	"Code",
};

// TODO: remove this in a few releases (in 2027 or later)
//       it got deprecated by CGameClient::StoreSave
void CChat::StoreSave(const char *pText)
{
	const char *pStart = str_find(pText, "Team successfully saved by ");
	const char *pMid = str_find(pText, ". Use '/load ");
	const char *pOn = str_find(pText, "' on ");
	const char *pEnd = str_find(pText, pOn ? " to continue" : "' to continue");

	if(!pStart || !pMid || !pEnd || pMid < pStart || pEnd < pMid || (pOn && (pOn < pMid || pEnd < pOn)))
		return;

	char aName[16];
	str_truncate(aName, sizeof(aName), pStart + 27, pMid - pStart - 27);

	char aSaveCode[64];

	str_truncate(aSaveCode, sizeof(aSaveCode), pMid + 13, (pOn ? pOn : pEnd) - pMid - 13);

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), TimestampFormat::SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;

	const char *apColumns[4] = {
		aTimestamp,
		aName,
		GameClient()->Map()->BaseName(),
		aSaveCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, 4, SAVES_HEADER);
	}
	CsvWrite(File, 4, apColumns);
	io_close(File);
}

void CChat::AddLine(int ClientId, int Team, const char *pLine)
{
	if(*pLine == 0 ||
		(ClientId == SERVER_MSG && !g_Config.m_ClShowChatSystem) ||
		(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_aName[0] == '\0' || // unknown client
					  GameClient()->m_aClients[ClientId].m_ChatIgnore ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && GameClient()->m_aClients[ClientId].m_Foe))))
		return;

	// TClient
	if(ClientId == CLIENT_MSG && !g_Config.m_TcShowChatClient)
		return;

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *pStr = pLine;
	const char *pEnd = nullptr;
	while(*pStr)
	{
		const char *pStrOld = pStr;
		int Code = str_utf8_decode(&pStr);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = nullptr;
		}
		else if(pEnd == nullptr)
		{
			pEnd = pStrOld;
		}

		if(++Length >= MAX_LINE_LENGTH)
		{
			*(const_cast<char *>(pStr)) = '\0';
			break;
		}
	}
	if(pEnd != nullptr)
		*(const_cast<char *>(pEnd)) = '\0';

	if(*pLine == 0)
		return;

	bool Highlighted = false;

	auto &&FChatMsgCheckAndPrint = [this](const CLine &Line) {
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "%s%s%s", Line.m_aName, Line.m_ClientId >= 0 ? ": " : "", Line.m_aText);
		ColorRGBA ChatLogColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(Line.m_Highlighted)
		{
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		}
		else
		{
			if(Line.m_Friend && g_Config.m_ClMessageFriend)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			else if(Line.m_Team)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			else if(Line.m_ClientId == SERVER_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			else if(Line.m_ClientId == CLIENT_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			else // regular message
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		}

		const char *pFrom;
		if(Line.m_Whisper)
			pFrom = "chat/whisper";
		else if(Line.m_Team)
			pFrom = "chat/team";
		else if(Line.m_ClientId == SERVER_MSG)
			pFrom = "chat/server";
		else if(Line.m_ClientId == CLIENT_MSG)
			pFrom = "chat/client";
		else
			pFrom = "chat/all";

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor);
	};

	// Custom color for new line
	std::optional<ColorRGBA> CustomColor = std::nullopt;
	if(ClientId == CLIENT_MSG)
		CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));

	CLine &PreviousLine = m_aLines[m_CurrentLine];

	// Team Number:
	// 0 = global; 1 = team; 2 = sending whisper; 3 = receiving whisper

	// If it's a client message, m_aText will have ": " prepended so we have to work around it.
	if(PreviousLine.m_Initialized &&
		PreviousLine.m_TeamNumber == Team &&
		PreviousLine.m_ClientId == ClientId &&
		str_comp(PreviousLine.m_aText, pLine) == 0 &&
		PreviousLine.m_CustomColor == CustomColor)
	{
		PreviousLine.m_TimesRepeated++;
		TextRender()->DeleteTextContainer(PreviousLine.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(PreviousLine.m_QuadContainerIndex);
		PreviousLine.m_Time = time();
		PreviousLine.m_aYOffset[0] = -1.0f;
		PreviousLine.m_aYOffset[1] = -1.0f;

		FChatMsgCheckAndPrint(PreviousLine);
		return;
	}

	m_CurrentLine = (m_CurrentLine + 1) % MAX_LINES;

	CLine &CurrentLine = m_aLines[m_CurrentLine];
	CurrentLine.Reset(*this);
	CurrentLine.m_Initialized = true;
	CurrentLine.m_Time = time();
	CurrentLine.m_aYOffset[0] = -1.0f;
	CurrentLine.m_aYOffset[1] = -1.0f;
	CurrentLine.m_ClientId = ClientId;
	CurrentLine.m_TeamNumber = Team;
	CurrentLine.m_Team = Team == 1;
	CurrentLine.m_Whisper = Team >= 2;
	CurrentLine.m_NameColor = -2;
	CurrentLine.m_CustomColor = CustomColor;

	// check for highlighted name
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}
	CurrentLine.m_Highlighted = Highlighted;

	str_copy(CurrentLine.m_aText, pLine);

	if(CurrentLine.m_ClientId == SERVER_MSG)
	{
		str_copy(CurrentLine.m_aName, "*** ");
	}
	else if(CurrentLine.m_ClientId == CLIENT_MSG)
	{
		str_copy(CurrentLine.m_aName, "- ");
	}
	else
	{
		const auto &LineAuthor = GameClient()->m_aClients[CurrentLine.m_ClientId];

		if(LineAuthor.m_Active)
		{
			if(LineAuthor.m_Team == TEAM_SPECTATORS)
				CurrentLine.m_NameColor = TEAM_SPECTATORS;

			if(GameClient()->IsTeamPlay())
			{
				if(LineAuthor.m_Team == TEAM_RED)
					CurrentLine.m_NameColor = TEAM_RED;
				else if(LineAuthor.m_Team == TEAM_BLUE)
					CurrentLine.m_NameColor = TEAM_BLUE;
			}
		}

		if(Team == TEAM_WHISPER_SEND)
		{
			str_copy(CurrentLine.m_aName, "→");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_BLUE;
			CurrentLine.m_Highlighted = false;
			Highlighted = false;
		}
		else if(Team == TEAM_WHISPER_RECV)
		{
			str_copy(CurrentLine.m_aName, "←");
			if(LineAuthor.m_Active)
			{
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, LineAuthor.m_aName);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			str_copy(CurrentLine.m_aName, LineAuthor.m_aName);
		}

		if(LineAuthor.m_Active)
		{
			CurrentLine.m_Friend = LineAuthor.m_Friend;
			CurrentLine.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(LineAuthor);
		}
	}

	FChatMsgCheckAndPrint(CurrentLine);

	// play sound
	int64_t Now = time();
	if(ClientId == SERVER_MSG)
	{
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10)
		{
			if(g_Config.m_SndServerMessage)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
				m_aLastSoundPlayed[CHAT_SERVER] = Now;
			}
		}
	}
	else if(ClientId == CLIENT_MSG)
	{
		// No sound yet
	}
	else if(Highlighted && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(Now - m_aLastSoundPlayed[CHAT_HIGHLIGHT] >= time_freq() * 3 / 10)
		{
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, CurrentLine.m_aText);
			Client()->Notify("DDNet Chat", aBuf);
			if(g_Config.m_SndHighlight)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
				m_aLastSoundPlayed[CHAT_HIGHLIGHT] = Now;
			}

			if(g_Config.m_ClEditor)
			{
				GameClient()->Editor()->UpdateMentions();
			}
		}
	}
	else if(Team != TEAM_WHISPER_SEND)
	{
		if(Now - m_aLastSoundPlayed[CHAT_CLIENT] >= time_freq() * 3 / 10)
		{
			bool PlaySound = CurrentLine.m_Team ? g_Config.m_SndTeamChat : g_Config.m_SndChat;
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				PlaySound &= (bool)g_Config.m_ClVideoShowChat;
			}
#endif
			if(PlaySound)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_CLIENT, 1.0f);
				m_aLastSoundPlayed[CHAT_CLIENT] = Now;
			}
		}
	}

	// TClient
	GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

void CChat::OnPrepareLines(float x, float y)
{
	const float Height = HudLayout::CANVAS_HEIGHT;
	const float Width = Height * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const bool LayoutOverride = HudLayout::HasRuntimeOverride(HudLayout::MODULE_CHAT);
	const float LayoutScale = LayoutOverride ? std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) : 1.0f;
	float FontSize = this->FontSize() * LayoutScale;

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool LayoutChanged = LayoutOverride && (x != m_PrevHudLayoutX || y != m_PrevHudLayoutY || Layout.m_Scale != m_PrevHudLayoutScale || !m_PrevHudLayoutEnabled);
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || LayoutChanged;
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	m_PrevHudLayoutX = x;
	m_PrevHudLayoutY = y;
	m_PrevHudLayoutScale = Layout.m_Scale;
	m_PrevHudLayoutEnabled = HudLayout::IsEnabled(HudLayout::MODULE_CHAT);

	const int TeeSize = round_to_int(MessageTeeSize() * LayoutScale);
	float RealMsgPaddingX = MessagePaddingX() * LayoutScale;
	float RealMsgPaddingY = MessagePaddingY() * LayoutScale;
	float RealMsgPaddingTee = TeeSize + MESSAGE_TEE_PADDING_RIGHT * LayoutScale;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	int64_t Now = time();
	float LineWidth = (IsScoreBoardOpen ? std::max(85.0f, (FontSize * 85.0f / 6.0f)) : g_Config.m_ClChatWidth * LayoutScale) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	float HeightLimit = IsScoreBoardOpen ? 180.0f : (m_PrevShowChat ? 50.0f : 200.0f);
	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;

	for(int i = 0; i < MAX_LINES; i++)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat)
			break;

		if(Line.m_TextContainerIndex.Valid() && !ForceRecreate)
			continue;

		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);

		char aClientId[16] = "";
		if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
		}

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		const char *pText = Line.m_aText;
		if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
			{
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			}
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
			{
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			}
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
			{
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
			}
		}

		const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Line.m_CustomColor = ColoredParts.Colors()[0].m_Color;
		pText = ColoredParts.Text();

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			// If hidden and there is translated text
			if(pText != Line.m_aText)
			{
				pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
			}
			else if(Line.m_pTranslateResponse->m_Error)
			{
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			}
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}

		// get the y offset (calculate it if we haven't done that yet)
		if(Line.m_aYOffset[OffsetType] < 0.0f)
		{
			CTextCursor MeasureCursor;
			MeasureCursor.SetPosition(vec2(TextBegin, 0.0f));
			MeasureCursor.m_FontSize = FontSize;
			MeasureCursor.m_Flags = 0;
			MeasureCursor.m_LineWidth = LineWidth;

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				MeasureCursor.m_X += RealMsgPaddingTee;

				if(Line.m_Friend && g_Config.m_ClMessageFriend)
				{
					TextRender()->TextEx(&MeasureCursor, "♥ ");
				}
			}

			TextRender()->TextEx(&MeasureCursor, aClientId);
			TextRender()->TextEx(&MeasureCursor, Line.m_aName);
			if(Line.m_TimesRepeated > 0)
				TextRender()->TextEx(&MeasureCursor, aCount);

			if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
			{
				TextRender()->TextEx(&MeasureCursor, ": ");
			}

			CTextCursor AppendCursor = MeasureCursor;
			AppendCursor.m_LongestLineWidth = 0.0f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				AppendCursor.m_StartX = MeasureCursor.m_X;
				AppendCursor.m_LineWidth -= MeasureCursor.m_LongestLineWidth;
			}

			if(pTranslatedText)
			{
				TextRender()->TextEx(&AppendCursor, pTranslatedText);
				if(pTranslatedLanguage)
				{
					TextRender()->TextEx(&AppendCursor, " [");
					TextRender()->TextEx(&AppendCursor, pTranslatedLanguage);
					TextRender()->TextEx(&AppendCursor, "]");
				}
				TextRender()->TextEx(&AppendCursor, "\n");
				AppendCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&AppendCursor, pText);
				AppendCursor.m_FontSize /= 0.8f;
			}
			else if(pTranslatedError)
			{
				TextRender()->TextEx(&AppendCursor, pText);
				TextRender()->TextEx(&AppendCursor, "\n");
				AppendCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&AppendCursor, pTranslatedError);
				AppendCursor.m_FontSize /= 0.8f;
			}
			else
			{
				TextRender()->TextEx(&AppendCursor, pText);
			}

			Line.m_aYOffset[OffsetType] = AppendCursor.Height() + RealMsgPaddingY;
		}

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && !ChatInteractionActive())
			break;

		// the position the text was created
		Line.m_TextYOffset = y + RealMsgPaddingY / 2.0f;

		int CurRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(CurRenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);

		// reset the cursor
		CTextCursor LineCursor;
		LineCursor.SetPosition(vec2(TextBegin, Line.m_TextYOffset));
		LineCursor.m_FontSize = FontSize;
		LineCursor.m_LineWidth = LineWidth;

		// Message is from valid player
		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			LineCursor.m_X += RealMsgPaddingTee;

			if(Line.m_Friend && g_Config.m_ClMessageFriend)
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(1.0f));
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "♥ ");
			}
		}

		// render name
		ColorRGBA NameColor;
		if(Line.m_CustomColor)
			NameColor = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId)) // TClient
			NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
		else if(Line.m_Team)
			NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else if(Line.m_NameColor == TEAM_RED)
			NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
		else if(Line.m_NameColor == TEAM_BLUE)
			NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		else if(Line.m_NameColor == TEAM_SPECTATORS)
			NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, 1.0f);
		else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
			NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
		else
			NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);

		TextRender()->TextColor(NameColor);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_aName);

		if(Line.m_TimesRepeated > 0)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aCount);
		}

		if(Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, ": ");
		}

		ColorRGBA Color;
		if(Line.m_CustomColor)
			Color = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_Highlighted)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		else if(Line.m_Team)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else // regular message
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		TextRender()->TextColor(Color);

		CTextCursor AppendCursor = LineCursor;
		AppendCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			AppendCursor.m_StartX = LineCursor.m_X;
			AppendCursor.m_LineWidth -= LineCursor.m_LongestLineWidth;
		}

		if(pTranslatedText)
		{
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedText);
			if(pTranslatedLanguage)
			{
				ColorRGBA ColorLang = Color;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, " [");
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedLanguage);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "]");
			}
			ColorRGBA ColorSub = Color;
			ColorSub.r *= 0.7f;
			ColorSub.g *= 0.7f;
			ColorSub.b *= 0.7f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else if(pTranslatedError)
		{
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			ColorRGBA ColorSub = Color;
			ColorSub.r = 0.7f;
			ColorSub.g = 0.6f;
			ColorSub.b = 0.6f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, "\n");
			AppendCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pTranslatedError);
			AppendCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else
		{
			ColoredParts.AddSplitsToCursor(AppendCursor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &AppendCursor, pText);
			AppendCursor.m_vColorSplits.clear();
		}

		if(!g_Config.m_ClChatOld && (Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0'))
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				FullWidth += LineCursor.m_LongestLineWidth + AppendCursor.m_LongestLineWidth;
			}
			else
			{
				FullWidth += std::max(LineCursor.m_LongestLineWidth, AppendCursor.m_LongestLineWidth);
			}
			Graphics()->SetColor(1, 1, 1, 1);
			Line.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(Begin, y, FullWidth, Line.m_aYOffset[OffsetType], MessageRounding(), IGraphics::CORNER_ALL);
		}

		TextRender()->SetRenderFlags(CurRenderFlags);
		if(Line.m_TextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_TextContainerIndex);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

CUIRect CChat::GetHudRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, HudWidth, HudHeight);
	const bool Override = HudLayout::HasRuntimeOverride(HudLayout::MODULE_CHAT);
	if(!ForcePreview && Override && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const bool Scoreboard = GameClient()->m_Scoreboard.IsShown() && Graphics()->ScreenAspect() > 1.7f;
	const bool Large = ForcePreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const float Scale = Override ? std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) : 1.0f;
	const float VisibleHeight = (Scoreboard ? 120.0f : (Large ? 250.0f : 95.0f)) * Scale;
	const float Width = (float)g_Config.m_ClChatWidth * Scale;
	const float DefaultY = HudHeight - (20.0f * FontSize() / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f));
	CUIRect Rect = {Override ? Layout.m_X : 5.0f, (Override ? Layout.m_Y : DefaultY) - VisibleHeight, Width, VisibleHeight};
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, HudHeight - Rect.h));
	return Rect;
}

void CChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	// send pending chat messages
	if(m_PendingChatCounter > 0 && m_LastChatSend + time_freq() < time())
	{
		CHistoryEntry *pEntry = m_History.Last();
		for(int i = m_PendingChatCounter - 1; pEntry; --i, pEntry = m_History.Prev(pEntry))
		{
			if(i == 0)
			{
				SendChat(pEntry->m_Team, pEntry->m_aText);
				break;
			}
		}
		--m_PendingChatCounter;
	}

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	const bool WindowActive = Kernel()->RequestInterface<IEngineGraphics>()->WindowActive();
	if(m_SmoothHudCursorActive && m_Mode == MODE_NONE)
		ResetSmoothHudState(true);
	else if(!WindowActive)
	{
		// Losing window focus must not close a chat that is already being edited.
		// Release absolute mouse mode while inactive and re-enable it on focus.
		if(m_SmoothHudCursorActive)
		{
			m_SmoothHudCursorActive = false;
			Input()->MouseModeRelative();
		}
		m_SmoothHudCaretInitialized = false;
		SyncSmoothHudTypingAnimationBaseline();
	}
	else if(m_Mode != MODE_NONE && !m_SmoothHudCursorActive)
	{
		Input()->MouseModeAbsolute();
		Input()->SetSystemCursorVisible(false);
		m_SmoothHudCursorActive = true;
	}
	else if(m_SmoothHudCursorActive)
		// Reapply this after a focus transition as SDL/platform backends may
		// restore their native cursor while the window was inactive.
		Input()->SetSystemCursorVisible(false);

	const auto ChatLayout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const bool ChatLayoutOverride = HudLayout::HasRuntimeOverride(HudLayout::MODULE_CHAT);
	if(ChatLayoutOverride && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return;
	float x = ChatLayoutOverride ? ChatLayout.m_X : 5.0f;

	// TClient
	float y = ChatLayoutOverride ? ChatLayout.m_Y : 300.0f - (20.0f * FontSize() / 6.0f + (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f));
	// float y = 300.0f - 20.0f * FontSize() / 6.0f;

	const float ChatLayoutScale = ChatLayoutOverride ? std::clamp(ChatLayout.m_Scale / 100.0f, 0.25f, 3.0f) : 1.0f;
	float ScaledFontSize = FontSize() * (8.0f / 6.0f) * ChatLayoutScale;
	if(m_Mode != MODE_NONE)
	{
		// Chat input remains logically live from the first frame. Only its
		// presentation receives a short entrance so typed characters are never
		// buffered, delayed or animated one by one.
		const bool AnimateChatOpen = g_Config.m_AmfSmoothHud && WindowActive && g_Config.m_AmfAnimChatOpen;
		const bool AnimateChatText = AnimateChatOpen && g_Config.m_AmfAnimText;
		const float ChatOpenProgress = AnimateChatOpen && m_SmoothHudChatOpenedTime != 0 ?
			std::clamp((time_get() - m_SmoothHudChatOpenedTime) / (g_Config.m_AmfAnimDuration / 1000.0f * time_freq()), 0.0f, 1.0f) :
			1.0f;
		const float ChatOpenEase = SmoothUiEaseOutCubic(ChatOpenProgress);
		const float ChatOpenOffset = AnimateChatOpen && g_Config.m_AmfAnimSlide ?
			(1.0f - ChatOpenEase) * std::min((float)g_Config.m_AmfAnimSlideDistance, 4.0f) :
			0.0f;
		if(AnimateChatText)
		{
			TextRender()->TextColor(TextRender()->DefaultTextColor().WithMultipliedAlpha(ChatOpenEase));
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(ChatOpenEase));
		}

		// render chat input
		CTextCursor InputCursor;
		InputCursor.SetPosition(vec2(x, y + ChatOpenOffset));
		InputCursor.m_FontSize = ScaledFontSize;
		InputCursor.m_LineWidth = g_Config.m_ClChatWidth * ChatLayoutScale;

		// TClient
		InputCursor.m_LineWidth = std::max(InputCursor.m_LineWidth, 190.0f * ChatLayoutScale);

		if(m_Mode == MODE_ALL)
			TextRender()->TextEx(&InputCursor, Localize("All"));
		else if(m_Mode == MODE_TEAM)
			TextRender()->TextEx(&InputCursor, Localize("Team"));
		else
			TextRender()->TextEx(&InputCursor, Localize("Chat"));

		TextRender()->TextEx(&InputCursor, ": ");

		const float MessageMaxWidth = InputCursor.m_LineWidth - (InputCursor.m_X - InputCursor.m_StartX);
		const CUIRect ClippingRect = {InputCursor.m_X, InputCursor.m_Y, MessageMaxWidth, 2.25f * InputCursor.m_FontSize};
		const float XScale = Graphics()->ScreenWidth() / Width;
		const float YScale = Graphics()->ScreenHeight() / Height;
		Graphics()->ClipEnable((int)(ClippingRect.x * XScale), (int)(ClippingRect.y * YScale), (int)(ClippingRect.w * XScale), (int)(ClippingRect.h * YScale));

		float ScrollOffset = m_Input.GetScrollOffset();
		float ScrollOffsetChange = m_Input.GetScrollOffsetChange();

		m_Input.Activate(EInputPriority::CHAT); // Ensure that the input is active
		const CUIRect InputCursorRect = {InputCursor.m_X, InputCursor.m_Y - ScrollOffset, 0.0f, 0.0f};
		const bool WasChanged = m_Input.WasChanged();
		const bool WasCursorChanged = m_Input.WasCursorChanged();
		const bool Changed = WasChanged || WasCursorChanged;
		const bool SmoothCaret = g_Config.m_AmfSmoothHud && WindowActive && g_Config.m_AmfAnimSmoothCaret;
		const bool AnimateTypedCharacters = g_Config.m_AmfSmoothHud && WindowActive && g_Config.m_AmfAnimText;
		constexpr float TypingAnimDuration = 0.18f;
		char aDisplayedInputText[MAX_LINE_LENGTH];
		str_copy(aDisplayedInputText, m_Input.GetDisplayedString());
		m_vSmoothHudTypedCharacterColorSplits.clear();
		if(AnimateTypedCharacters && aDisplayedInputText[0] != '\0' && ChatTypingAnimSupportsText(aDisplayedInputText))
		{
			for(auto It = m_vSmoothHudTypingGlyphAnims.begin(); It != m_vSmoothHudTypingGlyphAnims.end();)
			{
				const float Age = (time_get() - It->m_StartTime) / (float)time_freq();
				const int StartByte = It->m_ByteIndex;
				const int GlyphBytes = It->m_ByteLength;
				const bool Valid =
					Age < TypingAnimDuration &&
					StartByte >= 0 &&
					GlyphBytes > 0 &&
					StartByte + GlyphBytes <= str_length(aDisplayedInputText) &&
					str_length(It->m_aText) == GlyphBytes &&
					str_comp_num(It->m_aText, aDisplayedInputText + StartByte, GlyphBytes) == 0;
				if(!Valid)
				{
					It = m_vSmoothHudTypingGlyphAnims.erase(It);
					continue;
				}
				m_vSmoothHudTypedCharacterColorSplits.emplace_back(
					StartByte, GlyphBytes, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
				++It;
			}
		}
		m_Input.SetHideCaret(SmoothCaret);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		const bool DisableBaseOutline = !m_vSmoothHudTypedCharacterColorSplits.empty();
		if(DisableBaseOutline)
			TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		const STextBoundingBox BoundingBox = m_Input.Render(&InputCursorRect, InputCursor.m_FontSize, TEXTALIGN_TL, Changed, MessageMaxWidth, 0.0f, m_vSmoothHudTypedCharacterColorSplits);
		if(DisableBaseOutline)
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

		// Match Best Client's typing animation exactly: the newly inserted
		// glyph is hidden in the base pass, then drawn once as a separate
		// ease-out overlay. Previously entered glyphs are never reanimated.
		for(const STypingGlyphAnim &TypingGlyphAnim : m_vSmoothHudTypingGlyphAnims)
		{
			const float Age = (time_get() - TypingGlyphAnim.m_StartTime) / (float)time_freq();
			const float Progress = std::clamp(Age / TypingAnimDuration, 0.0f, 1.0f);
			const float Ease = 1.0f - std::pow(1.0f - Progress, 3.0f);
			const float OverlayYOffset = -4.5f * (1.0f - Ease);
			const int PrefixBytes = TypingGlyphAnim.m_ByteIndex;
		char aPrefixText[MAX_LINE_LENGTH] = "";
			if(PrefixBytes < 0 || PrefixBytes > str_length(aDisplayedInputText))
				continue;
			str_truncate(aPrefixText, sizeof(aPrefixText), aDisplayedInputText, PrefixBytes);

			CTextCursor MeasureCursor;
			MeasureCursor.SetPosition(vec2(InputCursorRect.x, InputCursorRect.y));
			MeasureCursor.m_FontSize = InputCursor.m_FontSize;
			MeasureCursor.m_LineWidth = MessageMaxWidth;
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
			TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
			TextRender()->TextEx(&MeasureCursor, aPrefixText);

			CTextCursor OverlayCursor;
			OverlayCursor.SetPosition(vec2(MeasureCursor.m_X, MeasureCursor.m_Y + OverlayYOffset));
			OverlayCursor.m_FontSize = InputCursor.m_FontSize;
			OverlayCursor.m_LineWidth = MessageMaxWidth;
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f + 0.25f * Ease));
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(0.75f + 0.25f * Ease));
			TextRender()->TextEx(&OverlayCursor, TypingGlyphAnim.m_aText);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		if(SmoothCaret)
		{
			const vec2 TargetCaretPos = m_Input.GetCaretPosition();
			if(!m_SmoothHudCaretInitialized || distance(m_SmoothHudCaretPos, TargetCaretPos) > 48.0f)
			{
				m_SmoothHudCaretPos = TargetCaretPos;
				m_SmoothHudCaretInitialized = true;
			}
			else
			{
				const int CaretDurationMs = std::clamp(125 - g_Config.m_AmfAnimCaretSpeed, 20, 115);
				m_SmoothHudCaretPos.x = SmoothApproachDuration(m_SmoothHudCaretPos.x, TargetCaretPos.x, Client()->RenderFrameTime(), CaretDurationMs);
				m_SmoothHudCaretPos.y = SmoothApproachDuration(m_SmoothHudCaretPos.y, TargetCaretPos.y, Client()->RenderFrameTime(), CaretDurationMs);
			}

			// Match the original flat DDNet caret: one unrounded, unshaded
			// vertical line. Only its position is smoothed. Pixel alignment is
			// performed after interpolation so neither the line nor the text is
			// blurred by a persistent subpixel drift.
			const float CaretX = std::round(m_SmoothHudCaretPos.x * XScale) / XScale;
			const float CaretY = std::round(m_SmoothHudCaretPos.y * YScale) / YScale;
			const float CaretWidth = std::max((float)g_Config.m_AmfAnimCaretWidth / XScale, 1.0f / XScale);
			const float CaretHeight = std::max(std::round(InputCursor.m_FontSize * YScale) / YScale, 1.0f / YScale);
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem CaretQuad(CaretX, CaretY, CaretWidth, CaretHeight);
			Graphics()->QuadsDrawTL(&CaretQuad, 1);
			Graphics()->QuadsEnd();
		}
		else
			m_SmoothHudCaretInitialized = false;

		Graphics()->ClipDisable();

		// Scroll up or down to keep the caret inside the clipping rect
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < ClippingRect.y)
			ScrollOffsetChange -= ClippingRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > ClippingRect.y + ClippingRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (ClippingRect.y + ClippingRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, ClippingRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);

		// Autocompletion hint
		if(m_Input.GetString()[0] == '/' && m_Input.GetString()[1] != '\0' && !m_vServerCommands.empty())
		{
			for(const auto &Command : m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, m_Input.GetString() + 1))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, Command.m_aName + str_length(m_Input.GetString() + 1));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					break;
				}
			}
		}
		if(AnimateChatText)
		{
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		}
	}

#if defined(CONF_VIDEORECORDER)
	if(!((g_Config.m_ClShowChat && !IVideo::Current()) || (g_Config.m_ClVideoShowChat && IVideo::Current())))
#else
	if(!g_Config.m_ClShowChat)
#endif
		return;
	if(g_Config.m_AmfFocusMode && g_Config.m_AmfFocusModeHideChat)
		return;

	y -= ScaledFontSize;

	OnPrepareLines(x, y);

	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsShown() && (Graphics()->ScreenAspect() > 1.7f); // only assume scoreboard when screen ratio is widescreen(something around 16:9)

	int64_t Now = time();
	float HeightLimit = (IsScoreBoardOpen ? 180.0f : (m_PrevShowChat ? 50.0f : 200.0f)) * ChatLayoutScale;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	// The interactive chat cursor and history remain available when TAB is
	// visible. The scoreboard has its own input gate, so this does not turn the
	// chat into a blocker for the scoreboard state.
	const bool ChatInteraction = ChatInteractionActive();
	const bool AnimateChatMessages = g_Config.m_AmfSmoothHud && WindowActive && g_Config.m_AmfAnimChatMessages;
	const int PreviouslyHoveredLine = m_SmoothHudHoveredLine;
	const float MessageBottom = y;
	if(ChatInteraction)
	{
		float ContentHeight = 0.0f;
		for(int i = 0; i < MAX_LINES; ++i)
		{
			const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!Line.m_Initialized)
				break;
			ContentHeight += std::max(Line.m_aYOffset[OffsetType], 0.0f);
		}
		m_SmoothHudHistoryScrollMax = std::max(0.0f, ContentHeight - (MessageBottom - HeightLimit));
		m_SmoothHudHistoryScrollTarget = std::clamp(m_SmoothHudHistoryScrollTarget, 0.0f, m_SmoothHudHistoryScrollMax);
		if(g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimMenu)
			m_SmoothHudHistoryScroll = SmoothApproachDuration(m_SmoothHudHistoryScroll, m_SmoothHudHistoryScrollTarget, Client()->RenderFrameTime(), std::max(g_Config.m_AmfAnimDuration, 60));
		else
			m_SmoothHudHistoryScroll = m_SmoothHudHistoryScrollTarget;
		UpdateSmoothHudMousePosition(Width, Height);
		m_SmoothHudHoveredLine = -1;
	}

	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
	}

	bool PendingMessageClick = ChatInteraction && m_SmoothHudPendingMessageClick;
	m_SmoothHudPendingMessageClick = false;
	for(int i = 0; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat)
			break;

		y -= Line.m_aYOffset[OffsetType];
		const float RenderY = y + (ChatInteraction ? m_SmoothHudHistoryScroll : 0.0f);

		// cut off if msgs waste too much space
		if(!ChatInteraction && y < HeightLimit)
			break;
		if(ChatInteraction && (RenderY < HeightLimit || RenderY > MessageBottom))
			continue;

		float Blend = Now > Line.m_Time + 14 * time_freq() && !m_PrevShowChat ? 1.0f - (Now - Line.m_Time - 14 * time_freq()) / (2.0f * time_freq()) : 1.0f;
		// Incoming messages are animated independently of whether the text input
		// is open. Interactive history is always available while chat is active,
		// while a new line receives the visual entrance only with Smooth HUD.
		const bool AnimateMessage = AnimateChatMessages;
		const int MessageDurationMs = g_Config.m_AmfAnimChatMessageDuration == 0 ? g_Config.m_AmfAnimDuration : g_Config.m_AmfAnimChatMessageDuration;
		const float MessageProgress = AnimateMessage ? std::clamp((Now - Line.m_Time) / (MessageDurationMs / 1000.0f * time_freq()), 0.0f, 1.0f) : 1.0f;
		const float MessageEase = SmoothUiEaseOutCubic(MessageProgress);
		const float MessageEntryOffset = AnimateMessage && g_Config.m_AmfAnimSlide ?
			(1.0f - MessageEase) * 42.0f :
			0.0f;
		const float AnimatedRenderY = RenderY + MessageEntryOffset;

		// Draw backgrounds for messages in one batch
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1)
			{
				Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)).WithMultipliedAlpha(Blend));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, 0, ((AnimatedRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset));
			}
		}

		if(ChatInteraction)
		{
			const CUIRect HitRect = {x, RenderY, (float)g_Config.m_ClChatWidth, Line.m_aYOffset[OffsetType]};
			const bool Hovered = HitRect.Inside(m_SmoothHudMousePos);
			if(Hovered)
			{
				m_SmoothHudHoveredLine = LineIndex;
				if(PendingMessageClick)
				{
					m_SmoothHudSelectedLine = LineIndex;
					m_SmoothHudContextMenuOpen = true;
					m_SmoothHudContextAlpha = 0.0f;
					m_SmoothHudContextX = m_SmoothHudMousePos.x + 7.0f;
					m_SmoothHudContextY = m_SmoothHudMousePos.y + 4.0f;
					PendingMessageClick = false;
				}
			}
			const float HoverAlpha = LineIndex == PreviouslyHoveredLine ? m_SmoothHudHoverAlpha : 0.0f;
			const float SelectedAlpha = LineIndex == m_SmoothHudSelectedLine ? m_SmoothHudSelectedAlpha : 0.0f;
			const float HighlightAlpha = std::max(HoverAlpha * 0.11f, SelectedAlpha * 0.17f);
			if(HighlightAlpha > 0.001f)
				HitRect.Draw(ColorRGBA(0.35f, 0.53f, 0.9f, HighlightAlpha), IGraphics::CORNER_ALL, MessageRounding());
		}

		if(Line.m_TextContainerIndex.Valid())
		{
			if(!g_Config.m_ClChatOld && Line.m_pManagedTeeRenderInfo != nullptr)
			{
				CTeeRenderInfo &TeeRenderInfo = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
				const int TeeSize = MessageTeeSize();
				TeeRenderInfo.m_Size = TeeSize;

				float RowHeight = FontSize() + RealMsgPaddingY;
				float OffsetTeeY = TeeSize / 2.0f;
				float FullHeightMinusTee = RowHeight - TeeSize;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeRenderInfo, OffsetToMid);
				vec2 TeeRenderPos(x + (RealMsgPaddingX + TeeSize) / 2.0f, AnimatedRenderY + OffsetTeeY + FullHeightMinusTee / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, Blend);
			}

			const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(Blend);
			const ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Blend);
			TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, 0, (AnimatedRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset);
		}
	}

	if(ChatInteraction)
	{
		if(g_Config.m_AmfSmoothHud && g_Config.m_AmfAnimMenu)
		{
			m_SmoothHudHoverAlpha = SmoothApproachDuration(m_SmoothHudHoverAlpha, m_SmoothHudHoveredLine >= 0 ? 1.0f : 0.0f, Client()->RenderFrameTime(), 70);
			m_SmoothHudSelectedAlpha = SmoothApproachDuration(m_SmoothHudSelectedAlpha, m_SmoothHudContextMenuOpen ? 1.0f : 0.0f, Client()->RenderFrameTime(), std::max(g_Config.m_AmfAnimDuration, 60));
		}
		else
		{
			m_SmoothHudHoverAlpha = m_SmoothHudHoveredLine >= 0 ? 1.0f : 0.0f;
			m_SmoothHudSelectedAlpha = m_SmoothHudContextMenuOpen ? 1.0f : 0.0f;
		}
		if(PendingMessageClick)
		{
			m_SmoothHudContextMenuOpen = false;
			m_SmoothHudContextAlpha = 0.0f;
		}
		RenderSmoothHudContextMenu(Width, Height);

		if(m_SmoothHudNotificationTime != 0)
		{
			const float Age = (time_get() - m_SmoothHudNotificationTime) / (float)time_freq();
			if(Age >= 2.0f)
			{
				m_SmoothHudNotificationTime = 0;
				m_aSmoothHudNotification[0] = '\0';
			}
			else
			{
				const bool AnimateNotification = g_Config.m_AmfSmoothHud && WindowActive && g_Config.m_AmfAnimMenu;
				const float Alpha = AnimateNotification ?
					std::min(1.0f, Age / 0.12f) * std::min(1.0f, (2.0f - Age) / 0.25f) :
					1.0f;
				CUIRect Notification = {5.0f, HeightLimit - 20.0f, 80.0f, 14.0f};
				Notification.Draw(ColorRGBA(0.04f, 0.06f, 0.10f, 0.85f * Alpha), IGraphics::CORNER_ALL, 3.0f);
				SLabelProperties NotificationProps;
				NotificationProps.SetColor(ColorRGBA(0.9f, 0.95f, 1.0f, Alpha));
				Ui()->DoLabel(&Notification, m_aSmoothHudNotification, 9.0f, TEXTALIGN_MC, NotificationProps);
			}
		}

		RenderTools()->RenderCursor(m_SmoothHudMousePos, 14.0f);
	}
}

void CChat::EnsureCoherentFontSize() const
{
	// Adjust font size based on width
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatFontSize = g_Config.m_ClChatWidth / CHAT_FONTSIZE_WIDTH_RATIO;
}

void CChat::EnsureCoherentWidth() const
{
	// Adjust width based on font size
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatWidth = CHAT_FONTSIZE_WIDTH_RATIO * g_Config.m_ClChatFontSize;
}

// ----- send functions -----

void CChat::SendChat(int Team, const char *pLine)
{
	SendChat(Team, pLine, g_Config.m_ClDummy);
}

void CChat::SendChat(int Team, const char *pLine, int Connection)
{
	// don't send empty messages
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;
	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;

	m_LastChatSend = time();

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		Client()->SendPackMsg(Connection, &Msg7, MSGFLAG_VITAL, true);
		return;
	}

	// send chat message
	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pLine;
	Client()->SendPackMsg(Connection, &Msg, MSGFLAG_VITAL);
}

void CChat::SendChatQueued(const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	bool AddEntry = false;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(m_Mode == MODE_ALL ? 0 : 1, pLine);
		AddEntry = true;
	}
	else if(m_PendingChatCounter < 3)
	{
		++m_PendingChatCounter;
		AddEntry = true;
	}

	if(AddEntry)
	{
		const int Length = str_length(pLine);
		CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
		pEntry->m_Team = m_Mode == MODE_ALL ? 0 : 1;
		str_copy(pEntry->m_aText, pLine, Length + 1);
	}
}
