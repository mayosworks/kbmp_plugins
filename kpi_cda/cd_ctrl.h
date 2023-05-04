//=============================================================================
// CD-ROM device controller.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#ifndef CD_CTRL_H_
#define CD_CTRL_H_

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

//-----------------------------------------------------------------------------
// 定数定義
//-----------------------------------------------------------------------------
#define CDDA_SECT_SIZE	2352
#define	CDDA_MAX_TRACK	100
#define CDDA_TIME_OUT	10

//-----------------------------------------------------------------------------
// １トラックをあらわす構造体（CDは最大９９トラックまで可能）
//-----------------------------------------------------------------------------
struct TRACK
{
	BYTE	reserved1;	// 予約済み
	BYTE	track_type;	// ADR/属性 ４～７ビット：ADR
						// 0ビット：プリエンファシスあり(=0)/なし(=1)
						// 1ビット：コピー禁止(=0)/許可(=1)
						// 2ビット：オーディオトラック(=0)/データトラック(=1)
						// 3ビット：2チャンネル(=0)/4チャンネル(=1)オーディオ
	BYTE	track_no;	// トラック№
	BYTE	reserved2;	// 予約済み
	UINT	std_sector;	// 開始アドレス（LBA or MSB）
};

//-----------------------------------------------------------------------------
// TableOfContents構造
//-----------------------------------------------------------------------------
struct TOC
{
	WORD	toc_length;
	BYTE	std_track_no;
	BYTE	end_track_no;
	TRACK	track_list[CDDA_MAX_TRACK];
};

//-----------------------------------------------------------------------------
// CD-ROM Deviceコントロール
//-----------------------------------------------------------------------------
class CDCtrl
{
public:
	CDCtrl();
	~CDCtrl();

	// CD-ROMデバイスのOpen/Close
	bool OpenDevice(const char* path);
	void CloseDevice();

	// メディアが存在するか？
	bool IsMediaLoaded() const;

	// メディアをロックする（UNLOAD禁止）
	bool LockMedia(bool locked) const;

	// TOC読み取り
	bool ReadTOC(TOC& toc) const;

	// DAE実行前の初期化
	bool InitCDDA(UINT sectors);
	void TermCDDA();

	// CDDA読み取り
	bool ReadCDDA(UINT std_srctor, UINT read_sector, void* buffer, UINT size);

private:
	// デバイスの制御（DeviceIoControl）
	static ULONG DeviceCommand(HANDLE device, BYTE* cmd_ptr, BYTE cmd_len, void* buf, DWORD buf_len);

	// パラグラフ境界に設定する
	static void* ConvPrgBound(void* ptr);

	// ビックエディアン＜－＞リトルエディアン 変換（３２ビット用）
	static UINT ConvEndian32(UINT value);
	static WORD ConvEndian16(WORD value);

private:
	HANDLE	m_device;	// 制御対象デバイス
	void*	m_dae_ptr;	// DAE用メモリ領域
	BYTE*	m_dae_buf;	// DAE用アドレス調整済みバッファ
};

#endif //CD_CTRL_H_
