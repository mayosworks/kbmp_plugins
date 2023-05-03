//=============================================================================
// IKpiDecoderModule declaration.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================
#pragma once

#include "kpi_impl.h"
#include "kpi_decoder.h"

class KpiDecoderModule
	: public KbKpiDecoderModuleImpl
{
public:
	KpiDecoderModule();
	virtual ~KpiDecoderModule();

	//IKpiDecoderModule派生
	void  WINAPI GetModuleInfo(const KPI_DECODER_MODULEINFO** module_info);

	DWORD WINAPI Open(const KPI_MEDIAINFO* media_info,
		IKpiFile* file, IKpiFolder* folder, IKpiDecoder** decoder);

	BOOL WINAPI EnumConfig(IKpiConfigEnumerator* enumerator);

	DWORD WINAPI ApplyConfig(const wchar_t* section_name,
		const wchar_t* key_name, INT64 int_value, double flt_value, const wchar_t* str_value);

private:
	KPI_DECODER_MODULEINFO m_module_info;
};
