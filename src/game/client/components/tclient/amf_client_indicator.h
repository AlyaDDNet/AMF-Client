#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AMF_CLIENT_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AMF_CLIENT_INDICATOR_H

#include <game/client/component.h>
#include <engine/graphics.h>
#include <engine/shared/protocol.h>

class CUIRect;

class CAmfClientIndicator : public CComponent
{
public:
	CAmfClientIndicator();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;

	bool IsPlayerAmf(int ClientId) const;
	IGraphics::CTextureHandle Texture() const { return m_Texture; }
	bool CanRender(int ClientId) const;
	bool CanRenderInWorld(int ClientId) const;
	bool LayoutName(const CUIRect &Available, int ClientId, float FontSize, float NameWidth, CUIRect &NameRect, CUIRect &IconRect, bool ForceRight = false) const;
	void RenderIcon(const CUIRect &Rect, float Alpha = 1.0f) const;

private:
	IGraphics::CTextureHandle m_Texture;
	mutable bool m_aDebugRenderedWorld[MAX_CLIENTS];
};

#endif
