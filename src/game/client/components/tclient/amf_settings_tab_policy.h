#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_TAB_POLICY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_TAB_POLICY_H

namespace AmfSettingsTabPolicy
{
enum
{
	PAGE_GENERAL = 0,
	PAGE_VISUAL = 1,
	PAGE_INFORMATION = 2,
};

// Bits 0-5 are owned by the existing TClient tabs. Reuse the same persisted
// mask for AMF's two optional tabs, leaving Information permanently visible.
constexpr int MASK_GENERAL = 1 << 6;
constexpr int MASK_VISUAL = 1 << 7;

constexpr int HiddenMaskForPage(int Page)
{
	return Page == PAGE_GENERAL ? MASK_GENERAL : Page == PAGE_VISUAL ? MASK_VISUAL : 0;
}

constexpr bool IsHidden(int Mask, int Page)
{
	const int HiddenMask = HiddenMaskForPage(Page);
	return HiddenMask != 0 && (Mask & HiddenMask) != 0;
}

constexpr int SetHidden(int Mask, int Page, bool Hidden)
{
	const int HiddenMask = HiddenMaskForPage(Page);
	if(HiddenMask == 0)
		return Mask;
	return Hidden ? Mask | HiddenMask : Mask & ~HiddenMask;
}

constexpr int ResolvePage(int Mask, int RequestedPage)
{
	if(RequestedPage < PAGE_GENERAL || RequestedPage > PAGE_INFORMATION || IsHidden(Mask, RequestedPage))
		return PAGE_INFORMATION;
	return RequestedPage;
}
} // namespace AmfSettingsTabPolicy

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_AMF_SETTINGS_TAB_POLICY_H
