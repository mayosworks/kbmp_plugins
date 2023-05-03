//=============================================================================
// KbKpiDecoder declaration.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "kpi_impl.h"
#include "kpi_decoder.h"

class KpiDecoder
	: public KbKpiDecoderImpl
{
public:
	KpiDecoder();
	virtual ~KpiDecoder();

	bool Open(const KPI_MEDIAINFO* media_info, IKpiFile* file, IKpiFolder* folder);
	void Close();

	DWORD  WINAPI Select(DWORD number, const KPI_MEDIAINFO** media_info, IKpiTagInfo* tag_info, DWORD tag_get_flags);

	DWORD  WINAPI Render(BYTE* buffer, DWORD sample_size);
	UINT64 WINAPI Seek(UINT64 pos_sample, DWORD flag);

	DWORD  WINAPI UpdateConfig(void* reserved) { return 0; }

private:
	KPI_MEDIAINFO		m_media_info;
	IKpiFile*			m_kpi_file;
	IMFMediaSource*		m_source;
	IMFSourceReader*	m_reader;
	IMFSample*			m_sample;
	IMFMediaBuffer*		m_buffer;
	UINT32				m_sample_rate;
	UINT32				m_sample_size;
	UINT32				m_used_size;
	bool				m_finished;
};
