#include <windows.h>
#include "kpi.h"
#include "kpi_impl.h"

class KbKpiNullConfig : public KbKpiUnknownImpl<IKpiConfig>
{//��Ƀf�t�H���g�l��Ԃ� IKpiConfig 
 //�{�̂���� kpi_CreateConfig �����s�����Ƃ��ɂ��̃N���X�̃C���X�^���X��Ԃ�
public:
    void   WINAPI SetInt(const wchar_t *cszSection, const wchar_t *cszKey, INT64  nValue){}
    INT64  WINAPI GetInt(const wchar_t *cszSection, const wchar_t *cszKey, INT64  nDefault){return nDefault;}
    void   WINAPI SetFloat(const wchar_t *cszSection, const wchar_t *cszKey, double dValue){}
    double WINAPI GetFloat(const wchar_t *cszSection, const wchar_t *cszKey, double dDefault){return dDefault;}
    void   WINAPI SetStr(const wchar_t *cszSection, const wchar_t *cszKey, const wchar_t *cszValue){}
    DWORD  WINAPI GetStr(const wchar_t *cszSection, const wchar_t *cszKey, wchar_t *pszValue,
                         DWORD dwSize, const wchar_t *cszDefault){
        lstrcpynW(pszValue, cszDefault, dwSize/sizeof(wchar_t));return (lstrlenW(cszDefault)+1)*sizeof(wchar_t);}
    //binary
    void   WINAPI SetBin(const wchar_t *cszSection, const wchar_t *cszKey, const BYTE *pBuffer, DWORD dwSize){};
    DWORD  WINAPI GetBin(const wchar_t *cszSection, const wchar_t *cszKey, BYTE *pBuffer, DWORD dwSize){
        ZeroMemory(pBuffer, dwSize); return 0;}
};
///////////////////////////////////////////////////////////////////////////////
//IKpiUnkProvider �̃w���p�[�֐�(���ۂɎg���̂̓v���O�C����)
//IKpiUnkProvider �ɏ��߂���Ȃ��̂́A�@�\��ǉ�����x��
//�h���N���X�����Ȃ��Ƃ����Ȃ��Ȃ�̂�������邽��
///////////////////////////////////////////////////////////////////////////////
DWORD WINAPI kpi_CreateConfig(IKpiUnkProvider *pProvider,
                              const GUID *pGUID,
                              DWORD *pdwPlatform,
                              IKpiConfig **ppConfig)
{
    if(pdwPlatform){
    //pProvider ���d�l�ύX���ɂ�� IID_IKpiConfig �ɑΉ����Ȃ�
    //�ꍇ�AppvObj �ȊO�̖��m�̈����͒l���ύX����Ȃ����߁A
    //�ύX�����҂��������ɑ΂��ĕs��Ȓl�ɂȂ�Ȃ��悤��
    //�\�߉��炩�̒l�������Ă���
        *pdwPlatform = (DWORD)-1;//���Ή��̏ꍇ�� -1 ��Ԃ�
    }
    *ppConfig = NULL;
    DWORD dwRet =pProvider->CreateInstance(IID_IKpiConfig,
                                     (void*)pGUID,
                                     (void*)pdwPlatform,
                                     NULL, NULL,
                                     (void**)ppConfig);
    if(*ppConfig){
        return dwRet;
    }
    //�v���O�C���̌Ăяo����(�ʏ�� KbMedia Player �{��)�� IKpiConfig �ɖ��Ή�
    //��Ƀf�t�H���g�l��Ԃ� IKpiConfig ��Ԃ�
    *ppConfig = new KbKpiNullConfig;
    if(pdwPlatform){
        *pdwPlatform = (DWORD)-1;
    }
    return 1;
}
///////////////////////////////////////////////////////////////////////////////
DWORD WINAPI kpi_CreateConfig(IKpiUnknown *pUnknown, //kpi_CreateInstance �̑�3����
                              const GUID *pGUID,
                              DWORD *pdwPlatform,
                              IKpiConfig **ppConfig)
{//IKpiUnknown(kpi_CreateInstance �̑�3����)���璼�ڎ擾
 //�Ăяo������ pUnknown �� kpi_CreateInstance �̑�3���������̂܂ܓn������
    IKpiUnkProvider *pProvider = NULL;
    if(pUnknown->QueryInterface(IID_IKpiUnkProvider, (void**)&pProvider) == S_OK){
        DWORD dwRet = kpi_CreateConfig(pProvider, pGUID, pdwPlatform, ppConfig);
        pProvider->Release();//pProvider ���s�v�Ȃ� ppConfig �������łȂ��Ă�����\
        return dwRet;
    }
    *ppConfig = NULL;
    return 0;
}
/*
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
*/
