#ifndef GAME_EDITOR_PROOF_MODE_H
#define GAME_EDITOR_PROOF_MODE_H

#include "component.h"

#include <base/vmath.h>

#include <vector>

class CProofMode : public CEditorComponent
{
public:
	enum class EProofBorder
	{
		OFF,
		INGAME,
		MENU,
	};
	class CState
	{
	public:
		EProofBorder m_ProofBorders;
		int m_CurrentMenuProofIndex;

		std::vector<vec2> m_vMenuBackgroundPositions;
		std::vector<std::vector<int>> m_vvMenuBackgroundCollisions;

		void Reset()
		{
			m_ProofBorders = EProofBorder::OFF;
			m_CurrentMenuProofIndex = 0;
			m_vMenuBackgroundPositions.clear();
			m_vvMenuBackgroundCollisions.clear();
		}
		template<typename TInitializePositions>
		bool EnterMenuMode(TInitializePositions InitializePositions)
		{
			InitializePositions();
			if(m_vMenuBackgroundPositions.empty())
				return false;

			if(m_CurrentMenuProofIndex < 0 || (size_t)m_CurrentMenuProofIndex >= m_vMenuBackgroundPositions.size())
				m_CurrentMenuProofIndex = 0;

			m_ProofBorders = EProofBorder::MENU;
			return true;
		}
	};

	void OnInit(CEditor *pEditor) override;
	void OnMapLoad() override;
	void RenderScreenSizes();

	bool IsEnabled() const;
	bool IsModeMenu() const;
	bool IsModeIngame() const;
	void Toggle();
	void SetModeMenu();
	void SetModeIngame();
	int CurrentMenuProofIndex() const;
	void SetCurrentMenuProofIndex(int MenuProofIndex);
	const std::vector<vec2> &MenuBackgroundPositions() const;
	vec2 CurrentMenuBackgroundPosition() const;
	const char *MenuBackgroundPositionName(int MenuProofIndex) const;
	const std::vector<int> &MenuBackgroundCollisions(int MenuProofIndex) const;
	void InitMenuBackgroundPositions();

private:
	std::vector<const char *> m_vpMenuBackgroundPositionNames;

	void InitMenuBackgroundPositionNames();
};

#endif
