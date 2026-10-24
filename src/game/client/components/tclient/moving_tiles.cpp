#include "moving_tiles.h"

#include <base/color.h>
#include <base/dbg.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/envelope_state.h>
#include <game/client/gameclient.h>
#include <game/map/render_map.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <iterator>

inline static bool QuadName(const int *pInts, size_t NumInts, char *pStr, size_t StrSize)
{
	dbg_assert(NumInts > 0, "QuadName: NumInts invalid");
	dbg_assert(StrSize >= NumInts * sizeof(int), "QuadName: StrSize invalid");

	// Unpack string without validation
	size_t StrIndex = 0;
	for(size_t IntIndex = 0; IntIndex < NumInts; IntIndex++)
	{
		const int CurrentInt = pInts[IntIndex];
		pStr[StrIndex] = ((CurrentInt >> 24) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 16) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 8) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = (CurrentInt & 0xff) - 128;
		StrIndex++;
	}
	// Ensure null-termination
	pStr[StrIndex - 1] = '\0';

	// Ensure valid UTF-8
	if(str_utf8_check(pStr))
	{
		return true;
	}
	pStr[0] = '\0';
	return false;
}

void CMovingTiles::Reset()
{
	m_vQuads.clear();
}

void CMovingTiles::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState && NewState != IClient::STATE_ONLINE)
		Reset();
}

void CMovingTiles::OnMapLoad()
{
	Reset();

	int GroupsStart, LayersStart, GroupsNum, LayersNum;

	GameClient()->Map()->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	GameClient()->Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	// size_t NumQuadLayers = 0;
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(GameClient()->Map()->GetItem(GroupsStart + GroupIndex));
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(GameClient()->Map()->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));

			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			char aLayerName[30];
			CMapItemLayerQuads *pTilemap = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			QuadName(pTilemap->m_aName, std::size(pTilemap->m_aName), aLayerName, std::size(aLayerName));

			for(size_t Names = 0; Names < std::size(ValidQuadNames); Names++)
			{
				if(!str_comp(ValidQuadNames[Names], aLayerName))
				{
					EQType Type = static_cast<EQType>(Names);
					if(!m_RenderAbove && (Type == EQType::HOOKABLE || Type == EQType::UNHOOKABLE))
						continue;
					else if(m_RenderAbove && Type != EQType::HOOKABLE && Type != EQType::UNHOOKABLE)
						continue;

					// NumQuadLayers++;
					CQuad *pQuads = (CQuad *)GameClient()->Map()->GetDataSwapped(pTilemap->m_Data);
					for(int NumQuads = 0; NumQuads < pTilemap->m_NumQuads; NumQuads++)
					{
						CQuadData QuadData;
						QuadData.m_pQuad = &pQuads[NumQuads];
						QuadData.m_pGroup = pGroup;
						QuadData.m_pLayer = pTilemap;
						QuadData.m_Type = Type;
						for(int j = 0; j < 5; j++)
							QuadData.m_Pos[j] = vec2(fx2f(QuadData.m_pQuad->m_aPoints[j].x), fx2f(QuadData.m_pQuad->m_aPoints[j].y));
						m_vQuads.push_back(QuadData);
					}
					break;
				}
			}
		}
	}
	// log_info("moving-tiles", "%" PRIzu " valid quadlayer%s with %d quads", NumQuadLayers, NumQuadLayers == 1 ? "" : "s", (int)m_vQuads.size());
	m_EnvEvaluator = CEnvelopeState(GameClient()->Map(), true);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
}

void CMovingTiles::AccumulatePracticeCollision(int PracticeGameTick, vec2 Position, bool ResolveCurrentPosition, MovingTilesCollision::SOverride &Override) const
{
	// EnvelopeEval(0) follows the interpolated render clock. Practice collision,
	// however, is evaluated while a private world is advancing one discrete tick
	// at a time. The character position passed to HandleTiles belongs to the
	// state before PracticeGameTick's deferred move, so evaluate the quad at the
	// matching start-of-tick envelope time (PracticeGameTick - 1).
	const int PredictionTick = Client()->PredGameTick(g_Config.m_ClDummy);
	const double PredictionIntra = Client()->PredIntraGameTick(g_Config.m_ClDummy);
	const int TickSpeed = Client()->GameTickSpeed();
	const double RenderEnvelopeTick = (double)PredictionTick - 1.0 + PredictionIntra;
	const double PracticeEnvelopeTick = (double)PracticeGameTick - 1.0;
	const int EnvelopeTimeOffsetMillis = TickSpeed > 0 ? (int)std::round((PracticeEnvelopeTick - RenderEnvelopeTick) * 1000.0 / (double)TickSpeed) : 0;

	for(const CQuadData &QuadData : m_vQuads)
	{
		if(!QuadData.m_pQuad || (QuadData.m_Type != EQType::FREEZE && QuadData.m_Type != EQType::UNFREEZE))
			continue;

		const CQuad *pQuad = QuadData.m_pQuad;
		std::array<vec2, 4> aInitial;
		for(size_t Point = 0; Point < aInitial.size(); Point++)
			aInitial[Point] = QuadData.m_Pos[Point];

		ColorRGBA PositionEnvelope(0.0f, 0.0f, 0.0f, 0.0f);
		m_EnvEvaluator.EnvelopeEval(EnvelopeTimeOffsetMillis, pQuad->m_PosEnv, PositionEnvelope, 3);
		const vec2 Offset(PositionEnvelope.r, PositionEnvelope.g);
		const float Rotation = PositionEnvelope.b / 180.0f * pi + QuadData.m_Angle;
		const std::array<vec2, 4> aCurrent = MovingTilesCollision::TransformQuad(aInitial, QuadData.m_Pos[4], Offset, Rotation);

		const int Tile = QuadData.m_Type == EQType::FREEZE ? TILE_FREEZE : TILE_UNFREEZE;
		MovingTilesCollision::Accumulate(Tile, aInitial, aCurrent, Position, ResolveCurrentPosition, Override);
	}
}

void CMovingTiles::OnRender()
{
	if(g_Config.m_ClOverlayEntities != 100)
		return;
	if(g_Config.m_AmfMovingTilesMode != AMF_MOVING_TILES_TCLIENT)
		return;
	if(m_vQuads.empty())
		return;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;

	auto ApplyGroupState = [&](const CMapItemGroup *pGroup) -> bool {
		Graphics()->ClipDisable();

		if(!pGroup)
			return false;

		if(!g_Config.m_GfxNoclip && pGroup->m_Version >= 2 && pGroup->m_UseClipping)
		{
			Graphics()->MapScreenToInterface(Center.x, Center.y, Zoom);

			float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
			Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

			const float ScreenWidth = ScreenX1 - ScreenX0;
			const float ScreenHeight = ScreenY1 - ScreenY0;
			const float Left = pGroup->m_ClipX - ScreenX0;
			const float Top = pGroup->m_ClipY - ScreenY0;
			const float Right = pGroup->m_ClipX + pGroup->m_ClipW - ScreenX0;
			const float Bottom = pGroup->m_ClipY + pGroup->m_ClipH - ScreenY0;

			if(Right < 0.0f || Left > ScreenWidth || Bottom < 0.0f || Top > ScreenHeight)
				return false;

			const int ClipX = (int)std::round(Left * Graphics()->ScreenWidth() / ScreenWidth);
			const int ClipY = (int)std::round(Top * Graphics()->ScreenHeight() / ScreenHeight);

			Graphics()->ClipEnable(
				ClipX,
				ClipY,
				(int)std::round(Right * Graphics()->ScreenWidth() / ScreenWidth) - ClipX,
				(int)std::round(Bottom * Graphics()->ScreenHeight() / ScreenHeight) - ClipY);
		}

		const int ParallaxZoom = std::clamp(std::max(pGroup->m_ParallaxX, pGroup->m_ParallaxY), 0, 100);
		float aScreen[4];
		Graphics()->MapScreenToWorld(Center.x, Center.y,
			pGroup->m_ParallaxX, pGroup->m_ParallaxY, (float)ParallaxZoom,
			pGroup->m_OffsetX, pGroup->m_OffsetY,
			Graphics()->ScreenAspect(), Zoom, aScreen);
		Graphics()->MapScreen(aScreen[0], aScreen[1], aScreen[2], aScreen[3]);

		return true;
	};

	auto RenderPass = [&]() {
		constexpr float ColorConv = 1.0f / 255.0f;

		size_t QuadStart = 0;
		while(QuadStart < m_vQuads.size())
		{
			const CMapItemGroup *pGroup = m_vQuads[QuadStart].m_pGroup;
			const CMapItemLayerQuads *pLayer = m_vQuads[QuadStart].m_pLayer;
			if(!pLayer)
			{
				QuadStart++;
				continue;
			}

			size_t QuadEnd = QuadStart + 1;
			while(QuadEnd < m_vQuads.size() &&
				m_vQuads[QuadEnd].m_pGroup == pGroup &&
				m_vQuads[QuadEnd].m_pLayer == pLayer)
			{
				QuadEnd++;
			}

			if(!ApplyGroupState(pGroup))
			{
				QuadStart = QuadEnd;
				continue;
			}

			if(pLayer->m_Image >= 0 && pLayer->m_Image < GameClient()->m_MapImages.Num())
				Graphics()->TextureSet(GameClient()->m_MapImages.Get(pLayer->m_Image));
			else
				Graphics()->TextureClear();

			Graphics()->TrianglesBegin();

			for(size_t QuadIndex = QuadStart; QuadIndex < QuadEnd; QuadIndex++)
			{
				const CQuadData &QuadData = m_vQuads[QuadIndex];
				const CQuad *pQuad = QuadData.m_pQuad;
				if(!pQuad)
					continue;

				ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_ColorEnvOffset, pQuad->m_ColorEnv, Color, 4);
				if(Color.a <= 0.0f)
					continue;

				Graphics()->QuadsSetSubsetFree(
					fx2f(pQuad->m_aTexcoords[0].x), fx2f(pQuad->m_aTexcoords[0].y),
					fx2f(pQuad->m_aTexcoords[1].x), fx2f(pQuad->m_aTexcoords[1].y),
					fx2f(pQuad->m_aTexcoords[2].x), fx2f(pQuad->m_aTexcoords[2].y),
					fx2f(pQuad->m_aTexcoords[3].x), fx2f(pQuad->m_aTexcoords[3].y));

				ColorRGBA Position(0.0f, 0.0f, 0.0f, 0.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_PosEnvOffset, pQuad->m_PosEnv, Position, 3);

				const vec2 Offset(Position.r, Position.g);
				const float Rotation = Position.b / 180.0f * pi + QuadData.m_Angle;

				auto QuadColor = [&](int Index) {
					return ColorRGBA(
						pQuad->m_aColors[Index].r * ColorConv * Color.r,
						pQuad->m_aColors[Index].g * ColorConv * Color.g,
						pQuad->m_aColors[Index].b * ColorConv * Color.b,
						pQuad->m_aColors[Index].a * ColorConv * Color.a);
				};
				// DDNet 20.1 exposes per-corner colors through SetColor4. Its argument
				// order differs from the map's TL, TR, BL, BR point storage order.
				Graphics()->SetColor4(QuadColor(0), QuadColor(1), QuadColor(3), QuadColor(2));

				const std::array<vec2, 4> aLocalPoints = {{
					QuadData.m_Pos[0],
					QuadData.m_Pos[1],
					QuadData.m_Pos[2],
					QuadData.m_Pos[3],
				}};
				const std::array<vec2, 4> aPoints = MovingTilesCollision::TransformQuad(aLocalPoints, QuadData.m_Pos[4], Offset, Rotation);

				const IGraphics::CFreeformItem Freeform(
					aPoints[0],
					aPoints[1],
					aPoints[2],
					aPoints[3]);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}

			Graphics()->TrianglesEnd();
			QuadStart = QuadEnd;
		}
	};

	Graphics()->BlendNormal();
	RenderPass();

	Graphics()->ClipDisable();
}
