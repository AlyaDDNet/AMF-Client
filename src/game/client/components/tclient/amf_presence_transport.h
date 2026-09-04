#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_TRANSPORT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_PRESENCE_TRANSPORT_H

#include <game/client/component.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CAmfPresenceTransport final : public CComponent
{
public:
	enum class ERemoteEventType
	{
		CONNECTED,
		DISCONNECTED,
		SNAPSHOT,
		DELTA,
		OFFLINE,
		BROWSER_SNAPSHOT,
	};

	struct SRemoteEvent
	{
		ERemoteEventType m_Type = ERemoteEventType::DISCONNECTED;
		std::string m_ClientId;
		std::string m_PresenceId;
		// A presence snapshot is authoritative only when every member record was
		// parsed and validated. Incomplete responses must not evict confirmed
		// identities merely because a record was temporarily malformed.
		bool m_SnapshotComplete = true;

		struct SConnection
		{
			bool m_Connected = false;
			bool m_SettingsOpen = false;
			bool m_FastPractice = false;
			bool m_CustomGradient = false;
			unsigned m_GradientColor1 = 0;
			unsigned m_GradientColor2 = 0;
			int m_GradientPosition = 0;
			int m_GradientStrength = 100;
			int m_GameClientId = -1;
			std::string m_PlayerName;
		};

		struct SPresence
		{
			std::string m_ClientId;
			std::string m_PresenceId;
			SConnection m_Main;
			SConnection m_Dummy;
		};

		struct SBrowserServer
		{
			std::string m_ServerKey;
			int m_ActiveCount = 0;
			std::vector<std::string> m_vPlayerNames;
		};

		std::vector<SPresence> m_vPresence;
		std::vector<SBrowserServer> m_vBrowserServers;
		int m_TotalActiveUsers = 0;
	};

	CAmfPresenceTransport() = default;
	~CAmfPresenceTransport() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;

	bool IsConnected() const { return m_Connected.load(std::memory_order_relaxed); }
	bool PollRemoteEvent(SRemoteEvent &Event);
	void RequestBrowserSnapshot();

private:
	struct SDesiredState
	{
		bool m_Enabled = false;
		bool m_BrowserSnapshotEnabled = false;
		std::string m_RoomId;
		std::string m_StateJson;
		std::string m_ClientId;
		std::string m_Token;
		uint64_t m_Revision = 0;
		uint64_t m_WakeupSerial = 0;
		uint64_t m_BrowserSnapshotRequestSerial = 0;
	};

	struct SLocalState
	{
		bool m_Online = false;
		bool m_Afk = false;
		bool m_EscOpen = false;
		bool m_MainConnected = false;
		bool m_DummyConnected = false;
		bool m_MainSettingsOpen = false;
		bool m_DummySettingsOpen = false;
		bool m_MainFastPractice = false;
		bool m_DummyFastPractice = false;
		bool m_MainCustomGradient = false;
		bool m_DummyCustomGradient = false;
		unsigned m_MainGradientColor1 = 0;
		unsigned m_MainGradientColor2 = 0;
		unsigned m_DummyGradientColor1 = 0;
		unsigned m_DummyGradientColor2 = 0;
		int m_MainGradientPosition = 0;
		int m_DummyGradientPosition = 0;
		int m_MainGradientStrength = 100;
		int m_DummyGradientStrength = 100;
		bool m_ActiveDummy = false;
		bool m_Spectating = false;
		int m_MainClientId = -1;
		int m_DummyClientId = -1;
		std::string m_MainPlayerName;
		std::string m_DummyPlayerName;
		std::string m_ServerAddress;

		bool operator==(const SLocalState &Other) const;
		bool operator!=(const SLocalState &Other) const { return !(*this == Other); }
	};

	void StartWorker();
	void StopWorker();
	void WorkerLoop();
	void SubmitDesiredState(bool Enabled, bool BrowserSnapshotEnabled, const std::string &RoomId, const std::string &StateJson);
	void QueueCredentialUpdate(const std::string &ClientId, const std::string &Token);
	void QueueRemoteEvent(SRemoteEvent Event);
	void ClearRemoteEvents();
	std::string BuildRoomId();
	std::string BuildStateJson(const SLocalState &State) const;

	std::mutex m_Mutex;
	std::condition_variable m_Condition;
	std::thread m_Worker;
	bool m_WorkerStarted = false;
	bool m_Stop = false;
	// Read while the worker is inside a cancellable libcurl request. Keeping
	// this separate from the mutex-protected worker state lets application
	// shutdown cancel the request before joining.
	std::atomic<bool> m_StopRequested{false};
	SDesiredState m_Desired;
	bool m_HasCredentialUpdate = false;
	std::string m_CredentialClientId;
	std::string m_CredentialToken;
	std::deque<SRemoteEvent> m_RemoteEvents;
	std::atomic<bool> m_Connected{false};
	std::atomic<bool> m_Debug{false};
	std::string m_InstanceId;

	bool m_HaveLastLocalState = false;
	SLocalState m_LastLocalState;
	std::string m_LastRoomSource;
	std::string m_LastRoomId;
	std::string m_LastStateJson;
};

#endif
