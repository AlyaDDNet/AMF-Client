#include <base/dbg.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/discord.h>

#include <game/version.h>

#if defined(CONF_DISCORD)
#include <discord_game_sdk.h>

constexpr const char *AMF_DISCORD_LARGE_IMAGE_KEY = "ascension_edition_logo_discord";

typedef enum EDiscordResult(DISCORD_API *FDiscordCreate)(DiscordVersion, struct DiscordCreateParams *, struct IDiscordCore **);

#if defined(CONF_DISCORD_DYNAMIC)
#include <dlfcn.h>
static FDiscordCreate GetDiscordCreate()
{
	void *pSdk = dlopen("discord_game_sdk.so", RTLD_NOW);
	if(!pSdk)
	{
		return nullptr;
	}
	return (FDiscordCreate)dlsym(pSdk, "DiscordCreate");
}
#else
static FDiscordCreate GetDiscordCreate()
{
	return DiscordCreate;
}
#endif

class CDiscord : public IDiscord
{
	static constexpr int RECONNECT_INTERVAL_SECONDS = 5;
	static constexpr int ACTIVITY_UPDATE_INTERVAL_SECONDS = 5;

	DiscordActivity m_Activity = {};
	bool m_HasActivity = false;
	bool m_UpdateActivity = false;
	int64_t m_LastActivityUpdate = 0;
	int64_t m_NextReconnectAttempt = 0;
	bool m_ActivityUpdatePending = false;
	int64_t m_ActivityUpdateDeadline = 0;
	bool m_ActivityUpdateFailed = false;
	EDiscordResult m_ActivityUpdateResult = DiscordResult_Ok;
	bool m_ConnectionFailureLogged = false;
	bool m_AwaitingReconnectConfirmation = false;
	bool m_ConnectionRestored = false;

	IDiscordCore *m_pCore = nullptr;
	IDiscordActivityEvents m_ActivityEvents = {};
	IDiscordActivityManager *m_pActivityManager = nullptr;

	FDiscordCreate m_pfnDiscordCreate = nullptr;
	bool m_Enabled = false;

	void DisconnectCore()
	{
		if(m_pCore)
			m_pCore->destroy(m_pCore);
		m_pCore = nullptr;
		m_pActivityManager = nullptr;
		m_ActivityUpdatePending = false;
		m_ActivityUpdateDeadline = 0;
		m_ActivityUpdateFailed = false;
		m_ActivityUpdateResult = DiscordResult_Ok;
		m_AwaitingReconnectConfirmation = false;
		m_ConnectionRestored = false;
	}

	void ScheduleReconnect(EDiscordResult Result)
	{
		DisconnectCore();
		m_UpdateActivity = true;
		m_NextReconnectAttempt = time_get() + time_freq() * RECONNECT_INTERVAL_SECONDS;
		if(!m_ConnectionFailureLogged)
		{
			dbg_msg("discord", "connection unavailable, error=%d; retrying every %d seconds", Result, RECONNECT_INTERVAL_SECONDS);
			m_ConnectionFailureLogged = true;
		}
	}

	static bool IsConnectionFailure(EDiscordResult Result)
	{
		return Result == DiscordResult_ServiceUnavailable ||
		       Result == DiscordResult_InternalError ||
		       Result == DiscordResult_NotInstalled ||
		       Result == DiscordResult_NotRunning ||
		       Result == DiscordResult_TransactionAborted;
	}

	void PublishActivity(bool Force)
	{
		if(!m_Enabled || !m_pCore || !m_pActivityManager || !m_HasActivity || m_ActivityUpdatePending)
			return;

		const int64_t Now = time_get();
		if(!Force && (!m_UpdateActivity || Now <= m_LastActivityUpdate + time_freq() * ACTIVITY_UPDATE_INTERVAL_SECONDS))
			return;

		m_UpdateActivity = false;
		m_LastActivityUpdate = Now;
		m_ActivityUpdatePending = true;
		m_ActivityUpdateDeadline = Now + time_freq() * ACTIVITY_UPDATE_INTERVAL_SECONDS * 2;
		m_pActivityManager->update_activity(m_pActivityManager, &m_Activity, this, &CDiscord::OnActivityUpdated);
	}

	static void DISCORD_CALLBACK OnActivityUpdated(void *pEventData, EDiscordResult Result)
	{
		CDiscord *pSelf = static_cast<CDiscord *>(pEventData);
		pSelf->m_ActivityUpdatePending = false;
		pSelf->m_ActivityUpdateDeadline = 0;
		if(Result == DiscordResult_Ok)
		{
			if(pSelf->m_AwaitingReconnectConfirmation)
			{
				pSelf->m_AwaitingReconnectConfirmation = false;
				pSelf->m_ConnectionRestored = true;
			}
			return;
		}
		pSelf->m_ActivityUpdateFailed = true;
		pSelf->m_ActivityUpdateResult = Result;
	}

public:
	bool Init(FDiscordCreate pfnDiscordCreate)
	{
		m_pfnDiscordCreate = pfnDiscordCreate;
		ClearGameInfo();
		return false;
	}
	bool InitDiscord()
	{
		DisconnectCore();
		if(!m_Enabled)
		{
			m_NextReconnectAttempt = 0;
			return false;
		}

		mem_zero(&m_ActivityEvents, sizeof(m_ActivityEvents));

		m_ActivityEvents.on_activity_join = &CDiscord::OnActivityJoin;

		DiscordCreateParams Params;
		DiscordCreateParamsSetDefault(&Params);

		Params.client_id = 1531029005732548770;
		Params.flags = EDiscordCreateFlags::DiscordCreateFlags_NoRequireDiscord;
		Params.event_data = this;
		Params.activity_events = &m_ActivityEvents;

		const EDiscordResult Error = m_pfnDiscordCreate(DISCORD_VERSION, &Params, &m_pCore);

		if(Error != DiscordResult_Ok || !m_pCore)
		{
			ScheduleReconnect(Error != DiscordResult_Ok ? Error : DiscordResult_InternalError);
			return true;
		}

		m_pActivityManager = m_pCore->get_activity_manager(m_pCore);
		if(!m_pActivityManager)
		{
			ScheduleReconnect(DiscordResult_InternalError);
			return true;
		}

		// which application to launch when joining activity
		m_pActivityManager->register_command(m_pActivityManager, CONNECTLINK_DOUBLE_SLASH);
		m_pActivityManager->register_steam(m_pActivityManager, 412220); // steam id

		m_AwaitingReconnectConfirmation = m_ConnectionFailureLogged;
		m_NextReconnectAttempt = 0;
		m_UpdateActivity = true;
		PublishActivity(true);

		return false;
	}

	void Update(bool Enabled) override
	{
		bool NeedsUpdate = m_Enabled != Enabled;
		m_Enabled = Enabled;

		if(NeedsUpdate)
		{
			if(!m_Enabled)
			{
				DisconnectCore();
				m_NextReconnectAttempt = 0;
				m_ConnectionFailureLogged = false;
				return;
			}
			m_NextReconnectAttempt = 0;
			InitDiscord();
		}

		if(!m_Enabled)
			return;

		if(!m_pCore)
		{
			if(time_get() >= m_NextReconnectAttempt)
				InitDiscord();
			return;
		}

		const EDiscordResult CallbackResult = m_pCore->run_callbacks(m_pCore);
		if(CallbackResult != DiscordResult_Ok)
		{
			ScheduleReconnect(CallbackResult);
			return;
		}

		if(m_ConnectionRestored)
		{
			m_ConnectionRestored = false;
			m_ConnectionFailureLogged = false;
			dbg_msg("discord", "connection restored and current activity published");
		}

		if(m_ActivityUpdateFailed)
		{
			const EDiscordResult ActivityResult = m_ActivityUpdateResult;
			m_ActivityUpdateFailed = false;
			m_ActivityUpdateResult = DiscordResult_Ok;
			if(IsConnectionFailure(ActivityResult))
			{
				ScheduleReconnect(ActivityResult);
				return;
			}
			if(ActivityResult == DiscordResult_RateLimited)
				m_UpdateActivity = true;
			else
				dbg_msg("discord", "failed to update activity, error=%d", ActivityResult);
		}

		if(m_ActivityUpdatePending && time_get() >= m_ActivityUpdateDeadline)
		{
			ScheduleReconnect(DiscordResult_TransactionAborted);
			return;
		}

		// Discord allows five activity updates per twenty seconds. Keep ordinary
		// state changes throttled, while InitDiscord publishes once immediately.
		PublishActivity(false);
	}

	void ClearGameInfo() override
	{
		SetIdleStatus("AMF Client", "In Main Menu");
	}

	void SetIdleStatus(const char *pDetails, const char *pState) override
	{
		const char *pSafeDetails = pDetails ? pDetails : "";
		const char *pSafeState = pState ? pState : "";
		if(!m_Activity.instance && str_comp(m_Activity.details, pSafeDetails) == 0 && str_comp(m_Activity.state, pSafeState) == 0)
			return;

		mem_zero(&m_Activity, sizeof(DiscordActivity));

		str_copy(m_Activity.assets.large_image, AMF_DISCORD_LARGE_IMAGE_KEY, sizeof(m_Activity.assets.large_image));
		str_copy(m_Activity.assets.large_text, CLIENT_NAME " " CLIENT_RELEASE_VERSION_DISPLAY, sizeof(m_Activity.assets.large_text));
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.name, "AMF Client", sizeof(m_Activity.name));
		str_copy(m_Activity.details, pSafeDetails, sizeof(m_Activity.details));
		str_copy(m_Activity.state, pSafeState, sizeof(m_Activity.state));
		m_Activity.instance = false;

		m_HasActivity = true;
		m_UpdateActivity = true;
	}

	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override
	{
		mem_zero(&m_Activity, sizeof(DiscordActivity));

		str_copy(m_Activity.assets.large_image, AMF_DISCORD_LARGE_IMAGE_KEY, sizeof(m_Activity.assets.large_image));
		str_copy(m_Activity.assets.large_text, CLIENT_NAME " " CLIENT_RELEASE_VERSION_DISPLAY, sizeof(m_Activity.assets.large_text));
		m_Activity.timestamps.start = time_timestamp();
		str_copy(m_Activity.name, "AMF Client", sizeof(m_Activity.name));
		m_Activity.instance = true;

		str_copy(m_Activity.details, "Playing DDNet", sizeof(m_Activity.details));
		str_format(m_Activity.state, sizeof(m_Activity.state), "Map: %s", ServerInfo.m_aMap);
		m_Activity.party.size.current_size = ServerInfo.m_NumClients;
		m_Activity.party.size.max_size = ServerInfo.m_MaxClients;
		// private makes it so the game isn't public to join, but there's 'Ask to Join' button instead
		m_Activity.party.privacy = Registered ? DiscordActivityPartyPrivacy_Public : DiscordActivityPartyPrivacy_Private;

		if(!Registered)
		{
			// private parties have random id to not leak the server ip
			char aPartyId[sizeof(m_Activity.party.id)];
			secure_random_password(aPartyId, sizeof(aPartyId), 64);
			str_copy(m_Activity.party.id, aPartyId);
		}
		UpdateServerIp(ServerInfo);

		m_HasActivity = true;
		m_UpdateActivity = true;
	}

	void UpdateServerInfo(const CServerInfo &ServerInfo) override
	{
		if(!m_Activity.instance)
			return;

		UpdateServerIp(ServerInfo);

		str_copy(m_Activity.details, "Playing DDNet", sizeof(m_Activity.details));
		str_format(m_Activity.state, sizeof(m_Activity.state), "Map: %s", ServerInfo.m_aMap);
		m_Activity.party.size.max_size = ServerInfo.m_MaxClients;
		m_UpdateActivity = true;
	}

	void UpdatePlayerCount(int Count) override
	{
		if(!m_Activity.instance)
			return;

		if(m_Activity.party.size.current_size == Count)
			return;

		m_Activity.party.size.current_size = Count;
		m_UpdateActivity = true;
	}

	void UpdateServerIp(const CServerInfo &ServerInfo)
	{
		if(!m_Activity.instance)
			return;

		// secret is only shared when player is joining the game, or when they are invited for private games
		if(str_length(ServerInfo.m_aAddress) < (int)sizeof(m_Activity.secrets.join))
		{
			str_copy(m_Activity.secrets.join, ServerInfo.m_aAddress);
		}
		else
		{
			char aAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&ServerInfo.m_aAddresses[0], aAddr, sizeof(aAddr), true);
			str_copy(m_Activity.secrets.join, aAddr);
		}

		if(m_Activity.party.privacy == DiscordActivityPartyPrivacy_Public)
		{
			// id is sha256, because it didn't work with the ':' character
			char aPartyId[SHA256_MAXSTRSIZE];
			SHA256_DIGEST PartyIdSha256 = sha256(m_Activity.secrets.join, str_length(m_Activity.secrets.join));
			sha256_str(PartyIdSha256, aPartyId, sizeof(aPartyId));
			str_copy(m_Activity.party.id, aPartyId);
		}
	}

	static void DISCORD_CALLBACK OnActivityJoin(void *pEventData, const char *pSecret)
	{
		CDiscord *pSelf = static_cast<CDiscord *>(pEventData);
		IClient *pClient = pSelf->Kernel()->RequestInterface<IClient>();
		pClient->Connect(pSecret);
	}

	~CDiscord()
	{
		DisconnectCore();
	}
};

static IDiscord *CreateDiscordImpl()
{
	FDiscordCreate pfnDiscordCreate = GetDiscordCreate();
	if(!pfnDiscordCreate)
	{
		return nullptr;
	}
	CDiscord *pDiscord = new CDiscord();
	if(pDiscord->Init(pfnDiscordCreate))
	{
		delete pDiscord;
		return nullptr;
	}
	return pDiscord;
}
#else
static IDiscord *CreateDiscordImpl()
{
	return nullptr;
}
#endif

class CDiscordStub : public IDiscord
{
	void Update(bool Enabled) override {}
	void SetIdleStatus(const char *pDetails, const char *pState) override {}
	void ClearGameInfo() override {}
	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override {}
	void UpdateServerInfo(const CServerInfo &ServerInfo) override {}
	void UpdatePlayerCount(int Count) override {}
};

IDiscord *CreateDiscord()
{
	IDiscord *pDiscord = CreateDiscordImpl();
	if(pDiscord)
	{
		return pDiscord;
	}
	return new CDiscordStub();
}
