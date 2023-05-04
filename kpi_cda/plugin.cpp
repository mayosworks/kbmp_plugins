//=============================================================================
// Plugin interface implementation.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "memapi.h"
#include "cd_ctrl.h"
#include "kmp_pi.h"

//-----------------------------------------------------------------------------
// 定義
//-----------------------------------------------------------------------------
namespace {

// KMPプラグインバージョン
const DWORD KMP_PLUGIN_VERSION = 100;

// 再生時コンテキスト
struct Context
{
	CDCtrl	cd_ctrl;	// CD制御
	UINT	std_sec;	// 開始セクタ
	UINT	end_sec;	// 終了セクタ
	UINT	cur_sec;	// 現在のセクタ
};

// グローバルオブジェクト

// 再生中かどうか？（２重処理防止のために使う）
bool		g_playing = false;

} //namespace


//-----------------------------------------------------------------------------
// Dll Entry Point
//-----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD call_reason, void* /*reserved*/)
{
	// スレッドのアタッチは通知不要
	if (call_reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(instance);
	}

	return TRUE;
}

//-----------------------------------------------------------------------------
// ファイルを開く
//-----------------------------------------------------------------------------
static HKMP WINAPI Open(const char* path, SOUNDINFO* info)
{
	if (lstrlenA(path) < 14) {
		return NULL;
	}

	Context* cxt = new Context();
	if (!cxt) {
		return NULL;
	}

	if (!cxt->cd_ctrl.OpenDevice(path)) {
		delete cxt;
		return NULL;
	}

	if (!cxt->cd_ctrl.IsMediaLoaded()) {
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	TOC toc;
	if (!cxt->cd_ctrl.ReadTOC(toc)) {
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	// トラックが範囲内かどうかを調べる
	// パスは、「Q:\Track01.cda」という形なので
	int track = (path[8] - L'0') * 10 + (path[9] - L'0') - 1;
	if (track < 0 || track >= toc.end_track_no) {
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	// データトラックなら再生しない
	if (toc.track_list[track].track_type & 0x04) {
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	if (!cxt->cd_ctrl.LockMedia(true)) {
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	if (!cxt->cd_ctrl.InitCDDA(1)) 	{
		cxt->cd_ctrl.LockMedia(false);
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
		return NULL;
	}

	cxt->std_sec = toc.track_list[track    ].std_sector;
	cxt->end_sec = toc.track_list[track + 1].std_sector - 1;
	cxt->cur_sec = cxt->std_sec;

	info->dwSeekable		= 1;
	info->dwLength			= MulDiv(cxt->end_sec - cxt->std_sec + 1, CDDA_SECT_SIZE * 1000, 176400);

	info->dwSamplesPerSec	= 44100;
	info->dwBitsPerSample	= 16;
	info->dwChannels		= 2;
	info->dwUnitRender		= CDDA_SECT_SIZE;

	g_playing = true;
	return cxt;
}

//-----------------------------------------------------------------------------
// ファイルを閉じる
//-----------------------------------------------------------------------------
static void WINAPI Close(HKMP hkmp)
{
	Context* cxt = static_cast<Context*>(hkmp);
	if (cxt) {
		cxt->cd_ctrl.TermCDDA();
		cxt->cd_ctrl.LockMedia(false);
		cxt->cd_ctrl.CloseDevice();
		delete cxt;
	}

	g_playing = false;
}

//-----------------------------------------------------------------------------
// レンダリング
//-----------------------------------------------------------------------------
static DWORD WINAPI Render(HKMP hkmp, BYTE* buffer, DWORD size)
{
	Context* cxt = static_cast<Context*>(hkmp);
	if (!cxt) {
		return 0;
	}

	if (cxt->cur_sec > cxt->end_sec) {
		return 0;
	}

	if (!cxt->cd_ctrl.ReadCDDA(cxt->cur_sec, 1, buffer, size)) {
		return 0;
	}

	++cxt->cur_sec;
	return size;
}

//-----------------------------------------------------------------------------
// シーク
//-----------------------------------------------------------------------------
static DWORD WINAPI SetPosition(HKMP hkmp, DWORD time_ms)
{
	Context* cxt = static_cast<Context*>(hkmp);
	if (!cxt) {
		return 0;
	}

	cxt->cur_sec = cxt->std_sec + MulDiv(time_ms, 176400, CDDA_SECT_SIZE * 1000);
	return MulDiv(cxt->cur_sec - cxt->std_sec, CDDA_SECT_SIZE * 1000, 176400);
}

//-----------------------------------------------------------------------------
// KbMPプラグイン、メインAPI
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ KMPMODULE* WINAPI kmp_GetTestModule(void)
{
	static const char* ext_list[] = {".cda", NULL};

	static KMPMODULE s_kpi;

	s_kpi.dwVersion			= KMPMODULE_VERSION;
	s_kpi.dwPluginVersion	= KMP_PLUGIN_VERSION;
	s_kpi.pszCopyright		= "Copyright (c) 2015-2016 MAYO.";
	s_kpi.pszDescription	= "Audio CD plugin v1.02";
	s_kpi.ppszSupportExts	= ext_list;
	s_kpi.dwReentrant		= 1;	// リエントラントにして、このプラグインで失敗させる（デバイスへのアクセスは１つのみなので）

	s_kpi.Init				= NULL;
	s_kpi.Deinit			= NULL;
	s_kpi.Open				= Open;
	s_kpi.OpenFromBuffer	= NULL;
	s_kpi.Close				= Close;
	s_kpi.Render			= Render;
	s_kpi.SetPosition		= SetPosition;

	return &s_kpi;
}
