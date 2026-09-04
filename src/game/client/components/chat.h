/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>

#include <vector>

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

constexpr auto SAVES_FILE = "ddnet-saves.txt";

class CChat : public CComponent
{
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	enum
	{
		MAX_LINES = 64,
		MAX_LINE_LENGTH = 256,
	};

	CLineInputBuffered<MAX_LINE_LENGTH> m_Input;
	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[64];
		char m_aText[MAX_LINE_LENGTH];
		bool m_Friend;
		bool m_Highlighted;
		std::optional<ColorRGBA> m_CustomColor;

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;

		int m_TimesRepeated;

		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;
	float m_PrevHudLayoutX = -10000.0f;
	float m_PrevHudLayoutY = -10000.0f;
	int m_PrevHudLayoutScale = -1;
	bool m_PrevHudLayoutEnabled = true;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[MAX_LINE_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	static char ms_aDisplayText[MAX_LINE_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[MAX_LINE_LENGTH];
	bool m_EditingNewLine;

	bool m_ServerSupportsCommandInfo;

	// Interactive chat state. These values are transient and intentionally do
	// not participate in config saving. Cursor and context actions are always
	// available while chat is open; Smooth HUD controls only their animation.
	bool m_SmoothHudCursorActive = false;
	bool m_SmoothHudPendingMessageClick = false;
	bool m_SmoothHudContextMenuOpen = false;
	int m_SmoothHudHoveredLine = -1;
	int m_SmoothHudSelectedLine = -1;
	float m_SmoothHudHoverAlpha = 0.0f;
	float m_SmoothHudSelectedAlpha = 0.0f;
	float m_SmoothHudContextAlpha = 0.0f;
	float m_SmoothHudHistoryScroll = 0.0f;
	float m_SmoothHudHistoryScrollTarget = 0.0f;
	float m_SmoothHudHistoryScrollMax = 0.0f;
	float m_SmoothHudContextX = 0.0f;
	float m_SmoothHudContextY = 0.0f;
	float m_SmoothHudContextW = 0.0f;
	float m_SmoothHudContextH = 0.0f;
	vec2 m_SmoothHudMousePos{};
	vec2 m_SmoothHudCaretPos{};
	bool m_SmoothHudCaretInitialized = false;
	int64_t m_SmoothHudChatOpenedTime = 0;
	struct STypingGlyphAnim
	{
		int64_t m_StartTime = 0;
		int m_ByteIndex = 0;
		int m_ByteLength = 0;
		char m_aText[16] = "";
	};
	std::vector<STypingGlyphAnim> m_vSmoothHudTypingGlyphAnims;
	std::vector<STextColorSplit> m_vSmoothHudTypedCharacterColorSplits;
	char m_aSmoothHudPreviousInputText[MAX_LINE_LENGTH] = "";
	int64_t m_SmoothHudNotificationTime = 0;
	char m_aSmoothHudNotification[64] = "";

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);
	bool ChatInteractionActive() const;
	void ResetSmoothHudState(bool RestoreMouseMode);
	void ResetSmoothHudTypingAnimation();
	void SyncSmoothHudTypingAnimationBaseline();
	void RefreshSmoothHudTypingAnimation();
	void UpdateSmoothHudMousePosition(float Width, float Height);
	bool SmoothHudContextContainsMouse() const;
	bool SmoothHudLineHasPlayerAuthor(const CLine &Line) const;
	bool SmoothHudContextActionEnabled(int Action) const;
	void ExecuteSmoothHudContextAction(int Action);
	void RenderSmoothHudContextMenu(float Width, float Height);

	friend class CBindChat;
	friend class CTranslate;
	friend class CTClient;

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientId, int Team, const char *pLine);
	void EnableMode(int Team);
	void DisableMode();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float x, float y);
	CUIRect GetHudRect(float HudWidth, float HudHeight, bool ForcePreview = false) const;
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnInit() override;

	void RebuildChat();
	void ClearLines();

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	float FontSize() const { return g_Config.m_ClChatFontSize / 10.0f; }
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * (1 / 2.f); }

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);

	// Sends a chat message through a specific local connection. This uses the
	// same normal chat path as SendChat above, which is needed for team actions
	// initiated by the dummy while the main player is controlled.
	void SendChat(int Team, const char *pLine, int Connection);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	void SendChatQueued(const char *pLine);
};
#endif
