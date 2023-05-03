#include <windows.h>
#include "kpi_decoder.h"

///////////////////////////////////////////////////////////////////////////////
//�v���O�C�����Ŏg�����߂̃w���p�[�֐�
//(���ۂɎg���̂̓v���O�C����)
///////////////////////////////////////////////////////////////////////////////
DWORD WINAPI kpi_CreateDecoder(IKpiUnkProvider *pProvider,
                               const KPI_MEDIAINFO *cpRequest,//�Đ����g�����̗v���l���܂܂ꂽ KPI_MEDIAINFO
                               IKpiFile     *pFile,   //���y�f�[�^
                               IKpiFolder   *pFolder, //���y�f�[�^������t�H���_
                               IKpiDecoder **ppDecoder)
{
    return pProvider->CreateInstance(IID_IKpiDecoder,
                                     (void*)cpRequest, pFile, pFolder,
                                     NULL,
                                     (void**)ppDecoder);
}
///////////////////////////////////////////////////////////////////////////////
DWORD WINAPI kpi_CreateDecoder(IKpiUnknown *pUnknown,  //kpi_CreateInstance �̑�3�������璼�ڎ擾
                               const KPI_MEDIAINFO *cpRequest,//�Đ����g�����̗v���l���܂܂ꂽ KPI_MEDIAINFO
                               IKpiFile     *pFile,    //���y�f�[�^
                               IKpiFolder   *pFolder,  //���y�f�[�^������t�H���_
                               IKpiDecoder **ppDecoder)
{//IKpiUnknown(kpi_CreateInstance �̑�3����)���璼�ڎ擾
 //�Ăяo������ pUnknown �� kpi_CreateInstance �̑�3���������̂܂ܓn������
    IKpiUnkProvider *pProvider = NULL;
    if(pUnknown->QueryInterface(IID_IKpiUnkProvider, (void**)&pProvider) == S_OK){
        DWORD dwRet = kpi_CreateDecoder(pProvider,
                                        cpRequest, pFile, pFolder, ppDecoder);
        pProvider->Release();//pProvider ���s�v�Ȃ� ppDecoder �������łȂ��Ă�����\
        return dwRet;
    }
    *ppDecoder = NULL;
    return 0;
}
///////////////////////////////////////////////////////////////////////////////
//�v���O�C�����G�N�X�|�[�g����֐����g���ۂ̃w���p�[�֐�
//(���ۂɎg���͖̂{�̑�)
///////////////////////////////////////////////////////////////////////////////
HRESULT WINAPI kpi_CreateDecoderModule(pfn_kpiCreateInstance fnCreateInstance,
                                       IKpiDecoderModule **ppModule,
                                       IKpiUnknown *pUnknown)
{
    return fnCreateInstance(IID_IKpiDecoderModule, (void**)ppModule, pUnknown);
}
///////////////////////////////////////////////////////////////////////////////

