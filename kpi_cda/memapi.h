//=============================================================================
// Kerenl memory functions.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#pragma once

// 既存定義を解除する
#ifdef RtlZeroMemory
#undef RtlZeroMemory
#endif

#ifdef RtlMoveMemory
#undef RtlMoveMemory
#endif

#ifdef RtlFillMemory
#undef RtlFillMemory
#endif

#ifdef ZeroMemory
#undef ZeroMemory
#endif

#ifdef CopyMemory
#undef CopyMemory
#endif

#ifdef MoveMemory
#undef MoveMemory
#endif

#ifdef FillMemory
#undef FillMemory
#endif


#ifdef __cplusplus
extern "C"
{
#endif

	// APIのインポート定義する
NTSYSAPI void NTAPI RtlZeroMemory(void* dest, size_t length);
NTSYSAPI void NTAPI RtlMoveMemory(void* dest, const void* src, size_t length);
NTSYSAPI void NTAPI RtlFillMemory(void* dest, size_t length, BYTE value);

#ifdef __cplusplus
}
#endif


// 通常APIの定義を行う
#define ZeroMemory RtlZeroMemory
#define CopyMemory RtlMoveMemory
#define MoveMemory RtlMoveMemory
#define FillMemory RtlFillMemory

// メモリ確保
inline void* AllocMemory(size_t size)
{
	return HeapAlloc(GetProcessHeap(), 0, size);
}

// メモリ開放
inline void FreeMemory(void* ptr)
{
	HeapFree(GetProcessHeap(), 0, ptr);
}

// new/delete
inline void* operator new (size_t size)
{
	return AllocMemory(size);
}

inline void* operator new [](size_t size)
{
	return AllocMemory(size);
}

inline void operator delete (void* ptr)
{
	FreeMemory(ptr);
}

inline void operator delete [](void* ptr)
{
	FreeMemory(ptr);
}
