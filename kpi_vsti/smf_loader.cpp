//=============================================================================
// SMF data loader.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#include "smf_loader.h"

#include <vector>
#include <map>
#include <algorithm>

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// 内部定義
namespace {

// 定数系定義

// GMリセットバイトシーケンス
const unsigned char DATA_GM1[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
const unsigned char DATA_GM2[] = {0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7};

// ヤマハ系リセットバイトシーケンス
const unsigned char DATA_XG[]  = {0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7};
const unsigned char DATA_MU0[] = {0xF0, 0x43, 0x10, 0x49, 0x00, 0x00, 0x12, 0x00, 0xF7};
const unsigned char DATA_MU1[] = {0xF0, 0x43, 0x10, 0x49, 0x00, 0x00, 0x12, 0x01, 0xF7};

// ローランド系リセットバイトシーケンス
const unsigned char DATA_GS[]  = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
const unsigned char DATA_88S[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7F, 0x00, 0x01, 0xF7};
const unsigned char DATA_88D[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7F, 0x01, 0x00, 0xF7};

// エンディアン変換(16bit)
int Swap16(const unsigned char* value)
{
	return (static_cast<int>(value[1])     )
		 | (static_cast<int>(value[0]) << 8);
}

// エンディアン変換(24bit)
int Swap24(const unsigned char* value)
{
	return (static_cast<int>(value[2])      )
		 | (static_cast<int>(value[1]) <<  8)
		 | (static_cast<int>(value[0]) << 16);
}

// エンディアン変換(32bit)
int Swap32(const unsigned char* value)
{
	return (static_cast<int>(value[3])      )
		 | (static_cast<int>(value[2]) <<  8)
		 | (static_cast<int>(value[1]) << 16)
		 | (static_cast<int>(value[0]) << 24);
}

// 64bit割り算
int Mul64Div32(int a, int b, int c)
{
	//return static_cast<int>(long long(a) * long long(b) / long long(c));
	return MulDiv(a, b, c);
}

// ブランク文字列消し
template <int LEN>
void TrimBlank(char (&buf)[LEN])
{
	const char SPACE0 = char(0x81);
	const char SPACE1 = char(0x40);

	buf[LEN - 1] = '\0';

	for (int i = (LEN - 1); 0 < i; --i) {
		const char c0 = buf[i];
		const char c1 = buf[i - 1];

		// 半角スペース、タブ、改行、全角スペースの２バイト目以外なら、そのまま
		if (c0 != ' ' && c0 != '\t' && c0 != '\r' && c0 != '\n' && c0 != SPACE0) {
			break;
		}

		// 全角文字のうち、スペースは削除する
		if (c0 == SPACE0 && c1 == SPACE1) {
			buf[i--] = '\0';
		}

		buf[i] = '\0';
	}
}

} //namespace

// SMFデータ解析
class SmfLoader::SmfParser
{
public:
	typedef std::vector<MidiMessage>	MidiMessageVector;
	typedef std::map<int, int>			TempoMap;

public:
	SmfParser(const unsigned char* data, int size, const LoadOption& option);
	~SmfParser();

	// 解析
	void ParseSmf();

	// MIDI演奏データアクセサ
	const MidiMessageVector& GetMidiMessageData() const { return messages_; }
	ResetType GetResetType() const { return m_reset_type; }
	SmfFormat GetSmfFormat() const { return m_smf_format; }
	int GetTrackNum() const { return m_track_num; }
	int GetTimeBase() const { return m_time_base; }
	int GetDuration() const { return m_duration; }
	int GetPortNum() const { return m_port_num; }

	// メタデータアクセサ
	void GetTitle(char* title) const { memcpy(title, m_title, sizeof(m_title)); }
	void GetCopyright(char* copyright) const { memcpy(copyright, m_copyright, sizeof(m_copyright)); }

private:
	bool ParseHeader(const unsigned char* data, int size, int& used);
	bool ParseTrack(const unsigned char* data, int size, int& used);

	int ParseExMessage(const unsigned char* data);
	int ParseMetaEvent(const unsigned char* data, int delta_time);
	int CalcPlayTime(int delta_time) const;

	int ParseNumber(const unsigned char* data, int& used);
	int ParseMessage(const unsigned char* data, int& used, int prev_msg, bool& ignore);

public:
	const unsigned char*	raw_data_;		// MIDIの生データ
	int						raw_size_;		// 生データサイズ
	LoadOption				option_;		// 読み込みオプション

	// MIDI演奏データ
	MidiMessageVector		messages_;		// 再生時間・チャンネル順に並べたMIDIメッセージ
	TempoMap				tempo_map_;		// テンポリスト
	ResetType				m_reset_type;	// 音源リセットタイプ
	SmfFormat				m_smf_format;	// SMFフォーマット
	int						m_track_num;		// Format1のトラック数（0は1固定）
	int						m_time_base;		// タイムベース
	int						m_duration;		// ミリ秒単位の時間
	int						m_port_num;		// ポート数
	int						port_index_;	// ポート番号

	// メタデータ
	char	m_title[METATEXT_MAXLEN + 1];
	char	m_copyright[METATEXT_MAXLEN + 1];
};

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
SmfLoader::SmfParser::SmfParser(const unsigned char* data, int size, const LoadOption& option)
	: raw_data_(data)
	, raw_size_(size)
	, option_(option)
	, m_reset_type(RESET_TYPE_GM1)
	, m_smf_format(SMF_FORMAT_0)
	, m_track_num(0)
	, m_time_base(1)
	, m_duration(0)
	, m_port_num(1)
	, port_index_(0)
{
	memset(m_title, 0, sizeof(m_title));
	memset(m_copyright, 0, sizeof(m_copyright));
}

//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
SmfLoader::SmfParser::~SmfParser()
{
}

//-----------------------------------------------------------------------------
// SMFデータを解析する
//-----------------------------------------------------------------------------
void SmfLoader::SmfParser::ParseSmf()
{
	const unsigned char* data = raw_data_;
	int size = raw_size_;
	int used = 0;

	// ヘッダを解析する
	if (!ParseHeader(data, size, used)) {
		return;
	}

	data += used;
	size -= used;

	for (int i = 0; (i < m_track_num) && (0 < size); ++i) {
		used = 0;

		// 各トラックを解析する
		if (!ParseTrack(data, size, used)) {
			return;
		}

		data += used;
		size -= used;
	}

	// 時間基準でソートするファンクタ
	struct SortFunctor
	{
		bool operator()( const MidiMessage& lhs, const MidiMessage& rhs) const
		{
			return (lhs.time < rhs.time);
		}
	};

	// 安定ソートにて、絶対時間順に並べる
	std::stable_sort(messages_.begin(), messages_.end(), SortFunctor());
}

//-----------------------------------------------------------------------------
// MIDI/MThd解析
//-----------------------------------------------------------------------------
bool SmfLoader::SmfParser::ParseHeader(const unsigned char* data, int size, int& used)
{
	// "MThd"確認
	if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd') {
		return false;
	}

	data += 4;

	// ヘッダのサイズ分データが無い場合は、不正なデータとし、解析しない
	int header_size = static_cast<int>(Swap32(data));
	if (header_size < 6) {
		return false;
	}

	header_size -= 6;
	data += 4;

	m_smf_format = static_cast<SmfFormat>(Swap16(data));
	data += 2;

	// SMFフォーマット0,1のみ対応（2は未対応）
	if ((m_smf_format != SMF_FORMAT_0) && (m_smf_format != SMF_FORMAT_1)) {
		return false;
	}

	m_track_num = Swap16(data);
	data += 2;
	m_time_base = Swap16(data);

	// 追加のヘッダ領域を無視する
	used += (header_size + 14);

	// 初期テンポ設定
	tempo_map_.clear();
	tempo_map_[0] = 120;
	return true;
}

//-----------------------------------------------------------------------------
// MIDI/MTrk解析
//-----------------------------------------------------------------------------
bool SmfLoader::SmfParser::ParseTrack(const unsigned char* data, int size, int& used)
{
	// "MTrk"確認
	if (data[0] != 'M' || data[1] != 'T' || data[2] != 'r' || data[3] != 'k') {
		return false;
	}

	data += 4;

	// トラックのサイズ分データが無い場合は、不正なデータとし、解析しない
	int track_size = static_cast<int>(Swap32(data));
	if (size < track_size) {
		return false;
	}

	data += 4;
	used += 8;

	port_index_ = 0;

	int delta_time = 0;
	int prev_message = 0;
	int result = 0;

	// 演奏データを取得
	while (0 < track_size) {
		// デルタタイム取得
		delta_time += ParseNumber(data, result);
		if (track_size < result) {
			data += track_size;
			used += track_size;
			track_size = 0;
			return true;
		}

		track_size -= result;
		data += result;
		used += result;

		// システムエクスクルーシブを処理
		result = ParseExMessage(data);
		if (0 < result) {
			prev_message = 0;
			track_size -= result;
			data += result;
			used += result;
			continue;
		}

		// メタイベントを処理
		result = ParseMetaEvent(data, delta_time);
		// トラック終了検出（ここまで処理しないといけない）
		if (result == END_OF_TRACK) {
			int play_time = CalcPlayTime(delta_time);
			MidiMessage msg = {MESSAGE_TYPE_END_OF_TRACK, play_time, END_OF_TRACK, port_index_, NULL};

			// 最後のメッセージを追加
			messages_.push_back(msg);

			// 曲長を更新
			if (m_duration < play_time) {
				m_duration = play_time;
			}

			track_size -= 3;
			data += 3;
			used += 3;
			return true;
		}
		else if (0 < result) {
			track_size -= result;
			data += result;
			used += result;
			continue;
		}

		result = 0;
		bool ignore = false;
		int message = ParseMessage(data, result, prev_message, ignore);

		track_size -= result;
		data += result;
		used += result;

		// 絶対時間を計算する
		int play_time = CalcPlayTime(delta_time);
		MidiMessage msg = {MESSAGE_TYPE_NOTE, play_time, message, port_index_, NULL};

		if (message != 0) {
			if (!ignore) {
				messages_.push_back(msg);
			}

			prev_message = message;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// システムエクスクルーシブ解析
//-----------------------------------------------------------------------------
int SmfLoader::SmfParser::ParseExMessage(const unsigned char* data)
{
	// システムエクスクルーシブでない場合は無視
	if (data[0] != 0xF0 && data[0] != 0xF7) {
		return 0;
	}

	int used = 0;
	int msg_size = ParseNumber(&data[1], used);

	// 音源リセットタイプを判定する
	if (option_.detect_reset_type) {
		struct ResetItem
		{
			const unsigned char*	reset_data;
			int						reset_size;
			ResetType				reset_type;
		};

		static const ResetItem reset_list[] =
		{
			{ &DATA_GM1[1], sizeof(DATA_GM1) - 1, RESET_TYPE_GM1 },
			{ &DATA_GM2[1], sizeof(DATA_GM2) - 1, RESET_TYPE_GM2 },
			{ &DATA_XG [1], sizeof(DATA_XG ) - 1, RESET_TYPE_XG  },
			{ &DATA_MU0[1], sizeof(DATA_MU0) - 1, RESET_TYPE_XG  },
			{ &DATA_MU1[1], sizeof(DATA_MU1) - 1, RESET_TYPE_XG  },
			{ &DATA_GS [1], sizeof(DATA_GS ) - 1, RESET_TYPE_GS  },
			{ &DATA_88S[1], sizeof(DATA_88S) - 1, RESET_TYPE_GS  },
			{ &DATA_88D[1], sizeof(DATA_88D) - 1, RESET_TYPE_GS  },
		};

		for (int i = 0; i < (sizeof(reset_list) / sizeof(reset_list[0])); ++i) {
			if (reset_list[i].reset_size == msg_size) {
				if (memcmp(&data[2], reset_list[i].reset_data, msg_size) == 0) {
					m_reset_type = reset_list[i].reset_type;
					break;
				}
			}
		}
	}

	return int((data[0] == 0xF0)? 2 : 1) + msg_size + (used - 1);
}

//-----------------------------------------------------------------------------
// メタイベントなど、演奏に関係ないデータを解析
//-----------------------------------------------------------------------------
int SmfLoader::SmfParser::ParseMetaEvent(const unsigned char* data, int delta_time)
{
	if (data[0] != 0xFF) {
		return 0;
	}

	int event_type = data[1];

	// エンドオブトラックの場合は、END_OF_TRACKを返す
	if (event_type == 0x2F) {
		return END_OF_TRACK;
	}

	int used = 0;
	int event_size = ParseNumber(&data[2], used);

	switch (event_type) {
	case 0x01:	// テキストイベント
	case 0x03:	// シーケンス名／トラック名
		if (m_title[0] == '\0') {
			int copy_size = std::min<int>(event_size, sizeof(m_title) - 1);
			memcpy(m_title, &data[used + 2], copy_size);
			TrimBlank(m_title);
		}
		break;

	case 0x02:	// 著作者
		if (m_copyright[0] == '\0') {
			int copy_size = std::min<int>(event_size, sizeof(m_copyright) - 1);
			memcpy(m_copyright, &data[used + 2], copy_size);
			TrimBlank(m_copyright);
		}
		break;

	case 0x21:	// ポート指定
		port_index_ = data[used + 2];
		if (m_port_num <= port_index_) {
			m_port_num = port_index_ + 1;
		}
		break;

	case 0x51:	// テンポ変更
		{
			int tttttt = Swap24(&data[used + 2]);
			int tempo = Mul64Div32(60, 1000000, tttttt);
			tempo_map_[delta_time] = tempo;
		}
		break;

	case 0x04:	// 楽器名
	case 0x05:	// 歌詞
	case 0x06:	// マーカー
		break;
	}

	// サイズのバイト数、データのバイト数、先頭２バイトの合計を返す
	return (event_size + used + 2);
}

//-----------------------------------------------------------------------------
// 現在のデルタタイムに対する絶対的な再生時間を計算する
//-----------------------------------------------------------------------------
int SmfLoader::SmfParser::CalcPlayTime(int delta_time) const
{
	int play_time = 0, dtime = 0, ptime = 0, tempo = 0;
	for (TempoMap::const_iterator it = tempo_map_.begin(); it != tempo_map_.end(); ++it) {
		// デルタタイムが過ぎている場合は、ループを抜ける
		if (delta_time < it->first) {
			break;
		}

		dtime = it->first - ptime;
		if (0 < dtime) {
			play_time += Mul64Div32(dtime, 60 * 1000, tempo * m_time_base);
		}

		ptime = it->first;
		tempo = it->second;
	}

	dtime = delta_time - ptime;
	play_time += Mul64Div32(dtime, 60 * 1000, tempo * m_time_base);
	return play_time;
}

//-----------------------------------------------------------------------------
// 可変長表現で示される数値を取得する
//-----------------------------------------------------------------------------
int SmfLoader::SmfParser::ParseNumber(const unsigned char* data, int& used)
{
	int value = 0;
	used = 0;

	for (int i = 0; i < 4; ++i) {
		value <<= 7;
		value |= int(data[i] & 0x7F);
		++used;

		if (!(data[i] & 0x80)) {
			break;
		}
	}

	return value;
}

//-----------------------------------------------------------------------------
// MIDIメッセージを取得する
//-----------------------------------------------------------------------------
int SmfLoader::SmfParser::ParseMessage(const unsigned char* data, int& used, int prev_message, bool& ignore)
{
	unsigned char temp = data[0];

	// ランニングステータス対応
	if ((temp & 0x80) == 0) {
		temp = (prev_message & 0xFF);
		used = 0;
	}
	else {
		used = 1;
	}

	int message = temp;

	switch (temp & 0xF0) {
	default:
		message = 0;
		break;

	case 0x80:	// ノートＯＮ
	case 0x90:	// ノートＯＦＦ
	case 0xA0:	// ポリフォニックキープレッシャー
	case 0xB0:	// コントロールチェンジ
	case 0xE0:	// ピッチベンドチェンジ
		message |= ((int(data[used]) << 8) | (int(data[used + 1]) << 16));
		used += 2;
		break;

	case 0xC0:	// プログラムチェンジ
	case 0xD0:	// チャネルプレッシャー
		message |= (int(data[used]) << 8);
		used += 1;
		break;
	}

	// バンクセレクトを無視(0x00:bank select MSB/0x20:bank select LSB)
	if (option_.ignore_bank_select) {
		int type = (message & 0xF0);
		int mval = ((message >> 8) & 0xFF);

		if ((type == 0xB0) && ((mval == 0x00) || (mval == 0x20))) {
			ignore = true;
		}
	}

	return message;
}


//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
SmfLoader::SmfLoader()
	: m_message_data(NULL)
	, m_message_num(0)
	, m_reset_type(RESET_TYPE_GM1)
	, m_smf_format(SMF_FORMAT_0)
	, m_track_num(0)
	, m_time_base(1)
	, m_duration(0)
	, m_port_num(1)
{
	memset(m_title, 0, sizeof(m_title));
	memset(m_copyright, 0, sizeof(m_copyright));
}

//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
SmfLoader::~SmfLoader()
{
	Clear();
}

//-----------------------------------------------------------------------------
//! @brief	SMFデータをファイルから読み込む
//-----------------------------------------------------------------------------
bool SmfLoader::Load(const wchar_t* path, const LoadOption& option)
{
	HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD size = GetFileSize(file, NULL);

	// 最低、以下のバイト数ないときは、エラーとする
	const DWORD REQUIRE_BYTES = 256;
	if (size < REQUIRE_BYTES) {
		CloseHandle(file);
		return false;
	}

	void* buffer = HeapAlloc(GetProcessHeap(), 0, size);
	if (!buffer) {
		CloseHandle(file);
		return false;
	}

	DWORD readed = 0;
	ReadFile(file, buffer, size, &readed, NULL);
	CloseHandle(file);

	if (readed != size) {
		HeapFree(GetProcessHeap(), 0, buffer);
		return false;
	}

	bool result = Load(buffer, size, option);
	HeapFree(GetProcessHeap(), 0, buffer);

	return result;
}

//-----------------------------------------------------------------------------
// SMFデータをメモリ上から読み込む
//-----------------------------------------------------------------------------
bool SmfLoader::Load(const void* data, int size, const LoadOption& option)
{
	Clear();

	SmfParser parser(static_cast<const unsigned char*>(data), size, option);
	parser.ParseSmf();

	const SmfParser::MidiMessageVector& msg = parser.GetMidiMessageData();
	if (msg.empty()) {
		return false;
	}

	m_message_num = static_cast<int>(msg.size());
	m_message_data = new MidiMessage[m_message_num];
	if (!m_message_data) {
		Clear();
		return false;
	}

	for (int i = 0; i < m_message_num; ++i) {
		m_message_data[i] = msg[i];
	}

	m_reset_type = parser.GetResetType();
	m_smf_format = parser.GetSmfFormat();
	m_track_num = parser.GetTrackNum();
	m_time_base = parser.GetTimeBase();
	m_duration = parser.GetDuration();
	m_port_num = parser.GetPortNum();

	parser.GetTitle(m_title);
	parser.GetCopyright(m_copyright);
	return true;
}

//-----------------------------------------------------------------------------
// 読み込んだデータをクリアする
//-----------------------------------------------------------------------------
void SmfLoader::Clear()
{
	for (int i = 0; i < m_message_num; ++i) {
		if (m_message_data[i].sys_ex) {
			delete m_message_data[i].sys_ex;
			m_message_data[i].sys_ex = NULL;
		}
	}

	delete [] m_message_data;
	m_message_data = NULL;
	m_message_num = 0;

	m_reset_type = RESET_TYPE_GM1;
	m_smf_format = SMF_FORMAT_0;
	m_track_num = 0;
	m_time_base = 1;
	m_duration = 0;

	memset(m_title, 0, sizeof(m_title));
	memset(m_copyright, 0, sizeof(m_copyright));
}

//-----------------------------------------------------------------------------
// 再生するMIDIメッセージ数取得
//-----------------------------------------------------------------------------
int SmfLoader::GetMidiMessageNum() const
{
	return m_message_num;
}

//-----------------------------------------------------------------------------
// 指定インデックスのMIDIメッセージ取得
//-----------------------------------------------------------------------------
const SmfLoader::MidiMessage& SmfLoader::GetMidiMessage(int index) const
{
	if (index < m_message_num) {
		return m_message_data[index];
	}

	static MidiMessage dummy_message;
	return dummy_message;
}

//-----------------------------------------------------------------------------
// 音源リセットメッセージ取得
//-----------------------------------------------------------------------------
const void* SmfLoader::GetResetMessageData() const
{
	const void* data = DATA_GM1;
	switch (m_reset_type) {
	case RESET_TYPE_GM1: data = DATA_GM1; break;
	case RESET_TYPE_GM2: data = DATA_GM2; break;
	case RESET_TYPE_XG:  data = DATA_XG;  break;
	case RESET_TYPE_GS:  data = DATA_GS;  break;
	}

	return data;
}

//-----------------------------------------------------------------------------
// 音源リセットメッセージのバイト数取得
//-----------------------------------------------------------------------------
int SmfLoader::GetResetMessageSize() const
{
	int size = sizeof(DATA_GM1);
	switch (m_reset_type) {
	case RESET_TYPE_GM1: size = sizeof(DATA_GM1); break;
	case RESET_TYPE_GM2: size = sizeof(DATA_GM2); break;
	case RESET_TYPE_XG:  size = sizeof(DATA_XG);  break;
	case RESET_TYPE_GS:  size = sizeof(DATA_GS);  break;
	}

	return size;
}

//-----------------------------------------------------------------------------
// 音源リセットタイプを取得
//-----------------------------------------------------------------------------
SmfLoader::ResetType SmfLoader::GetResetType() const
{
	return m_reset_type;
}

//-----------------------------------------------------------------------------
// SMFフォーマット取得
//-----------------------------------------------------------------------------
SmfLoader::SmfFormat SmfLoader::GetSmfFormat() const
{
	return m_smf_format;
}

//-----------------------------------------------------------------------------
// トラック数取得
//-----------------------------------------------------------------------------
int SmfLoader::GetTrackNum() const
{
	return m_track_num;
}

//-----------------------------------------------------------------------------
// タイムベース（分解能）取得
//-----------------------------------------------------------------------------
int SmfLoader::GetTimeBase() const
{
	return m_time_base;
}

//-----------------------------------------------------------------------------
// 曲の長さ取得
//-----------------------------------------------------------------------------
int SmfLoader::GetDuration() const
{
	return m_duration;
}

//-----------------------------------------------------------------------------
// メタデータに設定されたタイトル取得
//-----------------------------------------------------------------------------
const char* SmfLoader::GetTitle() const
{
	return m_title;
}

//-----------------------------------------------------------------------------
// メタデータに設定された権利情報取得
//-----------------------------------------------------------------------------
const char* SmfLoader::GetCopyright() const
{
	return m_copyright;
}
