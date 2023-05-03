//=============================================================================
// KbKpiDecoder implementation.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _T(var)	TEXT(var)

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <propsys.h>
#include <mfreadwrite.h>
#include <propkey.h>
#include <algorithm>
#include "kmp_pi.h"
#include "plugin_decoder.h"

//-----------------------------------------------------------------------------
// マクロ定義
//-----------------------------------------------------------------------------

// メッセージ出力
#if defined(_DEBUG)
#define LOG_MSG(msg)	MessageBoxW(NULL, L##msg, L"kpi_mfnd", MB_OK)
#else //defined(_DEBUG)
#define LOG_MSG(msg)
#endif //defined(_DEBUG)

// 解放マクロ
#define SAFE_RELEASE(obj)	if (obj) { obj->Release(); obj = NULL; }

//-----------------------------------------------------------------------------
// 定数、内部関数定義
//-----------------------------------------------------------------------------
namespace {

// ストリームインデックス定数
const DWORD STREAM_INDEX = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

//-----------------------------------------------------------------------------
// MediaSourceを生成する
//-----------------------------------------------------------------------------
HRESULT CreateMediaSource(const wchar_t* source_url, IMFMediaSource** media_source)
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
// Setup Audio stream and Get LPCM Format.
//-----------------------------------------------------------------------------
bool ConfigureAudioStream(IMFSourceReader* reader, WAVEFORMATEX& wfx)
{
	HRESULT hr = S_OK;

	IMFMediaType* audio_type = NULL;
	hr = MFCreateMediaType(&audio_type);
	if (FAILED(hr)) {
		LOG_MSG("MFCreateMediaType_FAILED");
		return false;
	}

	hr = audio_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (FAILED(hr)) {
		LOG_MSG("SetGUID(MF_MT_MAJOR_TYPE)_FAILED");
		SAFE_RELEASE(audio_type);
		return false;
	}

	hr = reader->SetCurrentMediaType(STREAM_INDEX, NULL, audio_type);
	if (FAILED(hr)) {
		// PCMにしてみる。
		hr = audio_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		if (FAILED(hr)) {
			LOG_MSG("SetGUID(MF_MT_SUBTYPE)/PCM_FAILED");
			SAFE_RELEASE(audio_type);
			return false;
		}

		hr = reader->SetCurrentMediaType(STREAM_INDEX, NULL, audio_type);
		if (FAILED(hr)) {
			LOG_MSG("SetCurrentMediaType_FAILED");
			SAFE_RELEASE(audio_type);
			return false;
		}
	}

	SAFE_RELEASE(audio_type);

	hr = reader->SetStreamSelection(STREAM_INDEX, TRUE);
	if (FAILED(hr)) {
		return false;
	}

	IMFMediaType* pcm_type = NULL;
	hr = reader->GetCurrentMediaType(STREAM_INDEX, &pcm_type);
	if (FAILED(hr)) {
		return false;
	}

	WAVEFORMATEX* pcm_fmt = NULL;
	UINT32 fmt_size = 0;

	hr = MFCreateWaveFormatExFromMFMediaType(pcm_type, &pcm_fmt, &fmt_size);
	pcm_type->Release();
	if (FAILED(hr)) {
		return false;
	}

	CopyMemory(&wfx, pcm_fmt, sizeof(wfx));

	if (pcm_fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pcm_fmt);
		if (InlineIsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
			wfx.wFormatTag = WAVE_FORMAT_PCM;
		}
		else if (InlineIsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
			wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
	}

	CoTaskMemFree(pcm_fmt);

	// PCM/INTのみ有効
	return (wfx.wFormatTag == WAVE_FORMAT_PCM);
}

//-----------------------------------------------------------------------------
// Get IMFSample & IMFMediaBuffer from Reader.
//-----------------------------------------------------------------------------
bool GetSampleBuffer(IMFSourceReader* reader,
	IMFSample** out_sample, IMFMediaBuffer** out_buffer, bool& end_of_stream)
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
		end_of_stream = true;
	}

	*out_sample = sample;
	*out_buffer = buffer;
	return true;
}

} //namespace


KpiDecoder::KpiDecoder()
	: m_media_info()
	, m_kpi_file(NULL)
	, m_source(NULL)
	, m_reader(NULL)
	, m_sample(NULL)
	, m_buffer(NULL)
	, m_sample_rate(0)
	, m_sample_size(0)
	, m_used_size(0)
	, m_finished(false)
{
    kpi_InitMediaInfo(&m_media_info);
}

KpiDecoder::~KpiDecoder()
{
	Close();
}

bool KpiDecoder::Open(const KPI_MEDIAINFO* media_info, IKpiFile* kpi_file, IKpiFolder* /*kpi_folder*/)
{
	HRESULT hr = S_OK;

	const wchar_t* wpath = NULL;
	if (!kpi_file->GetRealFileW(&wpath)) {
		return false;
	}

	IMFMediaSource* source = NULL;
	hr = CreateMediaSource(wpath, &source);
	if (FAILED(hr)) {
		LOG_MSG("CreateMediaSource_FAILED");
		return false;
	}

	IMFPresentationDescriptor* pdesc = NULL;
	hr = source->CreatePresentationDescriptor(&pdesc);
	if (FAILED(hr)) {
		LOG_MSG("CreatePresentationDescriptor_FAILED");
		SAFE_RELEASE(source);
		return false;
	}

	UINT64 duration = 0;
	hr = pdesc->GetUINT64(MF_PD_DURATION, &duration);
	if (FAILED(hr)) {
		LOG_MSG("GetUINT64(MF_PD_DURATION)_FAILED");
		SAFE_RELEASE(pdesc);
		SAFE_RELEASE(source);
		return false;
	}

	SAFE_RELEASE(pdesc);
	m_source = source;

	IMFSourceReader* reader = NULL;
	hr = MFCreateSourceReaderFromURL(wpath, NULL, &reader);
	if (FAILED(hr)) {
		LOG_MSG("MFCreateSourceReaderFromURL_FAILED");
		return false;
	}

	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(wfx));
	wfx.cbSize = sizeof(wfx);

	if (!ConfigureAudioStream(reader, wfx)) {
		LOG_MSG("ConfigureAudioStream_FAILED");
		SAFE_RELEASE(reader);
		return NULL;
	}

	kpi_file->AddRef();
	m_kpi_file = kpi_file;

	m_reader = reader;
	m_sample = NULL;
	m_buffer = NULL;

	m_sample_rate = wfx.nSamplesPerSec;
	m_sample_size = wfx.wBitsPerSample / 8 * wfx.nChannels;
	m_used_size = 0;
	m_finished = false;

	m_media_info.dwNumber = 1;
	m_media_info.dwCount = 1;
	m_media_info.dwFormatType = KPI_MEDIAINFO::FORMAT_PCM;
	m_media_info.dwSampleRate = wfx.nSamplesPerSec;
	m_media_info.nBitsPerSample = wfx.wBitsPerSample;
	m_media_info.dwChannels = wfx.nChannels;
	m_media_info.qwLength = duration;
	m_media_info.dwUnitSample = 0;
	m_media_info.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE | KPI_MEDIAINFO::SEEK_FLAGS_ACCURATE | KPI_MEDIAINFO::SEEK_FLAGS_ROUGH;

	if (media_info) {
		media_info = &m_media_info;
	}

	return true;
}

void KpiDecoder::Close()
{
	SAFE_RELEASE(m_buffer);
	SAFE_RELEASE(m_sample);
	SAFE_RELEASE(m_reader);
	SAFE_RELEASE(m_source);

	SAFE_RELEASE(m_kpi_file);

	m_used_size = 0;
	m_finished = false;
}

DWORD WINAPI KpiDecoder::Select(DWORD number,
	const KPI_MEDIAINFO** media_info, IKpiTagInfo* tag_info, DWORD tag_get_flags)
{
	if (number < 1 || number > m_media_info.dwCount) {
        return 0;
    }

	if (tag_info) {
		IPropertyStore* props = NULL;
		HRESULT hr = MFGetService(m_source, MF_PROPERTY_HANDLER_SERVICE, IID_PPV_ARGS(&props));
		if (SUCCEEDED(hr)) {
			PROPVARIANT var;

			hr = props->GetValue(PKEY_Title, &var);
			if (SUCCEEDED(hr)) {
				tag_info->wSetValueW(SZ_KMP_NAME_TITLE, -1, var.pwszVal, -1);
				PropVariantClear(&var);
			}

			hr = props->GetValue(PKEY_Music_Artist, &var);
			if (SUCCEEDED(hr)) {
				// 配列になってることはあまり無いと思うけど、先頭だけコピー
				if (var.vt & VT_VECTOR) {
					tag_info->wSetValueW(SZ_KMP_NAME_ARTIST, -1, var.calpwstr.pElems[0], -1);
				}
				else {
					tag_info->wSetValueW(SZ_KMP_NAME_ARTIST, -1, var.pwszVal, -1);
				}

				PropVariantClear(&var);
			}

			hr = props->GetValue(PKEY_Music_AlbumTitle, &var);
			if (SUCCEEDED(hr)) {
				tag_info->wSetValueW(SZ_KMP_NAME_ALBUM, -1, var.pwszVal, -1);
				PropVariantClear(&var);
			}
		}
	}

	if (media_info) {
		*media_info = &m_media_info;
    }

    return 1;
}

DWORD WINAPI KpiDecoder::Render(BYTE* buffer, DWORD sample_size)
{
	UINT32 read_size = sample_size * m_sample_size;
	UINT32 fill_size = 0;
	DWORD bffer_used = 0;

	while (fill_size < read_size) {
		// MFMedaiBufferを取得。
		if (!m_sample && !m_finished) {
			if (!GetSampleBuffer(m_reader, &m_sample, &m_buffer, m_finished)) {
				return fill_size / m_sample_size;
			}

			m_used_size = 0;
		}

		// もうない(=EOS)なら、何もせず。
		if (!m_buffer) {
			return fill_size / m_sample_size;
		}

		BYTE* audio_data = NULL;
		DWORD audio_size = 0;

		HRESULT hr = m_buffer->Lock(&audio_data, NULL, &audio_size);
		if (FAILED(hr)) {
			return fill_size;
		}

		audio_data += m_used_size;
		audio_size -= m_used_size;

		UINT32 need_size = read_size - fill_size;
		UINT32 copy_size = std::min<UINT32>(need_size, audio_size);
		CopyMemory(buffer + bffer_used, audio_data, copy_size);
		bffer_used += copy_size;

		m_buffer->Unlock();

		fill_size += copy_size;
		m_used_size += copy_size;

		// 使い切ったら、解放。
		if (copy_size == audio_size) {
			SAFE_RELEASE(m_buffer);
			SAFE_RELEASE(m_sample);
			m_used_size = 0;
		}
	}

	return fill_size / m_sample_size;
}

UINT64 WINAPI KpiDecoder::Seek(UINT64 pos_sample, DWORD flag)
{
	PROPVARIANT vPos;

	vPos.vt = VT_I8;
	vPos.hVal.QuadPart = kpi_SampleTo100ns(pos_sample, m_sample_rate);

	HRESULT hr = m_reader->SetCurrentPosition(GUID_NULL, vPos);

	SAFE_RELEASE(m_buffer);
	SAFE_RELEASE(m_sample);
	m_used_size = 0;

	return SUCCEEDED(hr)? pos_sample : 0;
}
