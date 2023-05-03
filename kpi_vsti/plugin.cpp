//=============================================================================
// Plugin interface implementation.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// 高速シークを行う（※正常に再生されない可能性はある）
#define ENABLE_QUICK_SEEK_MODE

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include "kmp_pi.h"
#include "vsti_host.h"
#include "smf_loader.h"

//-----------------------------------------------------------------------------
// 定義
//-----------------------------------------------------------------------------
namespace {

// KMPプラグインバージョン
const DWORD KMP_PLUGIN_VERSION = 100;

// １回のRenderで再生する時間、ミリ秒単位。
const int BLOCK_TIME = 100;

// 何分割してRenderするか。
const int DIVIDE_NUM = 10;

// １回のRenderの単位時間。
const int UNIT_TIME = (BLOCK_TIME / DIVIDE_NUM);
//static_assert((UNIT_TIME * DIVIDE_NUM) == BLOCK_TIME, "割っても整数になる数であること");

// 対応レート、ビットリスト
const int SUPPORT_RATE_LIST[] = {44100, 48000, 64000, 88200, 96000};
const int SUPPORT_BITS_LIST[] = {16, 24, 32};

// デフォルト設定。
const int DEFAULT_RATE_INDEX = 0;
const int DEFAULT_BITS_INDEX = 0;

// メッセージリスト
typedef std::vector<int>	MsgList;

// 再生時コンテキスト
struct Context
{
	int	midx;	// 今のメッセージインデックス
	int	mnum;	// メッセージ総数
	int	time;	// 今の再生時間
	int	tend;	// 終了再生時間
	int	bits;	// 出力ビット数
	int	snum;	// サンプル数
};

// グローバルオブジェクト
HINSTANCE	g_inst;
VstiHost	g_vsti;
SmfLoader	g_loader;

// プロトタイプ宣言
int RenderMidi(const MsgList& msg, void* buffer, int samples, int out_bits);
int PlayMidi(Context* cxt, void* buffer);

//-----------------------------------------------------------------------------
// クランプ
//-----------------------------------------------------------------------------
template <typename T> inline T Clamp(T x, T min, T max)
{
	return (x < min)? min : ((x > max)? max : x);
}

//-----------------------------------------------------------------------------
// MIDIファイルを開く
//-----------------------------------------------------------------------------
bool LoadMidiFile(const char* path, SmfLoader& loader)
{
	wchar_t wpath[MAX_PATH] = {L'\0'};
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	SmfLoader::LoadOption option;
	option.detect_reset_type = true;
	option.ignore_bank_select = false;

	return loader.Load(wpath, option);
}

//-----------------------------------------------------------------------------
// MIDIメッセージを送信し、PCMデータを生成する
//-----------------------------------------------------------------------------
int RenderMidi(const MsgList& msg, void* buffer, int samples, int out_bits)
{
	const int* msg_data = msg.empty()? NULL : &msg[0];
	if (!g_vsti.Render(msg_data, static_cast<int>(msg.size()), samples)) {
		return 0;
	}

	int written_size = 0;
	if (buffer && ((out_bits == 32) || (out_bits == 24) || (out_bits == 16))) {
		const float* ch0 = g_vsti.GetChannel0();
		const float* ch1 = g_vsti.GetChannel1();

		// 32bit PCM
		if (out_bits == 32) {
			const float S32_MUL =  1717934489.6f;	// 80%
			const float S32_MAX =  2147418112.0f;
			const float S32_MIN = -2147418112.0f;

			int* pcm = static_cast<int*>(buffer);
			for (int i = 0, j = 0; i < samples; ++i) {
				float v0 = Clamp(ch0[i] * S32_MUL, S32_MIN, S32_MAX);
				float v1 = Clamp(ch1[i] * S32_MUL, S32_MIN, S32_MAX);

				pcm[j++] = static_cast<int>(v0);
				pcm[j++] = static_cast<int>(v1);
			}
		}
		// 24bit PCM
		else if (out_bits == 24) {
			const float S24_MUL =  6710885.6f;	// 80%
			const float S24_MAX =  8388607.0f;
			const float S24_MIN = -8388608.0f;

			union Int4Byte
			{
				int		i;
				char	b[4];
			};

			Int4Byte ib;
			char* pcm = static_cast<char*>(buffer);
			for (int i = 0, j = 0; i < samples; ++i) {
				float v0 = Clamp(ch0[i] * S24_MUL, S24_MIN, S24_MAX);
				float v1 = Clamp(ch1[i] * S24_MUL, S24_MIN, S24_MAX);

				ib.i = static_cast<int>(v0);
				pcm[j++] = ib.b[0];
				pcm[j++] = ib.b[1];
				pcm[j++] = ib.b[2];

				ib.i = static_cast<int>(v1);
				pcm[j++] = ib.b[0];
				pcm[j++] = ib.b[1];
				pcm[j++] = ib.b[2];
			}
		}
		// 16bit PCM
		else if (out_bits == 16) {
			const float S16_MUL =  26213.6f;	// 80%
			const float S16_MAX =  32767.0f;
			const float S16_MIN = -32768.0f;

			short* pcm = static_cast<short*>(buffer);
			for (int i = 0, j = 0; i < samples; ++i) {
				float v0 = Clamp(ch0[i] * S16_MUL, S16_MIN, S16_MAX);
				float v1 = Clamp(ch1[i] * S16_MUL, S16_MIN, S16_MAX);

				pcm[j++] = static_cast<short>(v0);
				pcm[j++] = static_cast<short>(v1);
			}
		}

		written_size = samples * (out_bits / 8);
	}

	return written_size;
}

//-----------------------------------------------------------------------------
// MIDI再生
//-----------------------------------------------------------------------------
int PlayMidi(Context* cxt, void* buffer)
{
	MsgList msg_list;

	// 最後までいっている場合は、残りのサンプルを取得するだけ。
	if (cxt->midx == cxt->mnum) {
		if (cxt->time < cxt->tend) {
			return RenderMidi(msg_list, buffer, cxt->snum, cxt->bits);
		}

		return 0;
	}

	msg_list.reserve(128);

	// 現在の演奏時間までのメッセージを全て送信する
	int time = g_loader.GetMidiMessage(cxt->midx).time;
	while ((cxt->midx < cxt->mnum) && (time <= cxt->time)) {
		const SmfLoader::MidiMessage& msg = g_loader.GetMidiMessage(cxt->midx);

		if (msg.data != SmfLoader::END_OF_TRACK) {
			msg_list.push_back(msg.data);
		}

		if (++cxt->midx < cxt->mnum) {
			time = g_loader.GetMidiMessage(cxt->midx).time;
		}
	}

	return RenderMidi(msg_list, buffer, cxt->snum, cxt->bits);
}

} //namespace


//-----------------------------------------------------------------------------
// Dll Entry Point
//-----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD call_reason, void* /*reserved*/)
{
	// スレッドのアタッチは通知不要
	if (call_reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(instance);
		g_inst = instance;
	}

	return TRUE;
}

//-----------------------------------------------------------------------------
// プラグイン初期化
//-----------------------------------------------------------------------------
static void WINAPI Init()
{
	// none
}

//-----------------------------------------------------------------------------
// プラグインの後始末
//-----------------------------------------------------------------------------
static void WINAPI Deinit()
{
	g_vsti.Term();
}

//-----------------------------------------------------------------------------
// ファイルを開く
//-----------------------------------------------------------------------------
static HKMP WINAPI Open(const char* path, SOUNDINFO* info)
{
	// ２重再生は不可（ノンリエントラント設定だから呼ばれないはずだが、念のためチェックする）
	if (g_vsti.IsPlaying()) {
		return NULL;
	}

	if (!LoadMidiFile(path, g_loader)) {
		return NULL;
	}

	// 対応レートを検索し、一致するものを取得
	int rate_index = DEFAULT_RATE_INDEX;
	for (int i = 0; i < (sizeof(SUPPORT_RATE_LIST) / sizeof(SUPPORT_RATE_LIST[0])); ++i) {
		if (info->dwSamplesPerSec == SUPPORT_RATE_LIST[i]) {
			rate_index = i;
			break;
		}
	}

	// 対応ビット数を検索し、一致するものを取得
	int bits_index = DEFAULT_BITS_INDEX;
	for (int i = 0; i < (sizeof(SUPPORT_BITS_LIST) / sizeof(SUPPORT_BITS_LIST[0])); ++i) {
		if (info->dwBitsPerSample == SUPPORT_BITS_LIST[i]) {
			bits_index = i;
			break;
		}
	}

	info->dwSamplesPerSec	= SUPPORT_RATE_LIST[rate_index];
	info->dwBitsPerSample	= SUPPORT_BITS_LIST[bits_index];
	info->dwChannels		= 2;
#if defined(ENABLE_QUICK_SEEK_MODE)
	info->dwSeekable		= 1;
#else //defined(ENABLE_QUICK_SEEK_MODE)
	info->dwSeekable		= 0;
#endif //defined(ENABLE_QUICK_SEEK_MODE)
	info->dwLength			= g_loader.GetDuration();
	info->dwUnitRender		= MulDiv(info->dwSamplesPerSec * info->dwBitsPerSample / 8 * info->dwChannels, BLOCK_TIME, 1000);

	if (!g_vsti.Start(info->dwSamplesPerSec)) {
		return NULL;
	}

	Context* context = new Context();
	if (!context) {
		g_vsti.Stop();
		return NULL;
	}

	context->midx = 0;
	context->mnum = g_loader.GetMidiMessageNum();
	context->time = 0;
	context->tend = g_loader.GetDuration();
	context->bits = info->dwBitsPerSample;
	context->snum = info->dwUnitRender / ((info->dwBitsPerSample / 8) * info->dwChannels) / DIVIDE_NUM;

	g_vsti.Reset(g_loader.GetResetMessageData(), g_loader.GetResetMessageSize());
	return context;
}

//-----------------------------------------------------------------------------
// ストリームから開く
//-----------------------------------------------------------------------------
static HKMP WINAPI OpenFromBuffer(const BYTE* buf_data, DWORD buf_size, SOUNDINFO* info)
{
	// ２重再生は不可（ノンリエントラント設定だから呼ばれないはずだが、念のためチェックする）
	if (g_vsti.IsPlaying()) {
		return NULL;
	}

	SmfLoader::LoadOption option;
	option.detect_reset_type = true;
	option.ignore_bank_select = false;

	if (!g_loader.Load(buf_data, buf_size, option)) {
		return NULL;
	}

	// 対応レートを検索し、一致するものを取得
	int rate_index = DEFAULT_RATE_INDEX;
	for (int i = 0; i < (sizeof(SUPPORT_RATE_LIST) / sizeof(SUPPORT_RATE_LIST[0])); ++i) {
		if (info->dwSamplesPerSec == SUPPORT_RATE_LIST[i]) {
			rate_index = i;
			break;
		}
	}

	// 対応レートを検索し、一致するものを取得
	int bits_index = DEFAULT_BITS_INDEX;
	for (int i = 0; i < (sizeof(SUPPORT_BITS_LIST) / sizeof(SUPPORT_BITS_LIST[0])); ++i) {
		if (info->dwBitsPerSample == SUPPORT_BITS_LIST[i]) {
			bits_index = i;
			break;
		}
	}

	info->dwSamplesPerSec	= SUPPORT_RATE_LIST[rate_index];
	info->dwBitsPerSample	= SUPPORT_BITS_LIST[bits_index];
	info->dwChannels		= 2;
#if defined(ENABLE_QUICK_SEEK_MODE)
	info->dwSeekable		= 1;
#else //defined(ENABLE_QUICK_SEEK_MODE)
	info->dwSeekable		= 0;
#endif //defined(ENABLE_QUICK_SEEK_MODE)
	info->dwLength			= g_loader.GetDuration();
	info->dwUnitRender		= MulDiv(info->dwSamplesPerSec * info->dwBitsPerSample / 8 * info->dwChannels, BLOCK_TIME, 1000);

	if (!g_vsti.Start(info->dwSamplesPerSec)) {
		return NULL;
	}

	Context* context = new Context();
	if (!context) {
		g_vsti.Stop();
		return NULL;
	}

	context->midx = 0;
	context->mnum = g_loader.GetMidiMessageNum();
	context->time = 0;
	context->tend = g_loader.GetDuration();
	context->bits = info->dwBitsPerSample;
	context->snum = info->dwUnitRender / ((info->dwBitsPerSample / 8) * info->dwChannels) / DIVIDE_NUM;

	g_vsti.Reset(g_loader.GetResetMessageData(), g_loader.GetResetMessageSize());
	return context;
}

//-----------------------------------------------------------------------------
// ファイルを閉じる
//-----------------------------------------------------------------------------
static void WINAPI Close(HKMP hkmp)
{
	g_vsti.Stop();

	Context* context = static_cast<Context*>(hkmp);
	if (context) {
		delete context;
	}
}

//-----------------------------------------------------------------------------
// レンダリング
//-----------------------------------------------------------------------------
static DWORD WINAPI Render(HKMP hkmp, BYTE* buffer, DWORD size)
{
	Context* context = static_cast<Context*>(hkmp);
	if (!context) {
		return 0;
	}

	int unit_size = size / DIVIDE_NUM;
	for (int i = 0; i < DIVIDE_NUM; ++i) {
		if (!PlayMidi(context, buffer)) {
			return (i * unit_size);
		}

		context->time += UNIT_TIME;
		buffer += unit_size;
	}

	return size;
}

//-----------------------------------------------------------------------------
// シーク
//-----------------------------------------------------------------------------
static DWORD WINAPI SetPosition(HKMP hkmp, DWORD time_ms)
{
	Context* context = static_cast<Context*>(hkmp);
	if (!context) {
		return 0;
	}

	// シーク時は、リセットして先頭からやり直し
	g_vsti.Reset(g_loader.GetResetMessageData(), g_loader.GetResetMessageSize());
	context->midx = 0;
	context->time = 0;

#if defined(ENABLE_QUICK_SEEK_MODE)
	// シーク先の時間再計算、UNIT_TIME以下の単位を切り捨てる
	int seek_time = UNIT_TIME * (time_ms / UNIT_TIME);
	int seek_sec = seek_time / 1000;

	// １秒単位でダミーレンダリング
	for (int i = 1; i < seek_sec; ++i) {
		PlayMidi(context, NULL);
		context->time += 1000;
	}

	// ラスト１秒＋ミリ秒分のレンダリング
	while (context->time < seek_time) {
		PlayMidi(context, NULL);
		context->time += UNIT_TIME;
	}
#endif //defined(ENABLE_QUICK_SEEK_MODE)

	return context->time;
}

//-----------------------------------------------------------------------------
// KbMPプラグイン、メインAPI
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ KMPMODULE* WINAPI kmp_GetTestModule(void)
{
	HINSTANCE inst = g_inst;

	TCHAR vsti_path[MAX_PATH];
	GetModuleFileName(inst, vsti_path, MAX_PATH);
	PathRenameExtension(vsti_path, TEXT(".dll"));
	if (!PathFileExists(vsti_path)) {
		return NULL;
	}

	if (!g_vsti.Init(vsti_path)) {
		return NULL;
	}

	static const char* ext_list[] = {".mid", NULL};

	static KMPMODULE s_kpi;

	s_kpi.dwVersion			= KMPMODULE_VERSION;
	s_kpi.dwPluginVersion	= KMP_PLUGIN_VERSION;
	s_kpi.pszCopyright		= "Copyright (c) 2015-2017 MAYO.";
	s_kpi.pszDescription	= "VSTi MIDI plugin v1.02";
	s_kpi.ppszSupportExts	= ext_list;
	s_kpi.dwReentrant		= 0xFFFFFFFF;

	s_kpi.Init				= Init;
	s_kpi.Deinit			= Deinit;
	s_kpi.Open				= Open;
	s_kpi.OpenFromBuffer	= OpenFromBuffer;
	s_kpi.Close				= Close;
	s_kpi.Render			= Render;
	s_kpi.SetPosition		= SetPosition;

	return &s_kpi;
}

//-----------------------------------------------------------------------------
// プラグイン設定API
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ DWORD WINAPI kmp_Config(HWND hwnd, DWORD /*version*/, DWORD /*reserved*/)
{
	g_vsti.ShowEditor(g_inst, hwnd);
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

	SmfLoader loader;

	if (!LoadMidiFile(path, loader)) {
		return FALSE;
	}

	info->SetValueA(SZ_KMP_TAGINFO_NAME_TITLE, loader.GetTitle());
	info->SetValueA(SZ_KMP_TAGINFO_NAME_COPYRIGHT, loader.GetCopyright());

	char buf[64] = {0};

	int seconds = loader.GetDuration() / 1000;
	sprintf(buf, "%d:%02d.%03d", seconds / 60, seconds % 60, loader.GetDuration() % 1000);
	info->SetValueA("Duration", buf);

	const char* FORMAT[] = {"GM1", "GM2", "XG", "GS"};
	sprintf(buf, "Format:%d, Tracks:%d, TimeBase:%d, Reset:%s", loader.GetSmfFormat(),
		loader.GetTrackNum(), loader.GetTimeBase(), FORMAT[loader.GetResetType()]);
	info->SetValueA("Details", buf);
	return TRUE;
}
