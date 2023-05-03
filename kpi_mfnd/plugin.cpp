//=============================================================================
// Plugin interface implementation.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mfapi.h>
#include <mfidl.h>
#include <propsys.h>
#include <mfreadwrite.h>
#include <propkey.h>
#include "kmp_pi.h"

//-----------------------------------------------------------------------------
// 定義
//-----------------------------------------------------------------------------

#define SAFE_RELEASE(obj)	if (obj) { obj->Release(); obj = NULL; }

// メッセージ出力
#if defined(_DEBUG)
#define LOG_MSG(msg)	MessageBoxW(NULL, L##msg, L"kpi_mfnd", MB_OK)
#else //defined(_DEBUG)
#define LOG_MSG(msg)
#endif //defined(_DEBUG)

namespace {

// KMPプラグインバージョン
const DWORD KMP_PLUGIN_VERSION = 100;

// ストリームインデックス定数
const DWORD STREAM_INDEX = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

// 再生時コンテキスト
struct Context
{
	IMFSourceReader*	reader;
	IMFSample*			sample;
	IMFMediaBuffer*		buffer;
	UINT32				used_size;
	UINT32				out_bits;
	bool				is_float;
	bool				is_eos;
};

//-----------------------------------------------------------------------------
// intに入れたfloatをInt24に変換する
//-----------------------------------------------------------------------------
int Unpack24(int x)
{
	const int INT24_MAX_VALUE = (1 << 23) - 1;

	int e = (x >> 23) & 0xFF;
	e = (127 < e)? 127 : ((e < 104)? 104 : e);

	int value = ((x & 0x7FFFFF) + (1 << 23)) >> (127 - e);
	value = (INT24_MAX_VALUE < value)? INT24_MAX_VALUE : value;

	return (x < 0)? -value : value;
}

//-----------------------------------------------------------------------------
// 64bit/32bit -> 32bit への割り算
//-----------------------------------------------------------------------------
UINT32 Div6432(UINT64 dividend, UINT32 divisor)
{
#if defined(_WIN32) && !defined(_WIN64)
	UINT32 remainder = 0;
	UINT32* rem = &remainder;

	// 64bit を上位・下位32bitに分割
	UINT32 lowPart  = static_cast<UINT32>( 0x00000000FFFFFFFF & dividend);
	UINT32 highPart = static_cast<UINT32>((0xFFFFFFFF00000000 & dividend) >> 32);
	UINT32 result = 0;

	// 割り算実施
	_asm
	{
		mov eax, lowPart
		mov edx, highPart
		mov ecx, rem
		div divisor
		or  ecx, ecx
		jz  short label
		mov [ecx], edx
	label:
		mov result, eax
	}

	return result;
#else //defined(_WIN32) && !defined(_WIN64)
	return static_cast<UINT32>(dividend / static_cast<UINT64>(divisor));
#endif //defined(_WIN32) && !defined(_WIN64)
}

//----------------------------------------------------------------------------
// 32bitx32bit -> 64bit への掛け算
//-----------------------------------------------------------------------------
static UINT64 Mul3232To64(UINT32 x, UINT32 y)
{
#if defined(_WIN32) && !defined(_WIN64)
	// 64bit を上位・下位32bitに分割
	UINT32 low_part  = 0;
	UINT32 high_part = 0;

	// 掛け算実施
	_asm
	{
		mov eax,x
		mul y
		mov low_part, eax
		mov high_part, edx
	}

	UINT64 result = static_cast<UINT64>(high_part) << 32 | static_cast<UINT64>(low_part);
	return result;
#else //defined(_WIN32) && !defined(_WIN64)
	return static_cast<UINT64>(x) * static_cast<UINT64>(y);
#endif //defined(_WIN32) && !defined(_WIN64)
}

} //namespace

//-----------------------------------------------------------------------------
// Setup Audio stream and Get LPCM Format.
//-----------------------------------------------------------------------------
static bool ConfigureAudioStream(IMFSourceReader* reader,
						UINT32& rate, UINT32& bits, UINT32& ch, bool& is_float)
{
	HRESULT hr = S_OK;

	IMFMediaType* audioType = NULL;
	hr = MFCreateMediaType(&audioType);
	if (FAILED(hr)) {
		LOG_MSG("MFCreateMediaType_FAILED");
		return false;
	}

	hr = audioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (FAILED(hr)) {
		LOG_MSG("SetGUID(MF_MT_MAJOR_TYPE)_FAILED");
		audioType->Release();
		return false;
	}

	if (is_float) {
		// 後で必要なビット数へ変換するため、Floatでデコードする。
		hr = audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
		if (FAILED(hr)) {
			LOG_MSG("SetGUID(MF_MT_SUBTYPE)_FAILED");
			audioType->Release();
			return false;
		}
	}

#ifdef CHANNLES_SET
	// チャンネル数は、ステレオ固定。
	if (ch) {
		hr = audioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
		if (FAILED(hr)) {
			audioType->Release();
			return false;
		}
	}
#endif

	hr = reader->SetCurrentMediaType(STREAM_INDEX, NULL, audioType);
	if (FAILED(hr)) {
		if (is_float) {
			// Floatで失敗したら、PCMにしてみる。
			hr = audioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			if (FAILED(hr)) {
				LOG_MSG("SetGUID(MF_MT_SUBTYPE)/PCM_FAILED");
				audioType->Release();
				return false;
			}

			hr = reader->SetCurrentMediaType(STREAM_INDEX, NULL, audioType);
			if (FAILED(hr)) {
				LOG_MSG("SetCurrentMediaType_FAILED");
				audioType->Release();
				return false;
			}

			is_float = false;
		}
		else {
			LOG_MSG("SetCurrentMediaType_FAILED");
			audioType->Release();
			return false;
		}
	}

	audioType->Release();

	hr = reader->SetStreamSelection(STREAM_INDEX, TRUE);
	if (FAILED(hr)) {
		return false;
	}

	IMFMediaType* pcm_type = NULL;
	hr = reader->GetCurrentMediaType(STREAM_INDEX, &pcm_type);
	if (FAILED(hr)) {
		return false;
	}

	WAVEFORMATEX* wfx = NULL;
	UINT32 wfxSize = 0;

	hr = MFCreateWaveFormatExFromMFMediaType(pcm_type, &wfx, &wfxSize);
	pcm_type->Release();
	if (FAILED(hr)) {
		return false;
	}

	rate = static_cast<UINT32>(wfx->nSamplesPerSec);
	bits = static_cast<UINT32>(wfx->wBitsPerSample);
	ch   = static_cast<UINT32>(wfx->nChannels);

	bool is_valid = false;
	if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
		if (InlineIsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
			is_float = false;
			is_valid = true;
		}
		else if (InlineIsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
			is_float = true;
			is_valid = true;
		}
	}
	else {
		is_float = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
		is_valid = (wfx->wFormatTag == WAVE_FORMAT_PCM || wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
	}

	CoTaskMemFree(wfx);
	return is_valid;
}

//-----------------------------------------------------------------------------
// Get IMFSample & IMFMediaBuffer from Reader.
//-----------------------------------------------------------------------------
static bool GetSampleBuffer(IMFSourceReader* reader,
	IMFSample** out_sample, IMFMediaBuffer** out_buffer, bool& is_eos)
{
	HRESULT hr = S_OK; 
	DWORD flags = 0;

	IMFSample* sample = NULL;
	IMFMediaBuffer* buffer = NULL;

	hr = reader->ReadSample(STREAM_INDEX, 0, NULL, &flags, NULL, &sample);
	if (FAILED(hr) || !sample) {
		return false;
	}

	// フォーマット変更には対応しない。
	if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
		sample->Release();
		return false;
	}

	hr = sample->ConvertToContiguousBuffer(&buffer);
	if (FAILED(hr) || !buffer) {
		sample->Release();
		return false;
	}

	if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
		is_eos = true;
	}

	*out_sample = sample;
	*out_buffer = buffer;
	return true;
}

//-----------------------------------------------------------------------------
// MediaSourceを生成する
//-----------------------------------------------------------------------------
static HRESULT CreateMediaSource(const wchar_t* source_url, IMFMediaSource** media_source)
{
    if (!source_url) {
        return E_INVALIDARG;
    }

    if (!media_source) {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    
    MF_OBJECT_TYPE object_type = MF_OBJECT_INVALID;
    IMFSourceResolver* resolver = NULL;
    IUnknown* unk_source = NULL;

    // Create the source resolver.
    hr = MFCreateSourceResolver(&resolver);

    // Use the source resolver to create the media source.
    if (SUCCEEDED(hr)) {
        hr = resolver->CreateObjectFromURL(source_url, MF_RESOLUTION_MEDIASOURCE, NULL, &object_type, &unk_source);
    }

    // Get the IMFMediaSource from the IUnknown pointer.
    if (SUCCEEDED(hr)) {
        hr = unk_source->QueryInterface(IID_PPV_ARGS(media_source));
    }

	SAFE_RELEASE(resolver);
	SAFE_RELEASE(unk_source);

	return hr;
}

//-----------------------------------------------------------------------------
// プラグイン初期化
//-----------------------------------------------------------------------------
static void WINAPI Init()
{
	MFStartup(MF_VERSION, MFSTARTUP_LITE);
}

//-----------------------------------------------------------------------------
// プラグインの後始末
//-----------------------------------------------------------------------------
static void WINAPI Deinit()
{
	MFShutdown();
}

//-----------------------------------------------------------------------------
// ファイルを開く
//-----------------------------------------------------------------------------
static HKMP WINAPI Open(const char* path, SOUNDINFO* info)
{
	HRESULT hr = S_OK;

	wchar_t wpath[MAX_PATH];
	RtlZeroMemory(wpath, sizeof(wpath));
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	IMFMediaSource* source = NULL;
	hr = CreateMediaSource(wpath, &source);
	if (FAILED(hr)) {
		LOG_MSG("CreateMediaSource_FAILED");
		return NULL;
	}

	IMFPresentationDescriptor* pdesc = NULL;
	hr = source->CreatePresentationDescriptor(&pdesc);
	if (FAILED(hr)) {
		LOG_MSG("CreatePresentationDescriptor_FAILED");
		source->Release();
		return NULL;
	}

	UINT64 duration = 0;
	hr = pdesc->GetUINT64(MF_PD_DURATION, &duration);
	if (FAILED(hr)) {
		LOG_MSG("GetUINT64(MF_PD_DURATION)_FAILED");
		pdesc->Release();
		source->Release();
		return NULL;
	}

	info->dwLength   = Div6432(duration, 10000);
	info->dwSeekable = 1;

	pdesc->Release();
	source->Release();

	IMFSourceReader* reader = NULL;
	hr = MFCreateSourceReaderFromURL(wpath, NULL, &reader);
	if (FAILED(hr)) {
		LOG_MSG("MFCreateSourceReaderFromURL_FAILED");
		return NULL;
	}

	UINT32 rate = 44100;
	UINT32 bits = 16;
	UINT32 ch   = 2;
	bool is_float = true;

	if (!ConfigureAudioStream(reader, rate, bits, ch, is_float)) {
		LOG_MSG("ConfigureAudioStream_FAILED");
		reader->Release();
		return NULL;
	}

	if (is_float) {
		bits = 16;//(out.sampleSize == 24 )? 24 : 16;
	}

	Context* context = static_cast<Context*>(HeapAlloc(GetProcessHeap(), 0, sizeof(Context)));
	if (!context) {
		LOG_MSG("HeapAlloc_FAILED");
		reader->Release();
		return NULL;
	}

	context->reader		= reader;
	context->sample		= NULL;
	context->buffer		= NULL;
	context->used_size	= 0;
	context->out_bits	= bits;
	context->is_float	= is_float;
	context->is_eos		= false;

	info->dwSamplesPerSec	= rate;
	info->dwBitsPerSample	= bits;
	info->dwChannels		= ch;
	info->dwUnitRender		= 0;

	return context;
}

//-----------------------------------------------------------------------------
// ファイルを閉じる
//-----------------------------------------------------------------------------
static void WINAPI Close(HKMP hkmp)
{
	Context* cxt = static_cast<Context*>(hkmp);
	if (!cxt) {
		return;
	}

	if (cxt->buffer) {
		cxt->buffer->Release();
	}

	if (cxt->sample) {
		cxt->sample->Release();
	}

	cxt->reader->Release();

	HeapFree(GetProcessHeap(), 0, cxt);
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

	BYTE* copy_to_buf = static_cast<BYTE*>(buffer);
	UINT32 fill_size = 0;

	UINT32 mul = cxt->out_bits / 8;
	while (fill_size < size) {
		// MFMedaiBufferを取得。
		if (!cxt->sample && !cxt->is_eos) {
			if (!GetSampleBuffer(cxt->reader, &cxt->sample, &cxt->buffer, cxt->is_eos)) {
				return fill_size;
			}

			cxt->used_size = 0;
		}

		// もうない(=EOS)なら、何もせず。
		if (!cxt->buffer) {
			return fill_size;
		}

		BYTE* audioData = NULL;
		DWORD audioSize = 0;

		HRESULT hr = cxt->buffer->Lock(&audioData, NULL, &audioSize);
		if (FAILED(hr)) {
			return fill_size;
		}

		audioData += cxt->used_size;
		audioSize -= cxt->used_size;

		UINT32 needSize = size - fill_size;
		UINT32 copySize = 0;
		UINT32 intAudioSize = audioSize;

		if (cxt->is_float) {
			intAudioSize = audioSize / 4 * mul;
			copySize = std::min<UINT32>(needSize, intAudioSize);

			union IntByte4
			{
				INT32	i;
				BYTE	b[4];
			};

			UINT32 samples = copySize / mul;
			IntByte4 ib;
			int* sampleData = reinterpret_cast<int*>(audioData);
			for (UINT32 i = 0; i < samples; ++i) {
				ib.i = Unpack24(sampleData[i]);
				if (cxt->out_bits == 16) {
					*copy_to_buf++ = ib.b[1];
					*copy_to_buf++ = ib.b[2];
				}
				else {
					*copy_to_buf++ = ib.b[0];
					*copy_to_buf++ = ib.b[1];
					*copy_to_buf++ = ib.b[2];
				}
			}
		}
		else {
			copySize = std::min<UINT32>(needSize, audioSize);
			CopyMemory(copy_to_buf, audioData, copySize);
		}

		cxt->buffer->Unlock();

		fill_size += copySize;
		cxt->used_size += (cxt->is_float? (copySize / mul * 4) : copySize);

		// 使い切ったら、解放。
		if (copySize == intAudioSize) {
			cxt->buffer->Release();
			cxt->buffer = NULL;

			cxt->sample->Release();
			cxt->sample = NULL;

			cxt->used_size = 0;
		}
	}

	return fill_size;
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

	PROPVARIANT vPos;

	vPos.vt = VT_I8;
	vPos.hVal.QuadPart = LONGLONG(time_ms) * LONGLONG(10000);

	HRESULT hr = cxt->reader->SetCurrentPosition(GUID_NULL, vPos);

	if (cxt->buffer) {
		cxt->buffer->Release();
		cxt->buffer = NULL;
	}

	if (cxt->sample) {
		cxt->sample->Release();
		cxt->sample = NULL;
	}

	cxt->used_size = 0;
	return SUCCEEDED(hr)? time_ms : 0;
}

//-----------------------------------------------------------------------------
// KbMPプラグイン、メインAPI
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ KMPMODULE* WINAPI kmp_GetTestModule()
{
	static const char* ext_list[] = {".wav", ".wma", ".mp3", ".m4a", NULL};

	static KMPMODULE s_kpi;

	s_kpi.dwVersion			= KMPMODULE_VERSION;
	s_kpi.dwPluginVersion	= KMP_PLUGIN_VERSION;
	s_kpi.pszCopyright		= "Copyright (c) 2015-2021 MAYO.";
	s_kpi.pszDescription	= "Media Foundation plugin v1.05";
	s_kpi.ppszSupportExts	= ext_list;
	s_kpi.dwReentrant		= 0;

	s_kpi.Init				= Init;
	s_kpi.Deinit			= Deinit;
	s_kpi.Open				= Open;
	s_kpi.OpenFromBuffer	= NULL;
	s_kpi.Close				= Close;
	s_kpi.Render			= Render;
	s_kpi.SetPosition		= SetPosition;

	return &s_kpi;
}

//-----------------------------------------------------------------------------
// 数値プロパティ設定
//-----------------------------------------------------------------------------
static void SetPropertyNumber(IPropertyStore* props, const PROPERTYKEY& key, IKmpTagInfo* info, const char* name)
{
	PROPVARIANT var;

	HRESULT hr = props->GetValue(key, &var);
	if (FAILED(hr)) {
		return;
	}

	wchar_t value[32];
	RtlZeroMemory(value, sizeof(value));
	wsprintf(value, L"%d", var.ulVal);
	PropVariantClear(&var);

	info->aSetValueW(name, value);
}

//-----------------------------------------------------------------------------
// 文字プロパティを設定
//-----------------------------------------------------------------------------
static void SetPropertyString(IPropertyStore* props, const PROPERTYKEY& key, IKmpTagInfo* info, const char* name)
{
	PROPVARIANT var;

	HRESULT hr = props->GetValue(key, &var);
	if (FAILED(hr)) {
		return;
	}

	if (var.vt & VT_VECTOR) {
		const int MAX_TAGLEN = 511;
		wchar_t value[MAX_TAGLEN + 1];
		int used = 0;

		RtlZeroMemory(value, sizeof(value));
		for (ULONG i = 0; ((i < var.calpwstr.cElems) && (used < MAX_TAGLEN)); ++i) {
			lstrcpyn(&value[used], var.calpwstr.pElems[i], MAX_TAGLEN - used);
			used += lstrlen(var.calpwstr.pElems[i]);

			if (((i + 1) < var.calpwstr.cElems) && (used < MAX_TAGLEN)) {
				value[used++] = L',';
			}
		}

		value[MAX_TAGLEN - 1] = L'\0';
		info->aSetValueW(name, value);
	}
	else {
		info->aSetValueW(name, var.pwszVal);
	}

	PropVariantClear(&var);
}

//-----------------------------------------------------------------------------
// タグ情報取得設定API
//-----------------------------------------------------------------------------
extern "C" /*__declspec(dllexport)*/ BOOL WINAPI kmp_GetTestTagInfo(const char* path, IKmpTagInfo* info)
{
	if (!info) {
		return FALSE;
	}

	HRESULT hr = S_OK;

	wchar_t wpath[MAX_PATH];
	RtlZeroMemory(wpath, sizeof(wpath));
	MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	Init();

	IMFMediaSource* source = NULL;
	hr = CreateMediaSource(wpath, &source);
	if (FAILED(hr)) {
		Deinit();
		return FALSE;
	}

	IPropertyStore* props = NULL;
	hr = MFGetService(source, MF_PROPERTY_HANDLER_SERVICE, IID_PPV_ARGS(&props));
	if (SUCCEEDED(hr)) {
		// KBMP標準
		SetPropertyString(props, PKEY_Title,				info, SZ_KMP_NAME_TITLE_A);
		SetPropertyString(props, PKEY_Comment,				info, SZ_KMP_NAME_COMMENT_A);
		SetPropertyString(props, PKEY_Copyright,			info, SZ_KMP_NAME_COPYRIGHT_A);
		SetPropertyString(props, PKEY_Music_Artist,			info, SZ_KMP_NAME_ARTIST_A);
		SetPropertyString(props, PKEY_Music_AlbumArtist,	info, SZ_KMP_NAME_ALBUMARTIST_A);
		SetPropertyString(props, PKEY_Music_AlbumTitle,		info, SZ_KMP_NAME_ALBUM_A);
		SetPropertyString(props, PKEY_Music_Composer,		info, SZ_KMP_NAME_COMPOSER_A);
		SetPropertyString(props, PKEY_Music_Genre,			info, SZ_KMP_NAME_GENRE_A);
		SetPropertyNumber(props, PKEY_Music_TrackNumber,	info, SZ_KMP_NAME_TRACKNUMBER_A);
		SetPropertyNumber(props, PKEY_Media_Year,			info, SZ_KMP_NAME_DATE_A);

		// それ以外で取得できるもの
		SetPropertyString(props, PKEY_Music_Lyrics,			info, "Lyrics");
		SetPropertyString(props, PKEY_Music_PartOfSet,		info, "PartOfSet");

		props->Release();
	}

	source->Release();

	Deinit();
	return TRUE;
}
