//=============================================================================
// Plugin interface implementation.
//                                                     Copyright (c) 2016 MAYO.
//=============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include "kmp_pi.h"
#include "luna_pi.h"

//-----------------------------------------------------------------------------
// 定義
//-----------------------------------------------------------------------------
namespace {

// KMPプラグインバージョン
const DWORD KMP_PLUGIN_VERSION = 100;

// 再生時コンテキスト
struct Context
{
	int	snum;	// サンプル数
};

// グローバルオブジェクト
HINSTANCE	g_kpi_inst = NULL;
HINSTANCE	g_lp_inst = NULL;
LunaPlugin*	g_plugin = NULL;

template <typename T, size_t N>
char (*lengthof_helper_(T (&a)[N]))[N];
#define lengthof(a) (sizeof(*lengthof_helper_(a)))

} //namespace


//-----------------------------------------------------------------------------
// Dll Entry Point
//-----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD call_reason, void* /*reserved*/)
{
	// スレッドのアタッチは通知不要
	if (call_reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(instance);
		g_kpi_inst = instance;
	}

	return TRUE;
}

//-----------------------------------------------------------------------------
// プラグイン初期化
//-----------------------------------------------------------------------------
static void WINAPI Init(void)
{
	// none
}

//-----------------------------------------------------------------------------
// プラグインの後始末
//-----------------------------------------------------------------------------
static void WINAPI Deinit(void)
{
	if (!g_plugin) {
		return;
	}

	if (g_plugin->Release) {
		g_plugin->Release();
	}

	g_plugin = NULL;
}

//-----------------------------------------------------------------------------
// ファイルを開く
//-----------------------------------------------------------------------------
static HKMP WINAPI Open(const char* path, SOUNDINFO* info)
{
	if (!g_plugin) {
		return NULL;
	}

	wchar_t  wpath[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	Metadata meta = {0};
	if (!g_plugin->Parse(wpath, &meta)) {
		return NULL;
	}

	Output out = {0};
	out.sample_rate = info->dwSamplesPerSec;
	out.sample_bits = info->dwBitsPerSample;

	Handle handle = g_plugin->Open(wpath, &out);
	if (!handle) {
		return NULL;
	}

	info->dwSamplesPerSec	= out.sample_rate;
	info->dwBitsPerSample	= out.sample_bits;
	info->dwChannels		= out.num_channels;
	info->dwSeekable		= meta.seekable;
	info->dwLength			= meta.duration;
	info->dwUnitRender		= out.unit_length;

	return handle;
}

//-----------------------------------------------------------------------------
// ファイルを閉じる
//-----------------------------------------------------------------------------
static void WINAPI Close(HKMP hkmp)
{
	g_plugin->Close(hkmp);
}

//-----------------------------------------------------------------------------
// レンダリング
//-----------------------------------------------------------------------------
static DWORD WINAPI Render(HKMP hkmp, BYTE* buffer, DWORD size)
{
	return g_plugin->Render(hkmp, buffer, size);
}

//-----------------------------------------------------------------------------
// シーク
//-----------------------------------------------------------------------------
static DWORD WINAPI SetPosition(HKMP hkmp, DWORD time_ms)
{
	return g_plugin->Seek(hkmp, time_ms);
}

//-----------------------------------------------------------------------------
// KbMPプラグイン、メインAPI
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ KMPMODULE* WINAPI kmp_GetTestModule(void)
{
	HINSTANCE inst = g_kpi_inst;

	TCHAR path[MAX_PATH];

	GetModuleFileName(inst, path, MAX_PATH);
	PathRenameExtension(path, TEXT(".lp"));

	// クリティカルエラーなどのメッセージを非表示化
	UINT err_mode = SetErrorMode(SEM_FAILCRITICALERRORS);

	HINSTANCE lp_inst = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

	// エラーモードを戻す
	SetErrorMode(err_mode);

	if (!lp_inst) {
		return NULL;
	}

	const char* EXPAPI_NAME = "GetLunaPlugin";
	typedef LunaPlugin* (LPAPI* pfGetLunaPlugin)(HINSTANCE instance);

	pfGetLunaPlugin GetLunaPlugin = (pfGetLunaPlugin)GetProcAddress(lp_inst, EXPAPI_NAME);
	if (!GetLunaPlugin) {
		FreeLibrary(lp_inst);
		return NULL;
	}

	LunaPlugin* plugin = GetLunaPlugin(lp_inst);
	if (!plugin) {
		FreeLibrary(lp_inst);
		return NULL;
	}

	if (!plugin->plugin_name || !plugin->support_type || !plugin->Parse || !plugin->Open || !plugin->Close) {
		FreeLibrary(lp_inst);
		return NULL;
	}

	if (plugin->plugin_kind != KIND_PLUGIN) {
		FreeLibrary(lp_inst);
		return NULL;
	}

	static char plg_desc[256] = {'\0'};
	WideCharToMultiByte(CP_ACP, 0, plugin->plugin_name, -1, plg_desc, lengthof(plg_desc), NULL, NULL);

	static char ext_buf[256] = {'\0'};
	WideCharToMultiByte(CP_ACP, 0, plugin->support_type, -1, ext_buf, lengthof(ext_buf), NULL, NULL);

	// 拡張子バッファ
	static const char* ext_list[] = { NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	};

	int ext_num = 0;
	char* ext_ptr = ext_buf;
	for (int i = 0; (i < lengthof(ext_buf)) && ext_buf[i]; ++i) {
		if (ext_buf[i] == '*') {
			ext_buf[i] = '\0';
			ext_ptr = &ext_buf[i + 1];
		}
		else if (ext_buf[i] == ';') {
			if (ext_num < lengthof(ext_list)) {
				ext_list[ext_num] = ext_ptr;
				++ext_num;
			}

			ext_buf[i] = '\0';
			ext_ptr = &ext_buf[i + 1];
		}
	}

	static KMPMODULE s_kpi;

	s_kpi.dwVersion			= KMPMODULE_VERSION;
	s_kpi.dwPluginVersion	= KMP_PLUGIN_VERSION;
	s_kpi.pszCopyright		= "Copyright (c) 2016 MAYO.";
	s_kpi.pszDescription	= plg_desc;
	s_kpi.ppszSupportExts	= ext_list;
	s_kpi.dwReentrant		= 0xFFFFFFFF;

	s_kpi.Init				= Init;
	s_kpi.Deinit			= Deinit;
	s_kpi.Open				= Open;
	s_kpi.OpenFromBuffer	= NULL;
	s_kpi.Close				= Close;
	s_kpi.Render			= Render;
	s_kpi.SetPosition		= SetPosition;

	g_lp_inst = lp_inst;
	g_plugin = plugin;

	return &s_kpi;
}

//-----------------------------------------------------------------------------
// プラグイン設定API
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ DWORD WINAPI kmp_Config(HWND hwnd, DWORD /*version*/, DWORD /*reserved*/)
{
	if (g_plugin->Property) {
		g_plugin->Property(g_lp_inst, hwnd);
	}
	else {
		MessageBox(hwnd, L"Plugin has no property.", g_plugin->plugin_name, MB_OK);
	}

	return 0;
}

//-----------------------------------------------------------------------------
// タグ情報取得設定API
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ BOOL WINAPI kmp_GetTestTagInfo(const char* path, IKmpTagInfo* info)
{
	if (!info) {
		return FALSE;
	}

	wchar_t  wpath[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	Metadata meta = {0};
	if (!g_plugin->Parse(wpath, &meta)) {
		return NULL;
	}

	if (meta.title[0] != L'\0') {
		info->SetValueW(SZ_KMP_TAGINFO_NAME_TITLE, meta.title);
	}

	if (meta.artist[0] != L'\0') {
		info->SetValueW(SZ_KMP_TAGINFO_NAME_ARTIST, meta.artist);
	}

	if (meta.album[0] != L'\0') {
		info->SetValueW(SZ_KMP_TAGINFO_NAME_ALBUM, meta.album);
	}

	return TRUE;
}
