#include "amf_presence_transport.h"

#include <base/detect.h>
#include <base/fs.h>
#include <base/hash.h>
#include <base/log.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/uuid_manager.h>
#include <engine/storage.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/version.h>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <random>
#include <utility>

namespace
{
using namespace std::chrono_literals;

constexpr const char *AMF_PRESENCE_REGISTER_URL = "https://amf-client-presence.amfclient.workers.dev/v1/clients/register";
constexpr const char *AMF_PRESENCE_WEBSOCKET_ORIGIN = "https://amf-client-presence.amfclient.workers.dev/";
constexpr const char *AMF_PRESENCE_WEBSOCKET_HOST = "amf-client-presence.amfclient.workers.dev";
constexpr const char *AMF_PRESENCE_WEBSOCKET_PATH = "/v1/presence/connect";
constexpr const char *AMF_PRESENCE_PROTOCOL = "amf-presence-v1";
constexpr const char *AMF_PRESENCE_BROWSER_URL = "https://amf-client-presence.amfclient.workers.dev/v1/presence/browser";
constexpr size_t MAX_REGISTRATION_RESPONSE_SIZE = 16 * 1024;
constexpr size_t MAX_BROWSER_SNAPSHOT_RESPONSE_SIZE = 256 * 1024;
constexpr size_t MAX_SERVER_MESSAGE_SIZE = 256 * 1024;
constexpr size_t MAX_REMOTE_EVENTS = 256;
constexpr size_t MAX_HANDSHAKE_RESPONSE_SIZE = 16 * 1024;
constexpr size_t MAX_WIRE_BUFFER_SIZE = MAX_SERVER_MESSAGE_SIZE + 64 * 1024;
constexpr auto SEND_TIMEOUT = 2s;
constexpr auto WORKER_POLL_INTERVAL = 100ms;
constexpr int CONNECT_TIMEOUT_MS = 10000;
constexpr int BROWSER_SNAPSHOT_TIMEOUT_MS = 5000;
constexpr int64_t BROWSER_SNAPSHOT_INTERVAL_MS = 60000;
constexpr int64_t BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS = 1000;
// Refresh the existing presence state. This is the sole long-lived WebSocket
// heartbeat; it also lets a freshly deployed Worker enroll hibernating sockets
// that were established by an older Worker version.
constexpr int64_t PRESENCE_REFRESH_INTERVAL_MS = 90000;

constexpr int PRESENCE_SHUTDOWN_POLL_INTERVAL_MS = 50;

CURLcode PerformPresenceRequest(CURL *pCurl, const std::atomic<bool> &StopRequested)
{
	CURLM *pMulti = curl_multi_init();
	if(!pMulti)
		return CURLE_FAILED_INIT;

	if(curl_multi_add_handle(pMulti, pCurl) != CURLM_OK)
	{
		curl_multi_cleanup(pMulti);
		return CURLE_FAILED_INIT;
	}

	CURLcode Result = CURLE_OK;
	bool Done = false;
	while(!Done)
	{
		int Running = 0;
		if(curl_multi_perform(pMulti, &Running) != CURLM_OK)
		{
			Result = CURLE_FAILED_INIT;
			break;
		}

		CURLMsg *pMessage = nullptr;
		int MessagesLeft = 0;
		while((pMessage = curl_multi_info_read(pMulti, &MessagesLeft)))
		{
			if(pMessage->msg == CURLMSG_DONE && pMessage->easy_handle == pCurl)
			{
				Result = pMessage->data.result;
				Done = true;
				break;
			}
		}
		if(Done)
			break;

		if(StopRequested.load(std::memory_order_relaxed))
		{
			Result = CURLE_ABORTED_BY_CALLBACK;
			break;
		}
		if(Running == 0)
		{
			Result = CURLE_GOT_NOTHING;
			break;
		}

		int NumEvents = 0;
		if(curl_multi_poll(pMulti, nullptr, 0, PRESENCE_SHUTDOWN_POLL_INTERVAL_MS, &NumEvents) != CURLM_OK)
		{
			Result = CURLE_FAILED_INIT;
			break;
		}
	}

	curl_multi_remove_handle(pMulti, pCurl);
	curl_multi_cleanup(pMulti);
	return Result;
}

int AbortPresenceConnect(void *pUserData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(pUserData)->load(std::memory_order_relaxed) ? 1 : 0;
}

CURLcode PerformPresenceConnect(CURL *pCurl, const std::atomic<bool> &StopRequested)
{
	// CURLOPT_CONNECT_ONLY hands the socket to curl_easy_send/recv. libcurl
	// requires that lifecycle to begin with curl_easy_perform, not a completed
	// multi transfer. The progress callback keeps this connect-only transfer
	// cancellable while preserving the handle's raw-socket state.
	curl_easy_setopt(pCurl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(pCurl, CURLOPT_XFERINFOFUNCTION, AbortPresenceConnect);
	curl_easy_setopt(pCurl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(&StopRequested));
	return curl_easy_perform(pCurl);
}
constexpr int64_t STABLE_CONNECTION_TIME_MS = 30000;
constexpr int BACKOFF_BASE_MS = 1000;
constexpr int BACKOFF_MAX_MS = 60000;

int64_t SteadyNowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t CurlWriteToString(char *pData, size_t Size, size_t Count, void *pUser)
{
	const size_t DataSize = Size * Count;
	auto *pOutput = static_cast<std::string *>(pUser);
	if(DataSize > MAX_REGISTRATION_RESPONSE_SIZE || pOutput->size() > MAX_REGISTRATION_RESPONSE_SIZE - DataSize)
		return 0;
	pOutput->append(pData, DataSize);
	return DataSize;
}

size_t CurlWriteBrowserSnapshot(char *pData, size_t Size, size_t Count, void *pUser)
{
	const size_t DataSize = Size * Count;
	auto *pOutput = static_cast<std::string *>(pUser);
	if(DataSize > MAX_BROWSER_SNAPSHOT_RESPONSE_SIZE || pOutput->size() > MAX_BROWSER_SNAPSHOT_RESPONSE_SIZE - DataSize)
		return 0;
	pOutput->append(pData, DataSize);
	return DataSize;
}

bool IsValidClientId(const std::string &ClientId)
{
	if(ClientId.size() != 36)
		return false;
	for(size_t Index = 0; Index < ClientId.size(); ++Index)
	{
		const bool HyphenPosition = Index == 8 || Index == 13 || Index == 18 || Index == 23;
		if(HyphenPosition ? ClientId[Index] != '-' : !((ClientId[Index] >= '0' && ClientId[Index] <= '9') || (ClientId[Index] >= 'a' && ClientId[Index] <= 'f')))
			return false;
	}
	return true;
}

bool IsValidPresenceId(const std::string &PresenceId, const std::string &ClientId)
{
	if(PresenceId == ClientId)
		return true; // Protocol v1 compatibility.
	if(PresenceId.size() != ClientId.size() + 1 + 36 || PresenceId.compare(0, ClientId.size(), ClientId) != 0 || PresenceId[ClientId.size()] != ':')
		return false;
	return IsValidClientId(PresenceId.substr(ClientId.size() + 1));
}

bool IsValidIdentity(const std::string &ClientId, const std::string &Token)
{
	if(!IsValidClientId(ClientId) || Token.size() < 32 || Token.size() > 128)
		return false;
	return std::all_of(Token.begin(), Token.end(), [](unsigned char Character) {
		return (Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-';
	});
}

const char *InstallationIdSuffix(const std::string &ClientId)
{
	static constexpr size_t SUFFIX_LENGTH = 8;
	return ClientId.size() > SUFFIX_LENGTH ? ClientId.c_str() + ClientId.size() - SUFFIX_LENGTH : ClientId.c_str();
}

const char *InstallationIdSuffix(const char *pClientId)
{
	static constexpr size_t SUFFIX_LENGTH = 8;
	const size_t Length = str_length(pClientId);
	return Length > SUFFIX_LENGTH ? pClientId + Length - SUFFIX_LENGTH : pClientId;
}

std::string StorageIdentity(IStorage *pStorage)
{
	char aSavePath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aSavePath, sizeof(aSavePath));
	if(fs_is_relative_path(aSavePath))
	{
		char aWorkingDirectory[IO_MAX_PATH_LENGTH];
		if(fs_getcwd(aWorkingDirectory, sizeof(aWorkingDirectory)))
		{
			char aAbsolutePath[IO_MAX_PATH_LENGTH];
			str_format(aAbsolutePath, sizeof(aAbsolutePath), "%s/%s", aWorkingDirectory, aSavePath);
			str_copy(aSavePath, aAbsolutePath);
		}
	}
	fs_normalize_path(aSavePath);
#if defined(CONF_FAMILY_WINDOWS)
	for(char *pCharacter = aSavePath; *pCharacter != '\0'; ++pCharacter)
	{
		if(*pCharacter >= 'A' && *pCharacter <= 'Z')
			*pCharacter = *pCharacter - 'A' + 'a';
	}
#endif
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(aSavePath, str_length(aSavePath)), aHash, sizeof(aHash));
	return aHash;
}

const char *SafePlayerName(const std::string &PlayerName, char (&aBuffer)[MAX_NAME_LENGTH])
{
	str_copy(aBuffer, PlayerName.c_str());
	str_sanitize_cc(aBuffer);
	return aBuffer;
}

bool RegisterInstallation(std::string &ClientId, std::string &Token, const std::atomic<bool> &StopRequested)
{
	CURL *pCurl = curl_easy_init();
	if(!pCurl)
		return false;

	const std::string Body = std::string("{\"clientVersion\":\"") + GAME_RELEASE_VERSION + "\",\"platform\":\"" + CONF_PLATFORM_STRING + "\"}";
	std::string Response;
	char aError[CURL_ERROR_SIZE] = {0};
	curl_slist *pHeaders = nullptr;
	pHeaders = curl_slist_append(pHeaders, "Content-Type: application/json");

	curl_easy_setopt(pCurl, CURLOPT_URL, AMF_PRESENCE_REGISTER_URL);
	curl_easy_setopt(pCurl, CURLOPT_PROTOCOLS_STR, "HTTPS");
	curl_easy_setopt(pCurl, CURLOPT_POST, 1L);
	curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, Body.data());
	curl_easy_setopt(pCurl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(Body.size()));
	curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
	curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(CONNECT_TIMEOUT_MS));
	curl_easy_setopt(pCurl, CURLOPT_TIMEOUT_MS, static_cast<long>(CONNECT_TIMEOUT_MS));
	curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "AMF Client Presence/1");
	curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, aError);
	curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &Response);

	const CURLcode Result = PerformPresenceRequest(pCurl, StopRequested);
	long StatusCode = 0;
	curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &StatusCode);
	curl_slist_free_all(pHeaders);
	curl_easy_cleanup(pCurl);
	if(Result != CURLE_OK || StatusCode != 201 || Response.empty())
		return false;

	json_value *pJson = json_parse(Response.data(), Response.size());
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		return false;
	}
	const json_value *pClientId = json_object_get(pJson, "clientId");
	const json_value *pToken = json_object_get(pJson, "token");
	const bool Valid = pClientId != &json_value_none && pClientId->type == json_string && pToken != &json_value_none && pToken->type == json_string;
	if(Valid)
	{
		ClientId = pClientId->u.string.ptr;
		Token = pToken->u.string.ptr;
	}
	json_value_free(pJson);
	return Valid && IsValidIdentity(ClientId, Token);
}

bool ParseBrowserSnapshotPayload(const std::string &Payload, CAmfPresenceTransport::SRemoteEvent &Event);

bool FetchBrowserSnapshot(CAmfPresenceTransport::SRemoteEvent &Event, const std::atomic<bool> &StopRequested)
{
	CURL *pCurl = curl_easy_init();
	if(!pCurl)
		return false;
	std::string Response;
	char aError[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(pCurl, CURLOPT_URL, AMF_PRESENCE_BROWSER_URL);
	curl_easy_setopt(pCurl, CURLOPT_PROTOCOLS_STR, "HTTPS");
	curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(BROWSER_SNAPSHOT_TIMEOUT_MS));
	curl_easy_setopt(pCurl, CURLOPT_TIMEOUT_MS, static_cast<long>(BROWSER_SNAPSHOT_TIMEOUT_MS));
	curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "AMF Client Presence/1");
	curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, aError);
	curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, CurlWriteBrowserSnapshot);
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &Response);
	const CURLcode Result = PerformPresenceRequest(pCurl, StopRequested);
	long StatusCode = 0;
	curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &StatusCode);
	curl_easy_cleanup(pCurl);
	return Result == CURLE_OK && StatusCode == 200 && ParseBrowserSnapshotPayload(Response, Event);
}

enum class EConnectResult
{
	CONNECTED,
	AUTH_FAILED,
	FAILED,
};

std::string Base64Encode(const unsigned char *pData, size_t Size)
{
	static constexpr char s_aAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Result;
	Result.reserve(((Size + 2) / 3) * 4);
	for(size_t Index = 0; Index < Size; Index += 3)
	{
		const uint32_t Value = static_cast<uint32_t>(pData[Index]) << 16 |
			(Index + 1 < Size ? static_cast<uint32_t>(pData[Index + 1]) << 8 : 0) |
			(Index + 2 < Size ? static_cast<uint32_t>(pData[Index + 2]) : 0);
		Result.push_back(s_aAlphabet[(Value >> 18) & 0x3f]);
		Result.push_back(s_aAlphabet[(Value >> 12) & 0x3f]);
		Result.push_back(Index + 1 < Size ? s_aAlphabet[(Value >> 6) & 0x3f] : '=');
		Result.push_back(Index + 2 < Size ? s_aAlphabet[Value & 0x3f] : '=');
	}
	return Result;
}

enum class EWebSocketFrameType : unsigned char
{
	CONTINUATION = 0x0,
	TEXT = 0x1,
	CLOSE = 0x8,
	PING = 0x9,
	PONG = 0xa,
};

struct SRawWebSocket
{
	CURL *m_pCurl = nullptr;
	std::vector<unsigned char> m_vWireInput;
	std::vector<unsigned char> m_vWireOutput;
	size_t m_OutputOffset = 0;
	std::string m_TextFragment;
	std::deque<std::string> m_ReceivedMessages;
	std::string m_Error;
	bool m_ReceivingText = false;
	bool m_Failed = false;
};

void DestroyWebSocket(SRawWebSocket &Connection)
{
	if(Connection.m_pCurl)
		curl_easy_cleanup(Connection.m_pCurl);
	Connection = SRawWebSocket{};
}

bool SendRawBlocking(CURL *pCurl, const char *pData, size_t Size, const std::atomic<bool> &StopRequested)
{
	const auto Deadline = std::chrono::steady_clock::now() + SEND_TIMEOUT;
	size_t Offset = 0;
	while(Offset < Size)
	{
		if(StopRequested.load(std::memory_order_relaxed))
			return false;
		size_t Sent = 0;
		const CURLcode Result = curl_easy_send(pCurl, pData + Offset, Size - Offset, &Sent);
		Offset += Sent;
		if(Result != CURLE_OK && Result != CURLE_AGAIN)
			return false;
		if(Offset == Size)
			return true;
		if(std::chrono::steady_clock::now() >= Deadline)
			return false;
		std::this_thread::sleep_for(10ms);
	}
	return true;
}

void QueueWebSocketFrame(SRawWebSocket &Connection, const std::string &Payload, EWebSocketFrameType Type)
{
	const size_t Start = Connection.m_vWireOutput.size();
	const uint64_t PayloadSize = Payload.size();
	Connection.m_vWireOutput.push_back(0x80 | static_cast<unsigned char>(Type));
	if(PayloadSize <= 125)
		Connection.m_vWireOutput.push_back(0x80 | static_cast<unsigned char>(PayloadSize));
	else if(PayloadSize <= 0xffff)
	{
		Connection.m_vWireOutput.push_back(0x80 | 126);
		Connection.m_vWireOutput.push_back(static_cast<unsigned char>(PayloadSize >> 8));
		Connection.m_vWireOutput.push_back(static_cast<unsigned char>(PayloadSize));
	}
	else
	{
		Connection.m_vWireOutput.push_back(0x80 | 127);
		for(int Shift = 56; Shift >= 0; Shift -= 8)
			Connection.m_vWireOutput.push_back(static_cast<unsigned char>(PayloadSize >> Shift));
	}
	unsigned char aMask[4];
	secure_random_fill(aMask, sizeof(aMask));
	Connection.m_vWireOutput.insert(Connection.m_vWireOutput.end(), std::begin(aMask), std::end(aMask));
	for(size_t Index = 0; Index < Payload.size(); ++Index)
		Connection.m_vWireOutput.push_back(static_cast<unsigned char>(Payload[Index]) ^ aMask[Index % 4]);
	if(Start > 0 && Connection.m_OutputOffset == Start)
	{
		Connection.m_vWireOutput.erase(Connection.m_vWireOutput.begin(), Connection.m_vWireOutput.begin() + Start);
		Connection.m_OutputOffset = 0;
	}
}

bool PumpWebSocket(SRawWebSocket &Connection)
{
	while(Connection.m_OutputOffset < Connection.m_vWireOutput.size())
	{
		size_t Sent = 0;
		const CURLcode Result = curl_easy_send(Connection.m_pCurl, Connection.m_vWireOutput.data() + Connection.m_OutputOffset,
			Connection.m_vWireOutput.size() - Connection.m_OutputOffset, &Sent);
		Connection.m_OutputOffset += Sent;
		if(Result == CURLE_AGAIN)
			break;
		if(Result != CURLE_OK)
		{
			Connection.m_Error = curl_easy_strerror(Result);
			return false;
		}
	}
	if(Connection.m_OutputOffset == Connection.m_vWireOutput.size())
	{
		Connection.m_vWireOutput.clear();
		Connection.m_OutputOffset = 0;
	}

	while(true)
	{
		unsigned char aBuffer[16 * 1024];
		size_t Received = 0;
		const CURLcode Result = curl_easy_recv(Connection.m_pCurl, aBuffer, sizeof(aBuffer), &Received);
		if(Result == CURLE_AGAIN)
			break;
		if(Result != CURLE_OK || Received == 0)
		{
			Connection.m_Error = Result == CURLE_OK ? "connection closed" : curl_easy_strerror(Result);
			return false;
		}
		if(Received > MAX_WIRE_BUFFER_SIZE || Connection.m_vWireInput.size() > MAX_WIRE_BUFFER_SIZE - Received)
		{
			Connection.m_Error = "incoming websocket buffer exceeds limit";
			return false;
		}
		Connection.m_vWireInput.insert(Connection.m_vWireInput.end(), aBuffer, aBuffer + Received);
	}

	size_t Offset = 0;
	while(Connection.m_vWireInput.size() - Offset >= 2)
	{
		const unsigned char First = Connection.m_vWireInput[Offset];
		const unsigned char Second = Connection.m_vWireInput[Offset + 1];
		const bool Final = (First & 0x80) != 0;
		if((First & 0x70) != 0 || (Second & 0x80) != 0)
		{
			Connection.m_Error = "invalid server websocket frame";
			return false;
		}
		uint64_t PayloadSize = Second & 0x7f;
		size_t HeaderSize = 2;
		if(PayloadSize == 126)
		{
			if(Connection.m_vWireInput.size() - Offset < 4)
				break;
			PayloadSize = static_cast<uint64_t>(Connection.m_vWireInput[Offset + 2]) << 8 | Connection.m_vWireInput[Offset + 3];
			HeaderSize = 4;
		}
		else if(PayloadSize == 127)
		{
			if(Connection.m_vWireInput.size() - Offset < 10)
				break;
			PayloadSize = 0;
			for(int Index = 0; Index < 8; ++Index)
				PayloadSize = PayloadSize << 8 | Connection.m_vWireInput[Offset + 2 + Index];
			HeaderSize = 10;
		}
		if(PayloadSize > MAX_SERVER_MESSAGE_SIZE || PayloadSize > Connection.m_vWireInput.size() - Offset - HeaderSize)
		{
			if(PayloadSize > MAX_SERVER_MESSAGE_SIZE)
			{
				Connection.m_Error = "incoming websocket frame exceeds limit";
				return false;
			}
			break;
		}
		const unsigned char Opcode = First & 0x0f;
		const char *pPayload = reinterpret_cast<const char *>(Connection.m_vWireInput.data() + Offset + HeaderSize);
		if(Opcode == static_cast<unsigned char>(EWebSocketFrameType::TEXT))
		{
			Connection.m_TextFragment.assign(pPayload, static_cast<size_t>(PayloadSize));
			Connection.m_ReceivingText = !Final;
			if(Final)
				Connection.m_ReceivedMessages.push_back(std::move(Connection.m_TextFragment));
		}
		else if(Opcode == static_cast<unsigned char>(EWebSocketFrameType::CONTINUATION) && Connection.m_ReceivingText)
		{
			if(PayloadSize > MAX_SERVER_MESSAGE_SIZE - Connection.m_TextFragment.size())
			{
				Connection.m_Error = "fragmented websocket message exceeds limit";
				return false;
			}
			Connection.m_TextFragment.append(pPayload, static_cast<size_t>(PayloadSize));
			if(Final)
			{
				Connection.m_ReceivingText = false;
				Connection.m_ReceivedMessages.push_back(std::move(Connection.m_TextFragment));
			}
		}
		else if(Opcode == static_cast<unsigned char>(EWebSocketFrameType::PING))
			QueueWebSocketFrame(Connection, std::string(pPayload, static_cast<size_t>(PayloadSize)), EWebSocketFrameType::PONG);
		else if(Opcode == static_cast<unsigned char>(EWebSocketFrameType::CLOSE))
		{
			Connection.m_Error = "server closed websocket";
			return false;
		}
		Offset += HeaderSize + static_cast<size_t>(PayloadSize);
	}
	if(Offset > 0)
		Connection.m_vWireInput.erase(Connection.m_vWireInput.begin(), Connection.m_vWireInput.begin() + Offset);
	return true;
}

EConnectResult ConnectWebSocket(const std::string &ClientId, const std::string &Token, const std::string &InstanceId, const std::string &RoomId, SRawWebSocket &Connection, bool Debug, const std::atomic<bool> &StopRequested)
{
	DestroyWebSocket(Connection);
	Connection.m_pCurl = curl_easy_init();
	if(!Connection.m_pCurl)
		return EConnectResult::FAILED;
	char aError[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_URL, AMF_PRESENCE_WEBSOCKET_ORIGIN);
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_PROTOCOLS_STR, "HTTPS");
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_CONNECT_ONLY, 1L);
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(CONNECT_TIMEOUT_MS));
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_TIMEOUT_MS, static_cast<long>(CONNECT_TIMEOUT_MS));
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(Connection.m_pCurl, CURLOPT_ERRORBUFFER, aError);
	const CURLcode ConnectResult = PerformPresenceConnect(Connection.m_pCurl, StopRequested);
	if(ConnectResult != CURLE_OK)
	{
		if(Debug)
			log_info("amf_presence", "websocket TCP/TLS connect failed curl=%d error='%s' detail='%s' room_id=%s installation_suffix=%s",
				(int)ConnectResult, curl_easy_strerror(ConnectResult), aError[0] ? aError : "-", RoomId.c_str(), InstallationIdSuffix(ClientId));
		DestroyWebSocket(Connection);
		return EConnectResult::FAILED;
	}

	unsigned char aKeyBytes[16];
	secure_random_fill(aKeyBytes, sizeof(aKeyBytes));
	const std::string Key = Base64Encode(aKeyBytes, sizeof(aKeyBytes));
	const std::string Path = std::string(AMF_PRESENCE_WEBSOCKET_PATH) + "?clientId=" + ClientId + "&instanceId=" + InstanceId + "&roomId=" + RoomId;
	const std::string Request = "GET " + Path + " HTTP/1.1\r\nHost: " + AMF_PRESENCE_WEBSOCKET_HOST +
		"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + Key +
		"\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: " + AMF_PRESENCE_PROTOCOL +
		"\r\nAuthorization: Bearer " + Token + "\r\nUser-Agent: AMF Client Presence/1\r\n\r\n";
	if(!SendRawBlocking(Connection.m_pCurl, Request.data(), Request.size(), StopRequested))
	{
		if(Debug)
			log_info("amf_presence", "websocket handshake send failed room_id=%s installation_suffix=%s", RoomId.c_str(), InstallationIdSuffix(ClientId));
		DestroyWebSocket(Connection);
		return EConnectResult::FAILED;
	}

	std::string Response;
	const int64_t Deadline = SteadyNowMs() + CONNECT_TIMEOUT_MS;
	while(!StopRequested.load(std::memory_order_relaxed) && Response.find("\r\n\r\n") == std::string::npos && SteadyNowMs() < Deadline)
	{
		char aBuffer[4096];
		size_t Received = 0;
		const CURLcode Result = curl_easy_recv(Connection.m_pCurl, aBuffer, sizeof(aBuffer), &Received);
		if(Result == CURLE_AGAIN)
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}
		if(Result != CURLE_OK || Received == 0 || Received > MAX_HANDSHAKE_RESPONSE_SIZE || Response.size() > MAX_HANDSHAKE_RESPONSE_SIZE - Received)
			break;
		Response.append(aBuffer, Received);
	}
	const size_t HeaderEnd = Response.find("\r\n\r\n");
	std::string LowerResponse = HeaderEnd == std::string::npos ? std::string() : Response.substr(0, HeaderEnd + 4);
	std::transform(LowerResponse.begin(), LowerResponse.end(), LowerResponse.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
	const bool Status101 = LowerResponse.rfind("http/1.1 101", 0) == 0 || LowerResponse.rfind("http/1.0 101", 0) == 0;
	const bool CorrectProtocol = LowerResponse.find("sec-websocket-protocol: " + std::string(AMF_PRESENCE_PROTOCOL)) != std::string::npos;
	if(!Status101 || !CorrectProtocol)
	{
		const bool AuthFailed = LowerResponse.rfind("http/1.1 401", 0) == 0 || LowerResponse.rfind("http/1.0 401", 0) == 0;
		if(Debug)
			log_info("amf_presence", "websocket handshake rejected status_101=%d protocol_ok=%d auth_failed=%d room_id=%s installation_suffix=%s",
				Status101, CorrectProtocol, AuthFailed, RoomId.c_str(), InstallationIdSuffix(ClientId));
		DestroyWebSocket(Connection);
		return AuthFailed ? EConnectResult::AUTH_FAILED : EConnectResult::FAILED;
	}
	const size_t PayloadStart = HeaderEnd + 4;
	if(PayloadStart < Response.size())
		Connection.m_vWireInput.assign(Response.begin() + PayloadStart, Response.end());
	return EConnectResult::CONNECTED;
}

std::string MakeUpdateMessage(uint64_t Sequence, const std::string &StateJson)
{
	return std::string("{\"v\":1,\"op\":\"presence.update\",\"seq\":") + std::to_string(Sequence) + ",\"state\":" + StateJson + "}";
}

std::string MakeOfflineMessage(uint64_t Sequence)
{
	return std::string("{\"v\":1,\"op\":\"presence.offline\",\"seq\":") + std::to_string(Sequence) + "}";
}

bool ReadJsonBool(const json_value *pObject, const char *pName, bool Default)
{
	if(!pObject || pObject->type != json_object)
		return Default;
	const json_value *pValue = json_object_get(pObject, pName);
	return pValue != &json_value_none && pValue->type == json_boolean ? pValue->u.boolean != 0 : Default;
}

void WritePackedColor(CJsonStringWriter &Json, unsigned Color)
{
	char aColor[9];
	str_format(aColor, sizeof(aColor), "%08x", Color);
	Json.WriteStrValue(aColor);
}

int HexDigit(char Character)
{
	if(Character >= '0' && Character <= '9')
		return Character - '0';
	if(Character >= 'a' && Character <= 'f')
		return Character - 'a' + 10;
	if(Character >= 'A' && Character <= 'F')
		return Character - 'A' + 10;
	return -1;
}

bool ReadPackedColor(const json_value *pObject, const char *pName, unsigned &Color)
{
	if(!pObject || pObject->type != json_object)
		return false;
	const json_value *pValue = json_object_get(pObject, pName);
	if(pValue == &json_value_none || pValue->type != json_string || pValue->u.string.length != 8)
		return false;

	unsigned Parsed = 0;
	for(unsigned Index = 0; Index < 8; ++Index)
	{
		const int Digit = HexDigit(pValue->u.string.ptr[Index]);
		if(Digit < 0)
			return false;
		Parsed = (Parsed << 4) | static_cast<unsigned>(Digit);
	}
	Color = Parsed;
	return true;
}

bool ParseRemoteConnection(const json_value *pConnections, const char *pName, CAmfPresenceTransport::SRemoteEvent::SConnection &Connection)
{
	if(!pConnections || pConnections->type != json_object)
		return false;
	const json_value *pConnection = json_object_get(pConnections, pName);
	if(pConnection == &json_value_none || pConnection->type != json_object)
		return false;

	Connection.m_Connected = ReadJsonBool(pConnection, "connected", false);
	Connection.m_SettingsOpen = ReadJsonBool(pConnection, "settingsOpen", false);
	Connection.m_FastPractice = ReadJsonBool(pConnection, "fastPractice", false);
	Connection.m_CustomGradient = ReadJsonBool(pConnection, "customGradient", false);
	if(!Connection.m_Connected)
		return true;

	const json_value *pGameClientId = json_object_get(pConnection, "clientId");
	const json_value *pPlayerName = json_object_get(pConnection, "playerName");
	if(pGameClientId == &json_value_none || pGameClientId->type != json_integer ||
		pPlayerName == &json_value_none || pPlayerName->type != json_string)
		return false;
	if(pGameClientId->u.integer < 0 || pGameClientId->u.integer >= MAX_CLIENTS ||
		pPlayerName->u.string.length == 0 || pPlayerName->u.string.length >= MAX_NAME_LENGTH)
		return false;

	Connection.m_GameClientId = static_cast<int>(pGameClientId->u.integer);
	Connection.m_PlayerName.assign(pPlayerName->u.string.ptr, pPlayerName->u.string.length);
	if(!str_utf8_check(Connection.m_PlayerName.c_str()))
		return false;

	// Gradient fields are optional so older AMF clients remain fully valid.
	// An incomplete or malformed optional gradient only disables that visual
	// override; it must never discard the member's Presence identity.
	if(Connection.m_CustomGradient)
	{
		const json_value *pPosition = json_object_get(pConnection, "gradientPosition");
		const json_value *pStrength = json_object_get(pConnection, "gradientStrength");
		if(!ReadPackedColor(pConnection, "gradientColor1", Connection.m_GradientColor1) ||
			!ReadPackedColor(pConnection, "gradientColor2", Connection.m_GradientColor2) ||
			pPosition == &json_value_none || pPosition->type != json_integer || pPosition->u.integer < 0 || pPosition->u.integer > 1 ||
			pStrength == &json_value_none || pStrength->type != json_integer || pStrength->u.integer < 0 || pStrength->u.integer > 100)
		{
			Connection.m_CustomGradient = false;
		}
		else
		{
			Connection.m_GradientPosition = static_cast<int>(pPosition->u.integer);
			Connection.m_GradientStrength = static_cast<int>(pStrength->u.integer);
		}
	}
	return true;
}

bool ParseRemotePresence(const json_value *pRecord, CAmfPresenceTransport::SRemoteEvent::SPresence &Presence)
{
	if(!pRecord || pRecord->type != json_object)
		return false;
	const json_value *pClientId = json_object_get(pRecord, "clientId");
	const json_value *pState = json_object_get(pRecord, "state");
	if(pClientId == &json_value_none || pClientId->type != json_string ||
		pState == &json_value_none || pState->type != json_object)
		return false;

	Presence.m_ClientId.assign(pClientId->u.string.ptr, pClientId->u.string.length);
	if(!IsValidClientId(Presence.m_ClientId))
		return false;
	const json_value *pPresenceId = json_object_get(pRecord, "presenceId");
	Presence.m_PresenceId = Presence.m_ClientId;
	if(pPresenceId != &json_value_none)
	{
		if(pPresenceId->type != json_string)
			return false;
		Presence.m_PresenceId.assign(pPresenceId->u.string.ptr, pPresenceId->u.string.length);
		if(!IsValidPresenceId(Presence.m_PresenceId, Presence.m_ClientId))
			return false;
	}
	const json_value *pConnections = json_object_get(pState, "connections");
	return ParseRemoteConnection(pConnections, "main", Presence.m_Main) &&
		ParseRemoteConnection(pConnections, "dummy", Presence.m_Dummy);
}

bool ParseRemoteEventPayload(const json_value *pJson, CAmfPresenceTransport::SRemoteEvent &Event)
{
	if(!pJson || pJson->type != json_object)
		return false;
	const json_value *pOp = json_object_get(pJson, "op");
	if(pOp == &json_value_none || pOp->type != json_string)
		return false;

	if(str_comp(pOp->u.string.ptr, "presence.snapshot") == 0)
	{
		const json_value *pMembers = json_object_get(pJson, "members");
		if(pMembers == &json_value_none || pMembers->type != json_array || pMembers->u.array.length > 256)
			return false;
		Event.m_Type = CAmfPresenceTransport::ERemoteEventType::SNAPSHOT;
		Event.m_SnapshotComplete = true;
		Event.m_vPresence.reserve(pMembers->u.array.length);
		for(unsigned Index = 0; Index < pMembers->u.array.length; ++Index)
		{
			CAmfPresenceTransport::SRemoteEvent::SPresence Presence;
			if(ParseRemotePresence(pMembers->u.array.values[Index], Presence))
				Event.m_vPresence.push_back(std::move(Presence));
			else
				Event.m_SnapshotComplete = false;
		}
		return true;
	}

	const json_value *pClientId = json_object_get(pJson, "clientId");
	if(pClientId == &json_value_none || pClientId->type != json_string)
		return false;
	Event.m_ClientId.assign(pClientId->u.string.ptr, pClientId->u.string.length);
	if(!IsValidClientId(Event.m_ClientId))
		return false;
	const json_value *pPresenceId = json_object_get(pJson, "presenceId");
	Event.m_PresenceId = Event.m_ClientId;
	if(pPresenceId != &json_value_none)
	{
		if(pPresenceId->type != json_string)
			return false;
		Event.m_PresenceId.assign(pPresenceId->u.string.ptr, pPresenceId->u.string.length);
		if(!IsValidPresenceId(Event.m_PresenceId, Event.m_ClientId))
			return false;
	}

	if(str_comp(pOp->u.string.ptr, "presence.delta") == 0)
	{
		Event.m_Type = CAmfPresenceTransport::ERemoteEventType::DELTA;
		CAmfPresenceTransport::SRemoteEvent::SPresence Presence;
		if(!ParseRemotePresence(pJson, Presence))
			return false;
		Event.m_vPresence.push_back(std::move(Presence));
		return true;
	}
	if(str_comp(pOp->u.string.ptr, "presence.offline") == 0)
	{
		Event.m_Type = CAmfPresenceTransport::ERemoteEventType::OFFLINE;
		return true;
	}
	return false;
}

bool ParseBrowserSnapshotPayload(const std::string &Payload, CAmfPresenceTransport::SRemoteEvent &Event)
{
	json_value *pJson = json_parse(Payload.data(), Payload.size());
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		return false;
	}
	const json_value *pTotal = json_object_get(pJson, "totalActiveUsers");
	const json_value *pServers = json_object_get(pJson, "servers");
	if(pTotal == &json_value_none || pTotal->type != json_integer || pTotal->u.integer < 0 || pTotal->u.integer > 1000000 ||
		pServers == &json_value_none || pServers->type != json_array || pServers->u.array.length > 16384)
	{
		json_value_free(pJson);
		return false;
	}

	Event.m_Type = CAmfPresenceTransport::ERemoteEventType::BROWSER_SNAPSHOT;
	Event.m_SnapshotComplete = true;
	Event.m_TotalActiveUsers = static_cast<int>(pTotal->u.integer);
	Event.m_vBrowserServers.reserve(pServers->u.array.length);
	for(unsigned Index = 0; Index < pServers->u.array.length; ++Index)
	{
		const json_value *pServer = pServers->u.array.values[Index];
		if(!pServer || pServer->type != json_object)
		{
			Event.m_SnapshotComplete = false;
			continue;
		}
		const json_value *pServerKey = json_object_get(pServer, "serverKey");
		const json_value *pCount = json_object_get(pServer, "count");
		if(pServerKey == &json_value_none || pServerKey->type != json_string || pServerKey->u.string.length == 0 || pServerKey->u.string.length >= NETADDR_MAXSTRSIZE ||
			pCount == &json_value_none || pCount->type != json_integer || pCount->u.integer < 1 || pCount->u.integer > MAX_CLIENTS)
		{
			Event.m_SnapshotComplete = false;
			continue;
		}
		CAmfPresenceTransport::SRemoteEvent::SBrowserServer Server;
		Server.m_ServerKey.assign(pServerKey->u.string.ptr, pServerKey->u.string.length);
		if(!str_utf8_check(Server.m_ServerKey.c_str()))
		{
			Event.m_SnapshotComplete = false;
			continue;
		}
		Server.m_ActiveCount = static_cast<int>(pCount->u.integer);
		const json_value *pPlayers = json_object_get(pServer, "players");
		if(pPlayers != &json_value_none && pPlayers->type == json_array && pPlayers->u.array.length <= 2 * MAX_CLIENTS)
		{
			Server.m_vPlayerNames.reserve(pPlayers->u.array.length);
			for(unsigned PlayerIndex = 0; PlayerIndex < pPlayers->u.array.length; ++PlayerIndex)
			{
				const json_value *pPlayerName = pPlayers->u.array.values[PlayerIndex];
				if(!pPlayerName || pPlayerName->type != json_string || pPlayerName->u.string.length == 0 || pPlayerName->u.string.length >= MAX_NAME_LENGTH)
				{
					Event.m_SnapshotComplete = false;
					continue;
				}
				std::string PlayerName(pPlayerName->u.string.ptr, pPlayerName->u.string.length);
				if(str_utf8_check(PlayerName.c_str()))
					Server.m_vPlayerNames.push_back(std::move(PlayerName));
				else
					Event.m_SnapshotComplete = false;
			}
		}
		else if(pPlayers != &json_value_none)
			Event.m_SnapshotComplete = false;
		// A positive server count without any player identities is not a complete
		// browser claim. Treat it like any other partial response so the previous
		// confirmed names remain published until a usable replacement arrives.
		if(Server.m_ActiveCount > 0 && Server.m_vPlayerNames.empty())
			Event.m_SnapshotComplete = false;
		Event.m_vBrowserServers.push_back(std::move(Server));
	}
	// A non-zero global total with no materialized server entries is an
	// internally inconsistent response (usually a transient backend/HTTP
	// partial result). Do not let it erase the previous published browser
	// metadata. A valid zero-total snapshot remains authoritative and complete.
	if(Event.m_TotalActiveUsers > 0 && Event.m_vBrowserServers.empty())
		Event.m_SnapshotComplete = false;
	json_value_free(pJson);
	return true;
}
}

CAmfPresenceTransport::~CAmfPresenceTransport()
{
	StopWorker();
}

bool CAmfPresenceTransport::SLocalState::operator==(const SLocalState &Other) const
{
	return m_Online == Other.m_Online && m_Afk == Other.m_Afk && m_EscOpen == Other.m_EscOpen &&
		m_MainConnected == Other.m_MainConnected && m_DummyConnected == Other.m_DummyConnected &&
		m_MainSettingsOpen == Other.m_MainSettingsOpen && m_DummySettingsOpen == Other.m_DummySettingsOpen &&
		m_MainFastPractice == Other.m_MainFastPractice && m_DummyFastPractice == Other.m_DummyFastPractice &&
		m_MainCustomGradient == Other.m_MainCustomGradient && m_DummyCustomGradient == Other.m_DummyCustomGradient &&
		m_MainGradientColor1 == Other.m_MainGradientColor1 && m_MainGradientColor2 == Other.m_MainGradientColor2 &&
		m_DummyGradientColor1 == Other.m_DummyGradientColor1 && m_DummyGradientColor2 == Other.m_DummyGradientColor2 &&
		m_MainGradientPosition == Other.m_MainGradientPosition && m_DummyGradientPosition == Other.m_DummyGradientPosition &&
		m_MainGradientStrength == Other.m_MainGradientStrength && m_DummyGradientStrength == Other.m_DummyGradientStrength &&
		m_ActiveDummy == Other.m_ActiveDummy && m_Spectating == Other.m_Spectating &&
		m_MainClientId == Other.m_MainClientId && m_DummyClientId == Other.m_DummyClientId &&
		m_MainPlayerName == Other.m_MainPlayerName && m_DummyPlayerName == Other.m_DummyPlayerName &&
		m_ServerAddress == Other.m_ServerAddress;
}

void CAmfPresenceTransport::OnInit()
{
	char aInstanceId[UUID_MAXSTRSIZE];
	FormatUuid(RandomUuid(), aInstanceId, sizeof(aInstanceId));
	m_InstanceId = aInstanceId;
	m_Debug.store(g_Config.m_AmfPresenceDebug != 0, std::memory_order_relaxed);
	const std::string CurrentStorageId = StorageIdentity(Storage());
	const bool MissingStorageBinding = g_Config.m_AmfPresenceStorageId[0] == '\0';
	const bool ChangedStorageBinding = !MissingStorageBinding && CurrentStorageId != g_Config.m_AmfPresenceStorageId;
	if(MissingStorageBinding || ChangedStorageBinding)
	{
		const bool HadCredentials = g_Config.m_AmfPresenceClientId[0] != '\0' || g_Config.m_AmfPresenceToken[0] != '\0';
		str_copy(g_Config.m_AmfPresenceStorageId, CurrentStorageId.c_str());
		// Credentials without a storage binding are migrated once. A copied portable
		// profile therefore cannot keep replacing the original installation session.
		g_Config.m_AmfPresenceClientId[0] = '\0';
		g_Config.m_AmfPresenceToken[0] = '\0';
		ConfigManager()->Save();
		if(g_Config.m_AmfPresenceDebug)
			log_info("amf_presence", "identity storage binding initialized credentials_reset=%d reason=%s", HadCredentials, ChangedStorageBinding ? "profile_path_changed" : "one_time_migration");
	}
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Desired.m_ClientId = g_Config.m_AmfPresenceClientId;
		m_Desired.m_Token = g_Config.m_AmfPresenceToken;
	}
	if(g_Config.m_AmfPresenceDebug && g_Config.m_AmfPresenceClientId[0] != '\0')
		log_info("amf_presence", "identity loaded installation_suffix=%s", InstallationIdSuffix(g_Config.m_AmfPresenceClientId));
	StartWorker();
}

void CAmfPresenceTransport::OnShutdown()
{
	StopWorker();
}

void CAmfPresenceTransport::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
	{
		ClearRemoteEvents();
		m_HaveLastLocalState = false;
		m_LastRoomSource.clear();
		m_LastRoomId.clear();
	}
	m_Condition.notify_all();
}

void CAmfPresenceTransport::StartWorker()
{
	if(m_WorkerStarted)
		return;
	m_StopRequested.store(false, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Stop = false;
	}
	m_Worker = std::thread(&CAmfPresenceTransport::WorkerLoop, this);
	m_WorkerStarted = true;
}

void CAmfPresenceTransport::StopWorker()
{
	if(!m_WorkerStarted)
		return;
	m_StopRequested.store(true, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_Stop = true;
		++m_Desired.m_Revision;
	}
	m_Condition.notify_all();
	if(m_Worker.joinable())
		m_Worker.join();
	m_WorkerStarted = false;
	m_Connected.store(false, std::memory_order_relaxed);
}

void CAmfPresenceTransport::QueueCredentialUpdate(const std::string &ClientId, const std::string &Token)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_CredentialClientId = ClientId;
	m_CredentialToken = Token;
	m_HasCredentialUpdate = true;
}

void CAmfPresenceTransport::QueueRemoteEvent(SRemoteEvent Event)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(Event.m_Type == ERemoteEventType::DISCONNECTED)
		m_RemoteEvents.clear();
	while(m_RemoteEvents.size() >= MAX_REMOTE_EVENTS)
		m_RemoteEvents.pop_front();
	m_RemoteEvents.push_back(std::move(Event));
}

void CAmfPresenceTransport::ClearRemoteEvents()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	m_RemoteEvents.clear();
}

bool CAmfPresenceTransport::PollRemoteEvent(SRemoteEvent &Event)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_RemoteEvents.empty())
		return false;
	Event = std::move(m_RemoteEvents.front());
	m_RemoteEvents.pop_front();
	return true;
}

std::string CAmfPresenceTransport::BuildRoomId()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return {};

	char aAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddress, sizeof(aAddress), true);
	CServerInfo ServerInfo;
	Client()->GetServerInfo(&ServerInfo);

	std::string RoomSource;
	const bool HasStableServerInfo = ServerInfo.m_aName[0] != '\0' && ServerInfo.m_aMap[0] != '\0';
	if(HasStableServerInfo)
	{
		// The endpoint is intentionally not part of this identity. One advertised
		// DDNet server can be reached through IPv4, IPv6 or another address alias.
		// Length prefixes avoid ambiguous concatenations of server-provided text.
		const auto AppendField = [&](const char *pValue) {
			RoomSource += std::to_string(str_length(pValue));
			RoomSource += ':';
			RoomSource += pValue;
			RoomSource += '|';
		};
		RoomSource = "server-v2|port:" + std::to_string(Client()->ServerAddress().port) + '|';
		AppendField(ServerInfo.m_aName);
		AppendField(ServerInfo.m_aMap);
		AppendField(ServerInfo.m_aGameType);
		RoomSource += "crc:" + std::to_string(ServerInfo.m_MapCrc) + "|size:" + std::to_string(ServerInfo.m_MapSize);
	}
	else
	{
		// This fallback exists only during the very short interval before the
		// connected server has supplied its info. It is replaced immediately when
		// the stable identity above becomes available.
		RoomSource = std::string("endpoint-v1|") + aAddress;
	}

	if(m_LastRoomSource == RoomSource)
		return m_LastRoomId;
	m_LastRoomSource = RoomSource;
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(RoomSource.data(), RoomSource.size()), aHash, sizeof(aHash));
	m_LastRoomId = std::string("ddnet:") + aHash;
	if(g_Config.m_AmfPresenceDebug)
		log_info("amf_presence", "room endpoint='%s' identity=%s room_id='%s'", aAddress, HasStableServerInfo ? "server-v2" : "endpoint-v1", m_LastRoomId.c_str());
	return m_LastRoomId;
}

std::string CAmfPresenceTransport::BuildStateJson(const SLocalState &State) const
{
	CJsonStringWriter Json;
	Json.BeginObject();
	Json.WriteAttribute("status");
	Json.WriteStrValue(State.m_Spectating ? "spectating" : "playing");
	Json.WriteAttribute("afk");
	Json.WriteBoolValue(State.m_Afk);
	Json.WriteAttribute("escOpen");
	Json.WriteBoolValue(State.m_EscOpen);
	Json.WriteAttribute("activeConnection");
	Json.WriteStrValue(State.m_ActiveDummy ? "dummy" : "main");
	if(!State.m_ServerAddress.empty())
	{
		Json.WriteAttribute("server");
		Json.BeginObject();
		Json.WriteAttribute("address");
		Json.WriteStrValue(State.m_ServerAddress.c_str());
		Json.EndObject();
	}
	Json.WriteAttribute("connections");
	Json.BeginObject();
	const auto WriteConnection = [&Json](bool Connected, bool SettingsOpen, bool FastPractice, bool CustomGradient, unsigned GradientColor1, unsigned GradientColor2, int GradientPosition, int GradientStrength, int ClientId, const std::string &PlayerName) {
		Json.BeginObject();
		Json.WriteAttribute("connected");
		Json.WriteBoolValue(Connected);
		Json.WriteAttribute("settingsOpen");
		Json.WriteBoolValue(SettingsOpen);
		Json.WriteAttribute("fastPractice");
		Json.WriteBoolValue(FastPractice);
		Json.WriteAttribute("customGradient");
		Json.WriteBoolValue(CustomGradient);
		if(CustomGradient)
		{
			Json.WriteAttribute("gradientColor1");
			WritePackedColor(Json, GradientColor1);
			Json.WriteAttribute("gradientColor2");
			WritePackedColor(Json, GradientColor2);
			Json.WriteAttribute("gradientPosition");
			Json.WriteIntValue(GradientPosition);
			Json.WriteAttribute("gradientStrength");
			Json.WriteIntValue(GradientStrength);
		}
		Json.WriteAttribute("clientId");
		Json.WriteIntValue(ClientId);
		Json.WriteAttribute("playerName");
		Json.WriteStrValue(PlayerName.c_str());
		Json.EndObject();
	};
	Json.WriteAttribute("main");
	WriteConnection(State.m_MainConnected, State.m_MainSettingsOpen, State.m_MainFastPractice, State.m_MainCustomGradient,
		State.m_MainGradientColor1, State.m_MainGradientColor2, State.m_MainGradientPosition, State.m_MainGradientStrength,
		State.m_MainClientId, State.m_MainPlayerName);
	Json.WriteAttribute("dummy");
	WriteConnection(State.m_DummyConnected, State.m_DummySettingsOpen, State.m_DummyFastPractice, State.m_DummyCustomGradient,
		State.m_DummyGradientColor1, State.m_DummyGradientColor2, State.m_DummyGradientPosition, State.m_DummyGradientStrength,
		State.m_DummyClientId, State.m_DummyPlayerName);
	Json.EndObject();
	Json.WriteAttribute("indicators");
	Json.BeginArray();
	if(State.m_EscOpen)
		Json.WriteStrValue("settings-gear");
	Json.EndArray();
	Json.EndObject();
	return Json.GetOutputString();
}

void CAmfPresenceTransport::SubmitDesiredState(bool Enabled, bool BrowserSnapshotEnabled, const std::string &RoomId, const std::string &StateJson)
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	if(m_Desired.m_Enabled == Enabled && m_Desired.m_BrowserSnapshotEnabled == BrowserSnapshotEnabled && m_Desired.m_RoomId == RoomId && m_Desired.m_StateJson == StateJson)
		return;
	m_Desired.m_Enabled = Enabled;
	m_Desired.m_BrowserSnapshotEnabled = BrowserSnapshotEnabled;
	m_Desired.m_RoomId = RoomId;
	m_Desired.m_StateJson = StateJson;
	++m_Desired.m_Revision;
	m_Condition.notify_all();
}

void CAmfPresenceTransport::RequestBrowserSnapshot()
{
	std::lock_guard<std::mutex> Lock(m_Mutex);
	++m_Desired.m_BrowserSnapshotRequestSerial;
	++m_Desired.m_WakeupSerial;
	m_Condition.notify_all();
}

void CAmfPresenceTransport::OnUpdate()
{
	m_Debug.store(g_Config.m_AmfPresenceDebug != 0, std::memory_order_relaxed);
	std::string CredentialClientId;
	std::string CredentialToken;
	bool HasCredentialUpdate = false;
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		if(m_HasCredentialUpdate)
		{
			CredentialClientId = m_CredentialClientId;
			CredentialToken = m_CredentialToken;
			m_HasCredentialUpdate = false;
			HasCredentialUpdate = true;
		}
	}
	if(HasCredentialUpdate)
	{
		str_copy(g_Config.m_AmfPresenceClientId, CredentialClientId.c_str());
		str_copy(g_Config.m_AmfPresenceToken, CredentialToken.c_str());
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Desired.m_ClientId = CredentialClientId;
			m_Desired.m_Token = CredentialToken;
			++m_Desired.m_Revision;
		}
		ConfigManager()->Save();
		m_Condition.notify_all();
	}

	const bool Enabled = g_Config.m_AmfPresenceEnabled != 0;
	SLocalState State;
	State.m_Online = Client()->State() == IClient::STATE_ONLINE;
	if(State.m_Online)
	{
		char aServerAddress[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aServerAddress, sizeof(aServerAddress), true);
		State.m_ServerAddress = aServerAddress;
	}
	State.m_ActiveDummy = g_Config.m_ClDummy != 0 && Client()->DummyConnected();
	const int MainClientId = GameClient()->m_aLocalIds[IClient::CONN_MAIN];
	const int DummyClientId = GameClient()->m_aLocalIds[IClient::CONN_DUMMY];
	State.m_MainConnected = State.m_Online && MainClientId >= 0 && MainClientId < MAX_CLIENTS && GameClient()->m_aClients[MainClientId].m_Active;
	State.m_DummyConnected = State.m_Online && Client()->DummyConnected() && DummyClientId >= 0 && DummyClientId < MAX_CLIENTS && GameClient()->m_aClients[DummyClientId].m_Active;
	State.m_MainClientId = State.m_MainConnected ? MainClientId : -1;
	State.m_DummyClientId = State.m_DummyConnected ? DummyClientId : -1;
	if(State.m_MainConnected)
		State.m_MainPlayerName = GameClient()->m_aClients[State.m_MainClientId].m_aName;
	if(State.m_DummyConnected)
		State.m_DummyPlayerName = GameClient()->m_aClients[State.m_DummyClientId].m_aName;
	State.m_EscOpen = State.m_Online && GameClient()->m_Menus.IsActive();
	State.m_MainSettingsOpen = State.m_EscOpen && !State.m_ActiveDummy && State.m_MainConnected;
	State.m_DummySettingsOpen = State.m_EscOpen && State.m_ActiveDummy && State.m_DummyConnected;
	const bool FastPracticeActive = GameClient()->m_FastPractice.Active();
	State.m_MainFastPractice = FastPracticeActive && State.m_MainConnected && GameClient()->m_FastPractice.IsPracticeParticipant(State.m_MainClientId);
	State.m_DummyFastPractice = FastPracticeActive && State.m_DummyConnected && GameClient()->m_FastPractice.IsPracticeParticipant(State.m_DummyClientId);
	const bool PublishCustomGradient = g_Config.m_AmfGradientCustomSelf != 0;
	State.m_MainCustomGradient = PublishCustomGradient && State.m_MainConnected;
	State.m_DummyCustomGradient = PublishCustomGradient && State.m_DummyConnected;
	if(State.m_MainCustomGradient)
	{
		State.m_MainGradientColor1 = g_Config.m_AmfGradientColor1;
		State.m_MainGradientColor2 = g_Config.m_AmfGradientColor2;
		State.m_MainGradientPosition = g_Config.m_AmfGradientPosition;
		State.m_MainGradientStrength = g_Config.m_AmfGradientStrength;
	}
	if(State.m_DummyCustomGradient)
	{
		State.m_DummyGradientColor1 = g_Config.m_AmfGradientColor1;
		State.m_DummyGradientColor2 = g_Config.m_AmfGradientColor2;
		State.m_DummyGradientPosition = g_Config.m_AmfGradientPosition;
		State.m_DummyGradientStrength = g_Config.m_AmfGradientStrength;
	}
	const int ActiveConnection = State.m_ActiveDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConnection];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS)
	{
		State.m_Afk = GameClient()->m_aClients[ActiveClientId].m_Afk;
		State.m_Spectating = GameClient()->m_aClients[ActiveClientId].m_Team == TEAM_SPECTATORS;
	}

	const std::string RoomId = Enabled && State.m_Online ? BuildRoomId() : std::string();
	if(!m_HaveLastLocalState || State != m_LastLocalState)
	{
		m_LastLocalState = State;
		m_HaveLastLocalState = true;
		m_LastStateJson = BuildStateJson(State);
		if(g_Config.m_AmfPresenceDebug)
		{
			char aMainName[MAX_NAME_LENGTH];
			char aDummyName[MAX_NAME_LENGTH];
			log_info("amf_presence", "local state installation_suffix=%s main={connected=%d client_id=%d name='%s' settings_open=%d fast_practice=%d} dummy={connected=%d client_id=%d name='%s' settings_open=%d fast_practice=%d} active=%s esc_open=%d",
				InstallationIdSuffix(g_Config.m_AmfPresenceClientId),
				State.m_MainConnected, State.m_MainClientId, SafePlayerName(State.m_MainPlayerName, aMainName), State.m_MainSettingsOpen, State.m_MainFastPractice,
				State.m_DummyConnected, State.m_DummyClientId, SafePlayerName(State.m_DummyPlayerName, aDummyName), State.m_DummySettingsOpen, State.m_DummyFastPractice,
				State.m_ActiveDummy ? "dummy" : "main", State.m_EscOpen);
		}
	}
	const bool BrowserSnapshotEnabled = GameClient()->m_Menus.IsActive() &&
		g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5;
	SubmitDesiredState(Enabled, BrowserSnapshotEnabled, RoomId, m_LastStateJson);
}

void CAmfPresenceTransport::WorkerLoop()
{
	std::random_device RandomDevice;
	std::mt19937_64 Random(static_cast<uint64_t>(RandomDevice()) ^ static_cast<uint64_t>(SteadyNowMs()));
	std::string ClientId;
	std::string Token;
	std::string ConnectedRoom;
	SRawWebSocket WebSocket;
	uint64_t Sequence = 0;
	uint64_t SentRevision = 0;
	unsigned int FailureCount = 0;
	int64_t NextAttemptAt = 0;
	int64_t ConnectedAt = 0;
	int64_t NextPresenceRefreshAt = 0;
	int64_t NextBrowserSnapshotAt = 0;
	int64_t LastBrowserSnapshotAt = -BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS;
	uint64_t CompletedBrowserSnapshotRequestSerial = 0;
	uint64_t BrowserSnapshotAwaitingAckSequence = 0;

	auto RefreshBrowserSnapshot = [&]() {
		SRemoteEvent Event;
		if(FetchBrowserSnapshot(Event, m_StopRequested))
			QueueRemoteEvent(std::move(Event));
		LastBrowserSnapshotAt = SteadyNowMs();
	};

	auto CloseWebSocket = [&](bool SendOffline) {
		if(WebSocket.m_pCurl)
		{
			if(SendOffline)
			{
				QueueWebSocketFrame(WebSocket, MakeOfflineMessage(++Sequence), EWebSocketFrameType::TEXT);
				const int64_t FlushDeadline = SteadyNowMs() + 200;
				while(!WebSocket.m_vWireOutput.empty() && SteadyNowMs() < FlushDeadline)
				{
					if(!PumpWebSocket(WebSocket))
						break;
					std::this_thread::sleep_for(5ms);
				}
			}
			DestroyWebSocket(WebSocket);
		}
		if(m_Connected.exchange(false, std::memory_order_relaxed))
			QueueRemoteEvent({ERemoteEventType::DISCONNECTED});
		ConnectedRoom.clear();
		Sequence = 0;
		SentRevision = 0;
		BrowserSnapshotAwaitingAckSequence = 0;
	};

	auto ScheduleReconnect = [&]() {
		if(ConnectedAt != 0 && SteadyNowMs() - ConnectedAt >= STABLE_CONNECTION_TIME_MS)
			FailureCount = 0;
		const unsigned int Shift = std::min(FailureCount, 6u);
		const int Cap = std::min(BACKOFF_MAX_MS, BACKOFF_BASE_MS << Shift);
		std::uniform_int_distribution<int> Distribution(std::max(1, Cap / 2), Cap);
		NextAttemptAt = SteadyNowMs() + Distribution(Random);
		FailureCount = std::min(FailureCount + 1, 30u);
	};

	while(true)
	{
		SDesiredState Desired;
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			if(m_Stop)
				break;
			Desired = m_Desired;
		}

		if(!Desired.m_ClientId.empty() && !Desired.m_Token.empty() && (ClientId.empty() || Token.empty()))
		{
			ClientId = Desired.m_ClientId;
			Token = Desired.m_Token;
		}

		if(!Desired.m_Enabled || Desired.m_RoomId.empty())
		{
			CloseWebSocket(true);
			FailureCount = 0;
			NextAttemptAt = 0;
			const int64_t Now = SteadyNowMs();
			if(Desired.m_BrowserSnapshotRequestSerial != CompletedBrowserSnapshotRequestSerial && Now - LastBrowserSnapshotAt >= BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS)
			{
				RefreshBrowserSnapshot();
				CompletedBrowserSnapshotRequestSerial = Desired.m_BrowserSnapshotRequestSerial;
				continue;
			}
			if(Desired.m_BrowserSnapshotEnabled && Now >= NextBrowserSnapshotAt)
			{
				RefreshBrowserSnapshot();
				NextBrowserSnapshotAt = SteadyNowMs() + BROWSER_SNAPSHOT_INTERVAL_MS;
				continue;
			}
			std::unique_lock<std::mutex> Lock(m_Mutex);
			const uint64_t Revision = m_Desired.m_Revision;
			const uint64_t WakeupSerial = m_Desired.m_WakeupSerial;
			const int64_t WaitMs = Desired.m_BrowserSnapshotEnabled ? std::max<int64_t>(1, NextBrowserSnapshotAt - Now) : BROWSER_SNAPSHOT_INTERVAL_MS;
			m_Condition.wait_for(Lock, std::chrono::milliseconds(WaitMs), [&] { return m_Stop || m_Desired.m_Revision != Revision || m_Desired.m_WakeupSerial != WakeupSerial; });
			continue;
		}

		if(WebSocket.m_pCurl && ConnectedRoom != Desired.m_RoomId)
		{
			CloseWebSocket(true);
			FailureCount = 0;
			NextAttemptAt = 0;
			continue;
		}

		const int64_t Now = SteadyNowMs();
		const bool ManualBrowserSnapshotPending = Desired.m_BrowserSnapshotRequestSerial != CompletedBrowserSnapshotRequestSerial;
		if(ManualBrowserSnapshotPending && BrowserSnapshotAwaitingAckSequence == 0 && WebSocket.m_pCurl && !Desired.m_StateJson.empty())
		{
			// A manual browser refresh first re-publishes the current state and
			// waits for its ack. This makes self-presence part of the authoritative
			// snapshot without a local +1 workaround.
			BrowserSnapshotAwaitingAckSequence = ++Sequence;
			QueueWebSocketFrame(WebSocket, MakeUpdateMessage(BrowserSnapshotAwaitingAckSequence, Desired.m_StateJson), EWebSocketFrameType::TEXT);
		}
		if(ManualBrowserSnapshotPending && BrowserSnapshotAwaitingAckSequence == 0 && Now - LastBrowserSnapshotAt >= BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS)
		{
			RefreshBrowserSnapshot();
			CompletedBrowserSnapshotRequestSerial = Desired.m_BrowserSnapshotRequestSerial;
			continue;
		}
		if(Desired.m_BrowserSnapshotEnabled && !ManualBrowserSnapshotPending && Now >= NextBrowserSnapshotAt)
		{
			RefreshBrowserSnapshot();
			NextBrowserSnapshotAt = SteadyNowMs() + BROWSER_SNAPSHOT_INTERVAL_MS;
		}
		if(!WebSocket.m_pCurl && Now < NextAttemptAt)
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			const uint64_t Revision = m_Desired.m_Revision;
			const uint64_t WakeupSerial = m_Desired.m_WakeupSerial;
			m_Condition.wait_for(Lock, std::chrono::milliseconds(NextAttemptAt - Now), [&] { return m_Stop || m_Desired.m_Revision != Revision || m_Desired.m_WakeupSerial != WakeupSerial; });
			continue;
		}

		if(!IsValidIdentity(ClientId, Token))
		{
			ClientId.clear();
			Token.clear();
			if(!RegisterInstallation(ClientId, Token, m_StopRequested))
			{
				ScheduleReconnect();
				continue;
			}
			QueueCredentialUpdate(ClientId, Token);
			if(m_Debug.load(std::memory_order_relaxed))
				log_info("amf_presence", "registration created installation_suffix=%s", InstallationIdSuffix(ClientId));
		}

		if(!WebSocket.m_pCurl)
		{
			const EConnectResult Result = ConnectWebSocket(ClientId, Token, m_InstanceId, Desired.m_RoomId, WebSocket, m_Debug.load(std::memory_order_relaxed), m_StopRequested);
			if(Result != EConnectResult::CONNECTED)
			{
				if(Result == EConnectResult::AUTH_FAILED)
				{
					ClientId.clear();
					Token.clear();
					QueueCredentialUpdate({}, {});
				}
				ScheduleReconnect();
				continue;
			}
			ConnectedRoom = Desired.m_RoomId;
			ConnectedAt = SteadyNowMs();
			NextPresenceRefreshAt = ConnectedAt + PRESENCE_REFRESH_INTERVAL_MS;
			Sequence = 0;
			SentRevision = 0;
			m_Connected.store(true, std::memory_order_relaxed);
			if(m_Debug.load(std::memory_order_relaxed))
			log_info("amf_presence", "websocket connected installation_suffix=%s room_id=%s", InstallationIdSuffix(ClientId), ConnectedRoom.c_str());
			QueueRemoteEvent({ERemoteEventType::CONNECTED});
		}

		if(SentRevision != Desired.m_Revision && !Desired.m_StateJson.empty())
		{
			QueueWebSocketFrame(WebSocket, MakeUpdateMessage(++Sequence, Desired.m_StateJson), EWebSocketFrameType::TEXT);
			SentRevision = Desired.m_Revision;
			if(m_Debug.load(std::memory_order_relaxed))
				log_info("amf_presence", "presence update queued installation_suffix=%s seq=%llu bytes=%d", InstallationIdSuffix(ClientId), (unsigned long long)Sequence, (int)Desired.m_StateJson.size());
		}

		if(SteadyNowMs() >= NextPresenceRefreshAt && !Desired.m_StateJson.empty())
		{
			QueueWebSocketFrame(WebSocket, MakeUpdateMessage(++Sequence, Desired.m_StateJson), EWebSocketFrameType::TEXT);
			if(m_Debug.load(std::memory_order_relaxed))
				log_info("amf_presence", "presence refresh queued room_id=%s installation_suffix=%s", ConnectedRoom.c_str(), InstallationIdSuffix(ClientId));
			NextPresenceRefreshAt = SteadyNowMs() + PRESENCE_REFRESH_INTERVAL_MS;
		}

		if(!PumpWebSocket(WebSocket))
			WebSocket.m_Failed = true;
		while(!WebSocket.m_ReceivedMessages.empty())
		{
			std::string ReceiveMessage = std::move(WebSocket.m_ReceivedMessages.front());
			WebSocket.m_ReceivedMessages.pop_front();
			json_value *pJson = json_parse(ReceiveMessage.data(), ReceiveMessage.size());
			SRemoteEvent Event;
			if(ParseRemoteEventPayload(pJson, Event))
			{
				if(m_Debug.load(std::memory_order_relaxed))
				{
					const char *pType = Event.m_Type == ERemoteEventType::SNAPSHOT ? "snapshot" : Event.m_Type == ERemoteEventType::DELTA ? "delta" : "offline";
					log_info("amf_presence", "remote %s installation_suffix=%s members=%d", pType, Event.m_ClientId.empty() ? "-" : InstallationIdSuffix(Event.m_ClientId), (int)Event.m_vPresence.size());
					for(const auto &Presence : Event.m_vPresence)
					{
						char aMainName[MAX_NAME_LENGTH];
						char aDummyName[MAX_NAME_LENGTH];
						log_info("amf_presence", "remote member installation_suffix=%s main={connected=%d client_id=%d name='%s' settings_open=%d fast_practice=%d} dummy={connected=%d client_id=%d name='%s' settings_open=%d fast_practice=%d}",
							InstallationIdSuffix(Presence.m_ClientId), Presence.m_Main.m_Connected, Presence.m_Main.m_GameClientId, SafePlayerName(Presence.m_Main.m_PlayerName, aMainName), Presence.m_Main.m_SettingsOpen, Presence.m_Main.m_FastPractice,
							Presence.m_Dummy.m_Connected, Presence.m_Dummy.m_GameClientId, SafePlayerName(Presence.m_Dummy.m_PlayerName, aDummyName), Presence.m_Dummy.m_SettingsOpen, Presence.m_Dummy.m_FastPractice);
					}
				}
				QueueRemoteEvent(std::move(Event));
			}
			else if(pJson && pJson->type == json_object)
			{
				const json_value *pOp = json_object_get(pJson, "op");
				if(pOp != &json_value_none && pOp->type == json_string)
				{
					if(str_comp(pOp->u.string.ptr, "ack") == 0 && BrowserSnapshotAwaitingAckSequence != 0)
					{
						const json_value *pSequence = json_object_get(pJson, "seq");
						if(pSequence != &json_value_none && pSequence->type == json_integer && static_cast<uint64_t>(pSequence->u.integer) >= BrowserSnapshotAwaitingAckSequence)
						{
							BrowserSnapshotAwaitingAckSequence = 0;
							if(Desired.m_BrowserSnapshotRequestSerial != CompletedBrowserSnapshotRequestSerial && SteadyNowMs() - LastBrowserSnapshotAt >= BROWSER_SNAPSHOT_MANUAL_COOLDOWN_MS)
							{
								RefreshBrowserSnapshot();
								CompletedBrowserSnapshotRequestSerial = Desired.m_BrowserSnapshotRequestSerial;
							}
						}
					}
					if(m_Debug.load(std::memory_order_relaxed))
					{
					if(str_comp(pOp->u.string.ptr, "session.ready") == 0)
						log_info("amf_presence", "session.ready installation_suffix=%s room_id=%s", InstallationIdSuffix(ClientId), ConnectedRoom.c_str());
					else if(str_comp(pOp->u.string.ptr, "pong") == 0)
						log_info("amf_presence", "websocket keepalive pong room_id=%s installation_suffix=%s", ConnectedRoom.c_str(), InstallationIdSuffix(ClientId));
					}
				}
			}
			if(pJson)
				json_value_free(pJson);
		}
		if(WebSocket.m_Failed)
		{
			if(m_Debug.load(std::memory_order_relaxed))
				log_info("amf_presence", "websocket disconnected error='%s' room_id=%s installation_suffix=%s",
					WebSocket.m_Error.empty() ? "unknown" : WebSocket.m_Error.c_str(), ConnectedRoom.c_str(), InstallationIdSuffix(ClientId));
			CloseWebSocket(false);
			ScheduleReconnect();
			continue;
		}

		std::unique_lock<std::mutex> Lock(m_Mutex);
		const uint64_t Revision = m_Desired.m_Revision;
		const uint64_t WakeupSerial = m_Desired.m_WakeupSerial;
		m_Condition.wait_for(Lock, WORKER_POLL_INTERVAL, [&] { return m_Stop || m_Desired.m_Revision != Revision || m_Desired.m_WakeupSerial != WakeupSerial; });
	}

	// The application is exiting. Close locally instead of waiting for an
	// optional remote offline acknowledgement; the server also observes the
	// socket close and the worker can join immediately.
	CloseWebSocket(false);
}
