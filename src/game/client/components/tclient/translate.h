#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATE_H

#include <game/client/component.h>
#include <game/client/components/chat.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class CTranslate;

class ITranslateBackend
{
public:
	explicit ITranslateBackend(const char *pName) :
		m_pName(pName) {}
	virtual ~ITranslateBackend() = default;
	virtual const char *EncodeTarget(const char *pTarget) const;
	virtual bool CompareTargets(const char *pA, const char *pB) const;
	const char *Name() const { return m_pName; }
	virtual std::optional<bool> Update(CTranslateResponse &Out) = 0;

private:
	const char *m_pName;
};

struct STranslateLanguage
{
	const char *m_pName;
	const char *m_pCode;
};

struct STranslateBackendInfo
{
	const char *m_pValue;
	const char *m_pName;
	std::vector<STranslateLanguage> m_vLanguages;
	bool m_NeedsEndpointConfig;
};

extern const std::array<STranslateBackendInfo, 2> g_aTranslateBackends;
int TranslateLanguageIndex(const std::vector<STranslateLanguage> &vLanguages, const char *pCode);

class CTranslate : public CComponent
{
	class CTranslateJob
	{
	public:
		std::unique_ptr<ITranslateBackend> m_pBackend = nullptr;
		// For chat translations
		CChat::CLine *m_pLine = nullptr;
		std::shared_ptr<CTranslateResponse> m_pTranslateResponse = nullptr;
		int64_t m_Deadline = 0;
	};
	std::vector<CTranslateJob> m_vJobs;
	int64_t m_NextRequestTime = 0;

	bool CanStartRequest() const;

	static void ConTranslate(IConsole::IResult *pResult, void *pUserData);
	static void ConTranslateId(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnRender() override;

	void Translate(int Id, bool ShowProgress = true);
	void Translate(const char *pName, bool ShowProgress = true);
	void Translate(CChat::CLine &Line, bool ShowProgress = true);

	void AutoTranslate(CChat::CLine &Line);
};

#endif
