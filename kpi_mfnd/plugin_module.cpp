//=============================================================================
// IKpiDecoderModule implementation.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#include <windows.h>
#include <propsys.h>
#include "plugin_module.h"
#include "plugin_decoder.h"

namespace {

const GUID GUID_KPI_PLUGIN =
	{ 0x5a92c5c7, 0x6068, 0x41e7, { 0xbb, 0x7d, 0xd3, 0x9d, 0xeb, 0x71, 0xe, 0xd } };

} //namespace

KpiDecoderModule::KpiDecoderModule()
	: KbKpiDecoderModuleImpl()
	, m_module_info()
{
	ZeroMemory(&m_module_info, sizeof(m_module_info));

	m_module_info.cb = sizeof(KPI_DECODER_MODULEINFO);
	m_module_info.dwModuleVersion = KPI_DECODER_MODULE_VERSION;
	m_module_info.dwPluginVersion = 100;
	m_module_info.dwMultipleInstance = KPI_MULTINST_INFINITE;
	m_module_info.guid = GUID_KPI_PLUGIN;
	m_module_info.cszDescription = L"Media Foundation plugin v1.05";
	m_module_info.cszCopyright = L"Copyright (c) 2015-2021 MAYO.";
	m_module_info.cszSupportExts = L".wav/.wma/.mp3/.m4a";
	m_module_info.dwSupportTagInfo = KPI_DECODER_MODULEINFO::TAG_TITLE;
}

KpiDecoderModule::~KpiDecoderModule()
{
	MFShutdown();
}

void  WINAPI KpiDecoderModule::GetModuleInfo(const KPI_DECODER_MODULEINFO** module_info)
{
    *module_info = &m_module_info;
}

DWORD WINAPI KpiDecoderModule::Open(const KPI_MEDIAINFO* media_info,
	IKpiFile* kpi_file, IKpiFolder* kpi_folder, IKpiDecoder** kpi_decoder)
{
	KpiDecoder* decoder = new KpiDecoder();
	if (!decoder->Open(media_info, kpi_file, kpi_folder)) {
		delete decoder;
		return 0;
	}

	*kpi_decoder = decoder;
	return 1;
}

BOOL WINAPI KpiDecoderModule::EnumConfig(IKpiConfigEnumerator* enumerator)
{
	return FALSE;
}

DWORD WINAPI KpiDecoderModule::ApplyConfig(const wchar_t* section_name,
	const wchar_t* key_name, INT64 int_value, double flt_value, const wchar_t* str_value)
{
	return KPI_CFGRET_OK;
}

HRESULT WINAPI kpi_CreateInstance(REFIID riid, void** object, IKpiUnknown* unknown)
{
	*object = NULL;

	if (!IsEqualIID(riid, IID_IKpiDecoderModule)) {
		return E_NOINTERFACE;
	}

	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (FAILED(hr)) {
		return E_FAIL;
	}

	KpiDecoderModule* decoder_module = new KpiDecoderModule();
	*object = decoder_module;
	return S_OK;
}
