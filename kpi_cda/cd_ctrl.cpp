//=============================================================================
// CD-ROM device controller.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winioctl.h>
#include <stddef.h>
#include "ntddscsi.h"
#include "devioctl.h"
#include "spti.h"
#include "memapi.h"
#include "cd_ctrl.h"

#ifndef SCSIOP_READ_CD
#define	SCSIOP_READ_CD	0xBE
#endif //SCSIOP_READ_CD

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
CDCtrl::CDCtrl()
	: m_device(INVALID_HANDLE_VALUE)
	, m_dae_ptr(NULL)
	, m_dae_buf(NULL)
{
}

//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
CDCtrl::~CDCtrl()
{
	TermCDDA();
	CloseDevice();
}

//-----------------------------------------------------------------------------
// CD-ROMデバイスを開く
//-----------------------------------------------------------------------------
bool CDCtrl::OpenDevice(const char* path)
{
	CloseDevice();

	wchar_t dev_path[16];

	wsprintf(dev_path, L"%c:\\", path[0]);
	if (GetDriveType(dev_path) != DRIVE_CDROM) {
		return false;
	}

	wsprintf(dev_path, L"\\\\.\\%c:", path[0]);
	m_device = CreateFile(dev_path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (m_device == INVALID_HANDLE_VALUE) {
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// CD-ROMデバイスを閉じる
//-----------------------------------------------------------------------------
void CDCtrl::CloseDevice()
{
	if (m_device != INVALID_HANDLE_VALUE) {
		CloseHandle(m_device);
		m_device = INVALID_HANDLE_VALUE;
	}
}

//-----------------------------------------------------------------------------
// ディスクが挿入されているか
//-----------------------------------------------------------------------------
bool CDCtrl::IsMediaLoaded() const
{
	DWORD bytes_returned = 0;

	if (!DeviceIoControl(m_device, IOCTL_STORAGE_CHECK_VERIFY,
						NULL, 0, NULL, 0, &bytes_returned, NULL)) {
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// ディスクをロックする
//-----------------------------------------------------------------------------
bool CDCtrl::LockMedia(bool locked) const
{
	PREVENT_MEDIA_REMOVAL pmr;
	DWORD bytesReturned = 0;

	pmr.PreventMediaRemoval = locked;

	if (!DeviceIoControl(m_device, IOCTL_STORAGE_MEDIA_REMOVAL,
			&pmr, sizeof(pmr), NULL, 0, &bytesReturned, NULL)) {
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// TOCを読み取る
//-----------------------------------------------------------------------------
bool CDCtrl::ReadTOC(TOC& toc) const
{
	BYTE buf[sizeof(TOC) + 0x10], cmd[16];

	ZeroMemory(&buf, sizeof(buf));
	ZeroMemory(&cmd, sizeof(cmd));

	cmd[0] = SCSIOP_READ_TOC;
	cmd[6] = 1;
	cmd[7] = HIBYTE(LOWORD(sizeof(toc)));
	cmd[8] = LOBYTE(LOWORD(sizeof(toc)));

	void* tocPtr = ConvPrgBound(buf);

	// TOCを取得する
	if (!DeviceCommand(m_device, cmd, 10, tocPtr, sizeof(toc))) {
		return false;
	}

	// TOC構造体にコピーする
	CopyMemory(&toc, tocPtr, sizeof(toc));

	// TOCサイズをエディアン変換する
	toc.toc_length = ConvEndian16(toc.toc_length);

	// 各トラックのアドレスをエディアン変換する
	for (UINT i = 0; i <= toc.end_track_no; ++i) {
		toc.track_list[i].std_sector = ConvEndian32(toc.track_list[i].std_sector);
	}

	return true;
}

//-----------------------------------------------------------------------------
// オーディオ読み取りを初期化する
//-----------------------------------------------------------------------------
bool CDCtrl::InitCDDA(UINT sectors)
{
	TermCDDA();

	m_dae_ptr = HeapAlloc(GetProcessHeap(), 0, (sectors + 1) * CDDA_SECT_SIZE);
	if (!m_dae_ptr) {
		return false;
	}

	m_dae_buf = static_cast<BYTE*>(ConvPrgBound(m_dae_ptr));
	return true;
}

//-----------------------------------------------------------------------------
// オーディオ読み取りを終了する
//-----------------------------------------------------------------------------
void CDCtrl::TermCDDA()
{
	if (m_dae_ptr) {
		HeapFree(GetProcessHeap(), 0, m_dae_ptr);
	}

	m_dae_ptr = NULL;
	m_dae_buf  = NULL;
}

//-----------------------------------------------------------------------------
// オーディオデータを取得する
//-----------------------------------------------------------------------------
bool CDCtrl::ReadCDDA(UINT std_srctor, UINT read_sector, void* buffer, UINT size)
{
	BYTE cmd[16];

	ZeroMemory(&cmd, sizeof(cmd));

	cmd[0] = SCSIOP_READ_CD;
	cmd[2] = HIBYTE(HIWORD(std_srctor));
	cmd[3] = LOBYTE(HIWORD(std_srctor));
	cmd[4] = HIBYTE(LOWORD(std_srctor));
	cmd[5] = LOBYTE(LOWORD(std_srctor));
	cmd[8] = static_cast<BYTE>(read_sector);
	cmd[9] = 0x10;

	if (!DeviceCommand(m_device, cmd, 12, m_dae_buf, size)) {
		return false;
	}

	CopyMemory(buffer, m_dae_buf, size);
	return true;
}

//-----------------------------------------------------------------------------
// デバイスにコマンドを送る
//-----------------------------------------------------------------------------
ULONG CDCtrl::DeviceCommand(HANDLE device, BYTE* cmd_ptr, BYTE cmd_len, void* buf, DWORD buf_len)
{
	SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER spt;

	ZeroMemory(&spt, sizeof(spt));
	CopyMemory(spt.sptd.Cdb, cmd_ptr, cmd_len);

	spt.sptd.Length				= sizeof(SCSI_PASS_THROUGH_DIRECT);
	spt.sptd.CdbLength			= cmd_len;
	spt.sptd.SenseInfoLength	= 24;
	spt.sptd.DataIn				= SCSI_IOCTL_DATA_IN;
	spt.sptd.DataBuffer			= buf;
	spt.sptd.DataTransferLength	= buf_len;
	spt.sptd.TimeOutValue		= CDDA_TIME_OUT;
	spt.sptd.SenseInfoOffset	= offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, ucSenseBuf);

	DWORD ret = 0, stSize = sizeof(spt);

	// IOCTRL実施
	if (!DeviceIoControl(device, IOCTL_SCSI_PASS_THROUGH_DIRECT, &spt, stSize, &spt, stSize, &ret, NULL)) {
		return 0;
	}

	return ret;
}

//-----------------------------------------------------------------------------
// アドレスを調整する
//-----------------------------------------------------------------------------
void* CDCtrl::ConvPrgBound(void* ptr)
{
	// 最下位アドレスを0の位置にする
	INT_PTR addr = reinterpret_cast<INT_PTR>(ptr) + 0x10;
	return reinterpret_cast<void*>(addr & (~0x0f));
}

//-----------------------------------------------------------------------------
// 32ビット分、エディアンを変換する
//-----------------------------------------------------------------------------
UINT CDCtrl::ConvEndian32(UINT value)
{
	UINT ret = 0;

	ret |= ((value      ) & 0x000000FF) << 24;
	ret |= ((value >>  8) & 0x000000FF) << 16;
	ret |= ((value >> 16) & 0x000000FF) <<  8;
	ret |= ((value >> 24) & 0x000000FF);
	return ret;
}

//-----------------------------------------------------------------------------
// 16ビット分エディアンを変換する
//-----------------------------------------------------------------------------
WORD CDCtrl::ConvEndian16(WORD value)
{
	WORD ret = 0;

	ret |= ((value     ) & 0x00FF) << 8;
	ret |= ((value >> 8) & 0x00FF);
	return ret;
}
