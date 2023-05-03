//=============================================================================
// SMF data loader.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================
#pragma once

//! @brief	SMFデータ読み込み
class SmfLoader
{
	class SmfParser;

public:
	//! @brief	メタデータのテキスト最大長
	static const int METATEXT_MAXLEN = 255;

	//! @brief	トラック終了MIDIメッセージ
	static const int END_OF_TRACK = -1;

	//! @brief	MIDIメッセージタイプ
	enum MessageType 
	{
		MESSAGE_TYPE_NOTE,
		MESSAGE_TYPE_SYS_EX_ONE,
		MESSAGE_TYPE_SYS_EX_ALL,
		MESSAGE_TYPE_END_OF_TRACK,
	};

	//! @brief	フォーマット
	enum SmfFormat
	{
		SMF_FORMAT_0,	//!< SMF Format0
		SMF_FORMAT_1,	//!< SMF Format1
		SMF_FORMAT_2,	//!< SMF Format2
	};

	//! @brief	音源のリセット種類
	enum ResetType
	{
		RESET_TYPE_GM1,	//!< GM (デフォルト)
		RESET_TYPE_GM2,	//!< GM2
		RESET_TYPE_XG,	//!< Yamaha XG系
		RESET_TYPE_GS,	//!< Roland GS系
	};


	//! @brief	読み込み時の設定
	struct LoadOption
	{
		bool	ignore_bank_select;		//!< バンクセレクトを無視する
		bool	detect_reset_type;		//!< 音源リセットタイプを確定する
	};

	//! @brief	SysExメッセージ
	struct SysExMessage
	{
		const void*	data;	//!< メッセージデータ
		int			size;	//!< メッセージサイズ
	};

	//! @brief	MIDIメッセージ
	struct MidiMessage
	{
		MessageType		type;		//!< メッセージタイプ
		int				time;		//!< ミリ秒単位の絶対時間
		int				data;		//!< メッセージデータ
		int				port;		//!< 出力先ポート
		SysExMessage*	sys_ex;		//!< SysExメッセージ
	};

public:
	SmfLoader();
	~SmfLoader();

	//! @brief	SMFデータをファイルから読み込む
	bool Load(const wchar_t* path, const LoadOption& option);

	//! @brief	SMFデータをメモリ上から読み込む
	bool Load(const void* data, int size, const LoadOption& option);

	//! @breif	読み込んだデータをクリアする
	void Clear();

	//! @brief	再生するMIDIメッセージ数取得
	int GetMidiMessageNum() const;

	//! @brief	指定インデックスのMIDIメッセージ取得
	const MidiMessage& GetMidiMessage(int index) const;

	//! @brief	音源リセットメッセージ取得
	//! @note	読み込み時の設定で、音源リセットタイプを決める設定にする必要あり
	const void* GetResetMessageData() const;

	//! @brief	音源リセットメッセージのバイト数取得
	int GetResetMessageSize() const;

	//! @brief	音源リセットタイプを取得
	ResetType GetResetType() const;

	//! @brief	SMFフォーマット取得
	SmfFormat GetSmfFormat() const;

	//! @brief	トラック数取得
	int GetTrackNum() const;

	//! @brief	タイムベース（分解能）取得
	int GetTimeBase() const;

	//! @brief	曲の長さ取得
	int GetDuration() const;

	//! @brief	メタデータに設定されたタイトル取得
	const char* GetTitle() const;

	//! @brief	メタデータに設定された権利情報取得
	const char* GetCopyright() const;

private:
	MidiMessage*	m_message_data;	// MIDIメッセージデータ
	int				m_message_num;	// MIDIメッセージ数
	ResetType		m_reset_type;	// 音源リセットタイプ
	SmfFormat		m_smf_format;	// SMFフォーマット
	int				m_track_num;	// Format1のトラック数（SMF0は1固定）
	int				m_time_base;	// タイムベース
	int				m_duration;		// ミリ秒単位の時間
	int				m_port_num;		// ポート数

	// メタデータ
	char			m_title[METATEXT_MAXLEN + 1];
	char			m_copyright[METATEXT_MAXLEN + 1];
};
