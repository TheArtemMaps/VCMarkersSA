#include "plugin.h"
#include "CTxdStore.h"
#include "CCamera.h"
#include "CCoronas.h"
#include "MemoryMgr.h"
#include "CVisibilityPlugins.h"
#include "CFileMgr.h"
#include "CFileLoader.h"
#include "C3dMarkers.h"
#include <game_sa/CStreaming.h>
#include <game_sa/CModelInfo.h>
#include "CZone.h"
#include <CSprite2d.h>
#include <game_sa/CWorld.h>
using namespace plugin;
// Events for rendering markers (movingThingsEvent works, but it should be RenderSpecialFxEvent, but it doesn't work???)
CdeclEvent    <AddressList<0x726AD0, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> RenderSpecialFxEvent;
ThiscallEvent <AddressList<0x53E175, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> RenderEffectsEvent;
CdeclEvent    <AddressList<0x717150, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> movingThingsEvent;
CdeclEvent    <AddressList<0x53DF40, H_CALL>, PRIORITY_BEFORE, ArgPickNone, void()> RenderSceneEvent;
// Some render ware stuff
#define RpGeometryGetMaterialMacro(_geometry, _num)             \
    (((_geometry)->matList.materials)[(_num)])
#define RpGeometryGetMaterial(_geometry, _num)                  \
    RpGeometryGetMaterialMacro(_geometry, _num)
#define RpGeometrySetFlagsMacro(_geometry, _flags)              \
    (((_geometry)->flags = (_flags)), (_geometry))
#define RpGeometrySetFlags(_geometry, _flags)                   \
    RpGeometrySetFlagsMacro(_geometry, _flags)
#define RpAtomicGetFrameMacro(_atomic)                                  \
    ((RwFrame *) rwObjectGetParent(_atomic))
#define RpAtomicGetFrame(_atomic) \
    RpAtomicGetFrameMacro(_atomic)
#define RpClumpGetFrameMacro(_clump)                                    \
    ((RwFrame *) rwObjectGetParent(_clump))
#define RpClumpGetFrame(_clump) \
    RpClumpGetFrameMacro(_clump)
/* NB "RpAtomicRender(atom++) will break it */
#define RpAtomicRenderMacro(_atomic)                                    \
    ((_atomic)->renderCallBack(_atomic))
#define RpAtomicRender(_atomic) \
    RpAtomicRenderMacro(_atomic)
void
PushRendergroup(const char* name)
{
	static WCHAR tmp[256];
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, name, -1, tmp, sizeof(tmp));
	D3DPERF_BeginEvent(0xFFFFFFFF, tmp);

}

void
PopRendergroup(void)
{
	D3DPERF_EndEvent();
}

#define PUSH_RENDERGROUP(str) PushRendergroup(str)
#define POP_RENDERGROUP() PopRendergroup()
// Sphere colors and pulse settings
// Blue
#define SPHERE_MARKER_R (0)
#define SPHERE_MARKER_G (128)
#define SPHERE_MARKER_B (255)
#define SPHERE_MARKER_A (128)
// Pink
#define MARKER_SET_COLOR_R	0xFC
#define MARKER_SET_COLOR_G	0x8A
#define MARKER_SET_COLOR_B	0xF2
#define MARKER_SET_COLOR_A	0xE4
#define SPHERE_MARKER_PULSE_PERIOD (2048)
#define SPHERE_MARKER_PULSE_FRACTION (0.1f)
// math stuff
#define PI (float)M_PI
#define TWOPI (PI*2)
inline float sq(float x) { return x * x; }
// Settings stuff
bool ReplaceEntranceMarkers = false;
bool MagentaMarkers = false;

class VCMarker : public C3dMarker
{

public:
	long double		CalculateRealSize();
	void			Render(void);
};

class VCMarkers : public C3dMarkers
{
private:
	static float				m_PosZMult;
	static const float			m_MovingMultiplier;

public:
	static inline float* GetPosZMult()
	{
		return &m_PosZMult;
	};
	static inline const float* GetMovingMult()
	{
		return &m_MovingMultiplier;
	};

	static void					Init(void);
	static void					Render(void);
	static void					Update(void);
	static void					Shutdown(void);
	// Last unused param removed
	static void					PlaceMarkerSet(unsigned int nIndex, unsigned short markerID, CVector& vecPos, float fSize, unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha, unsigned short pulsePeriod, float pulseFraction);
	static void                 PlaceMarkerCone(int id, CVector& posn, float size, char r, char g, char b, int alpha, __int16 pulsePeriod, float pulseFraction, int type, char bEnableCollision);
	VALIDATE_SIZE(VCMarker, 0xA0);
};

class RGBA : public CRGBA
{
public:
	RwRGBA ToRwRGBA() const;
};

float		VCMarkers::m_PosZMult;
const float	VCMarkers::m_MovingMultiplier = 0.40f;

void
VCMarkers::Init(void)
{
	int i = {};
	C3dMarker* marker;
	marker = m_aMarkerArray;
	// Init array
	for (i = 0; i < MAX_NUM_3DMARKERS; i++) {
		marker->m_vecLastPosition = CVector(0, 0, 0);
		marker->m_pAtomic = nullptr;
		marker->m_nType = MARKER3D_NA;
		marker->m_bIsUsed = false;
		marker->m_nIdentifier = 0;
		marker->m_colour.r = 255;
		marker->m_colour.g = 255;
		marker->m_colour.b = 255;
		marker->m_colour.a = 255;
		marker->m_nPulsePeriod = 1024;
		marker->m_nRotateRate = 5;
		marker->m_nStartTime = 0;
		marker->m_fPulseFraction = 0.25f;
		marker->m_fStdSize = 1.0f;
		marker->m_fSize = 1.0f;
		marker->m_fBrightness = 1.0f;
		marker->m_fCameraRange = 0.0f;
		marker->m_vecNormal = CVector(0, 0, 1);
		marker->m_nLastMapReadX = 30000;
		marker->m_fRoofHeight = 65535.0f;
		marker++;
	}
	ReplaceEntranceMarkers = (bool)GetPrivateProfileIntA("MAIN", "ReplaceEntranceMarkers", ReplaceEntranceMarkers, PLUGIN_PATH((char*)"VCMarkers.ini"));
	MagentaMarkers = (bool)GetPrivateProfileIntA("MAIN", "MagentaMarkers", MagentaMarkers, PLUGIN_PATH((char*)"VCMarkers.ini"));
	memset(m_pRpClumpArray, 0, sizeof(RpClump * [7]));
	NumActiveMarkers = 0;
	m_angleDiamond = 0.0f;
	// Loading marker models & textures
	CTxdStore::PushCurrentTxd();
	CTxdStore::SetCurrentTxd(CTxdStore::FindTxdSlot("particle"));
	CFileMgr::ChangeDir("\\");
	//LoadUser3dMarkers();
	m_pRpClumpArray[MARKER3D_CYLINDER] = LoadMarker("cylinder");
	m_pRpClumpArray[MARKER3D_TORUS] = LoadMarker("hoop");
	m_pRpClumpArray[MARKER3D_ARROW] = LoadMarker("diamond_3");
	m_pRpClumpArray[MARKER3D_CONE_NO_COLLISION] = m_pRpClumpArray[MARKER3D_ARROW];
	m_pRpClumpArray[MARKER3D_CONE] = LoadMarker("arrow");
	//CFileLoader::LoadAtomicFile2Return("models/generic/arrow.dff"); <-- crashes for some reason when starting a new game
	CTxdStore::PopCurrentTxd();
}

void
VCMarkers::Render(void)
{
	int alphafunc;
	C3dMarker* marker;
	//static RwRGBAReal markerAmbient = { 0.0, 0.0, 0.0, 0.0 };
	//static RwRGBAReal markerDirectional = { 0.0, 0.0, 0.0, 0.0 };
	static RwRGBAReal& ambient = *(RwRGBAReal*)0xC80444; // STATICREF
	static RwRGBAReal& directional = *(RwRGBAReal*)0xC80434; // STATICREF
	RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &alphafunc);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);
	NumActiveMarkers = 0;
	ActivateDirectional();
	SetAmbientColours(&ambient);
	SetDirectionalColours(&directional);
	User3dMarkersDraw();
	for (marker = m_aMarkerArray; marker < &m_aMarkerArray[MAX_NUM_3DMARKERS]; marker++) {
		CVector pos = marker->m_mat.GetPosition();
		if (marker->m_bIsUsed) {
			if (marker->m_fCameraRange < 150.0f || IgnoreRenderLimit || marker->m_nType == MARKER3D_TORUS) {
				reinterpret_cast<VCMarker*>(marker)->Render();
				if (marker->m_nType == MARKER3D_CYLINDER) {
					marker->m_mat.RotateZ(DEGTORAD(marker->m_nRotateRate * CTimer::ms_fTimeStep));
					marker->m_mat.GetPosition() = pos;
				}
				if (ReplaceEntranceMarkers) {
					if (marker->m_nType == MARKER3D_CONE) { // For entry/exit markers
						marker->m_mat.RotateZ(DEGTORAD(PI * CTimer::ms_fTimeStep));
						marker->m_mat.GetPosition() = pos;
					}
				}

				if (marker->m_nType == MARKER3D_CONE) {
					if (marker->m_nRotateRate != 0) {
						marker->m_mat.RotateZ(DEGTORAD(marker->m_nRotateRate * CTimer::ms_fTimeStep));
						marker->m_mat.GetPosition() = pos;
					}
				}
				
				if (marker->m_nType == MARKER3D_CONE) {
					CCoronas::RegisterCorona(reinterpret_cast<unsigned int>(marker) + 50 + 6 + (ReplaceEntranceMarkers ? 0 : 2), nullptr,
						marker->m_colour.r, marker->m_colour.g, marker->m_colour.b, 192,
						marker->m_mat.GetPosition(), 1.2f * marker->m_fSize, TheCamera.m_fLODDistMultiplier * 50.0f,
						CORONATYPE_SHINYSTAR, FLARETYPE_NONE, true, false, 1, 0.0f, false, 1.5f, false, 255.0f, false, true);

				}
				NumActiveMarkers++;
				marker->m_bIsUsed = false;
			}
			else if (marker->m_pAtomic != NULL) {
				marker->DeleteMarkerObject();
			}
		}
		
		DirectionArrowsDraw();
		RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)alphafunc);
	}
}

void VCMarkers::PlaceMarkerSet(unsigned int nIndex, unsigned short markerID, CVector& vecPos, float fSize, unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha, unsigned short pulsePeriod, float pulseFraction)
{
	uint8_t r, g, b, a;
	if (MagentaMarkers)
		r = MARKER_SET_COLOR_R, g = MARKER_SET_COLOR_G, b = MARKER_SET_COLOR_B, a = MARKER_SET_COLOR_A;
	else
		r = red, g = green, b = blue, a = alpha;
	//PlaceMarker(nIndex, markerID, vecPos, fSize, red, green, blue, static_cast<unsigned char>(alpha * (1.0f / 3.0f)), pulsePeriod, pulseFraction, 1, 0.0, 0.0, 0.0, false);
	//PlaceMarker(nIndex, markerID, vecPos, fSize * 0.9f, red, green, blue, static_cast<unsigned char>(alpha * (1.0f / 3.0f)), pulsePeriod, pulseFraction, -1, 0.0, 0.0, 0.0, false);
	PlaceMarker(nIndex, markerID, vecPos, fSize, r, g, b, a, pulsePeriod, pulseFraction, 1, 0.0f, 0.0f, 0.0f, false);
	//PlaceMarker(nIndex, markerID, vecPos, fSize, red, green, blue, alpha, pulsePeriod, pulseFraction, 1, 0.0f, 0.0f, 0.0f, false);
	//PlaceMarker(nIndex, markerID, vecPos, fSize * 0.93f, red, green, blue, alpha, pulsePeriod, pulseFraction, 2, 0.0f, 0.0f, 0.0f, false);
	//PlaceMarker(nIndex, markerID, vecPos, fSize * 0.86f, red, green, blue, alpha, pulsePeriod, pulseFraction, -1, 0.0f, 0.0f, 0.0f, false);
}

void __cdecl VCMarkers::PlaceMarkerCone(int id, CVector& posn, float size, char r, char g, char b, int alpha, __int16 pulsePeriod, float pulseFraction, int type, char bEnableCollision) {
	auto markertype = bEnableCollision ? MARKER3D_CONE : MARKER3D_CONE_NO_COLLISION;
	if (ReplaceEntranceMarkers)
		markertype = MARKER3D_CONE;
	else
		markertype = bEnableCollision ? MARKER3D_CONE : MARKER3D_CONE_NO_COLLISION;

	if ((posn - TheCamera.GetPosition()).Magnitude() >= sq(1.6f)) {
		PlaceMarker(id, markertype, posn, size, r, g, b, m_colDiamond, pulsePeriod, pulseFraction, 0, 0.0f, 0.0f, 0.0f, false);
	}
}

RwRGBA RGBA::ToRwRGBA() const {
	return { r, g, b, a };
}

void
VCMarker::Render(void)
{
	//PUSH_RENDERGROUP("VCMarker::Render");
	if (m_pAtomic == nullptr)
		return;
	if (m_nType == MARKER3D_CYLINDER)
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	else
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
	RwFrameUpdateObjects(RpAtomicGetFrame(m_pAtomic));
	CRGBA color2 = m_colour;
	if (MagentaMarkers)
		color2.r = MARKER_SET_COLOR_R, color2.g = MARKER_SET_COLOR_G, color2.b = MARKER_SET_COLOR_B, color2.a = MARKER_SET_COLOR_A;
	else
		color2.r = m_colour.r, color2.g = m_colour.g, color2.b = m_colour.b, color2.a = m_colour.a;
	switch (m_nType) {
	case MARKER3D_CONE:
		m_colour.r = color2.r;
		m_colour.g = color2.g;
		m_colour.b = color2.b;
		m_colour.a = color2.a;
		break;
	}
	const auto color = reinterpret_cast<RGBA&>(m_colour).ToRwRGBA();
	RpMaterialSetColor(m_pMaterial, &color);
	m_mat.UpdateRW();
	CMatrix mat;
	mat.Attach(m_mat.m_pAttachMatrix, false);
	mat.Scale(m_fSize);
	mat.UpdateRW();
	RwFrameUpdateObjects(RpClumpGetFrame(m_pAtomic));
	SetBrightMarkerColours(m_fBrightness);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	if (m_nType == MARKER3D_ARROW2)
		RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RpAtomicRender(m_pAtomic);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
	ReSetAmbientAndDirectionalColours();
	//POP_RENDERGROUP();
}
long double VCMarker::CalculateRealSize()
{
	long double		fVariable = (((m_nPulsePeriod - 1)) & (CTimer::m_snTimeInMilliseconds - m_nStartTime));
	return (2 * M_PI) * fVariable / static_cast<long double>(m_nPulsePeriod);
}
void __declspec(naked) C3dMarkerSizeHack()
{
	_asm
	{
		push	eax		// Not keeping it causes marker to flicker
		mov		ecx, esi
		call	VCMarker::CalculateRealSize
		fst		VCMarkers::m_PosZMult
		pop		eax
		retn
	}
}

void __declspec(naked) EnexMarkersColorBreak()
{
	_asm
	{
		push	96h
		push	64h
		push	ebx
		//push	00h
		mov		eax, 440F43h
		jmp		eax
	}
}

class VCMarkersSA {
public:
	VCMarkersSA() {
		// Добавьте здесь код инициализации плагина
		Events::initRwEvent += []() {
			using namespace Memory;
			//VCMarkers::Init();

			InjectHook(0x7269FA, VCMarkers::Init);
			InjectHook(0x7250B1, &VCMarker::Render);
			InjectHook(0x726AE4, VCMarkers::Render);
			//InjectHook(0x722710, VCMarkers::Shutdown, PATCH_JUMP);
			//InjectHook(0x7227B0, VCMarkers::Update);
		//*(uint8_t*)0x8D5D8B = 0xD1;	// cone marker alpha
			// Spheres colours
			//dwFunc = 0x4810E0 + 0x2B;
			//	patch(dwFunc, MARKER_SET_COLOR_A, 4);
			//	dwFunc += 0x6;
			//Patch<BYTE>(0x4810E0 + 0x2B, SPHERE_MARKER_B);
			//dwFunc += 0x2;
			//Patch<BYTE>(0x4810E0 + 0x2B + 0x2, SPHERE_MARKER_G);
			//dwFunc += 0x2;
			//Patch<BYTE>(0x4810E0 + 0x2B + 0x4, SPHERE_MARKER_R);

			// Growing/shrinking 3DMarkers
			Patch<float>(0x440F26, 0.0f);
			InjectHook(0x72576B, &C3dMarkerSizeHack, PATCH_CALL);
			Nop(0x725770, 1);

			// New style of markers
			// What is this?
			InjectHook(0x725BA0, &VCMarkers::PlaceMarkerSet, PATCH_JUMP);
			InjectHook(0x440F4E, &VCMarkers::PlaceMarkerCone);

			// Enex markers RGB
			//InjectHook(0x440F38, EnexMarkersColorBreak, PATCH_JUMP);

			// arrow.dff as marker
			Patch<const float*>(0x725636, VCMarkers::GetPosZMult());
			Patch<const float*>(0x7259A1, VCMarkers::GetPosZMult());
			Patch<const float*>(0x72564B, VCMarkers::GetMovingMult());
			Patch<const float*>(0x7259A9, VCMarkers::GetMovingMult());
			Nop(0x72563A, 6);
			Nop(0x72599F, 6);
			Nop(0x72502B, 6);
			Nop(0x725647, 2);
			Patch<uint8_t>(0x726DA6, 5);	// arrow (old cone) rotate rate
			Patch(0x7232C1, &C3dMarkers::m_pRpClumpArray[0]);	// marker 0 (user marker)*/
		};
		Events::reInitGameEvent += []() { // To reload ini file by loading your save file
			ReplaceEntranceMarkers = (bool)GetPrivateProfileIntA("MAIN", "ReplaceEntranceMarkers", ReplaceEntranceMarkers, PLUGIN_PATH((char*)"VCMarkers.ini"));
			MagentaMarkers = (bool)GetPrivateProfileIntA("MAIN", "MagentaMarkers", MagentaMarkers, PLUGIN_PATH((char*)"VCMarkers.ini"));
		};
	}
} vCMarkersSA;
