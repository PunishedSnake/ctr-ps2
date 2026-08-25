#include <platform.h>
#include "ctr_scratchpad.h"
#include "platform/native_memory.h"
#if defined(CTR_INTERNAL)
#include "platform/native_checkpoint.h"
#endif

#include <common.h>
#include <macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Keep retail PS1 memory pressure as the default correctness baseline, but
// allow native builds to expose a larger arena. This is intentionally a
// platform policy switch: the game-facing MEMPACK allocator and its lifetime
// semantics remain unchanged.
#ifndef CTR_NATIVE_MEMPACK_RETAIL_PRESSURE
#define CTR_NATIVE_MEMPACK_RETAIL_PRESSURE 1
#endif

#if CTR_NATIVE_MEMPACK_RETAIL_PRESSURE
// NTSC-U 926 retail window inside the PS1 2 MiB address space.
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x200000u
#define CTR_NATIVE_MEMPACK_START_OFFSET 0xba9f0u
#define CTR_NATIVE_MEMPACK_SIZE         0x144e10u
#else
// Expanded native arena used to remove the PS1 main-RAM capacity limit while
// preserving MEMPACK's low/high allocation, bookmark and clear semantics.
// 8 MiB is deliberately conservative: large enough to expose capacity-bound
// assumptions without turning the compatibility allocator into an unbounded
// desktop heap.
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x800000u
#define CTR_NATIVE_MEMPACK_START_OFFSET 0x000000u
#define CTR_NATIVE_MEMPACK_SIZE         CTR_NATIVE_MEMPACK_BUFFER_SIZE
#endif

#if CTR_NATIVE_MEMPACK_RETAIL_PRESSURE
CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE + MEMPACK_PS1_END_GUARD_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);
#else
CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);
#endif

union NativeScratchpadStorage
{
	u8 bytes[CTR_SCRATCHPAD_SIZE];
	u32 words[CTR_SCRATCHPAD_SIZE / sizeof(u32)];
};

CTR_STATIC_ASSERT(sizeof(union NativeScratchpadStorage) == CTR_SCRATCHPAD_SIZE);

global_variable char s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
global_variable struct PlatformMempackArena s_mempackArena;
global_variable union NativeScratchpadStorage s_scratchpadMemory;
u8 *gCTRNativeScratchpadBase;

void Platform_InitScratchpad(void)
{
#if defined(CTR_NATIVE)
	gCTRNativeScratchpadBase = &s_scratchpadMemory.bytes[0];
	memset(&s_scratchpadMemory, 0, sizeof(s_scratchpadMemory));
#endif
}

void Platform_ConfigureMempackArena(void)
{
	s_mempackArena.base = &s_mempackMemory[0];
	s_mempackArena.start = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET];
	s_mempackArena.endOfMemory = &s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
	s_mempackArena.size = CTR_NATIVE_MEMPACK_SIZE;
	s_mempackArena.backingSize = CTR_NATIVE_MEMPACK_BUFFER_SIZE;
}

const struct PlatformMempackArena *Platform_InitMempackArena(void)
{
	memset(s_mempackMemory, 0, sizeof(s_mempackMemory));
	Platform_ConfigureMempackArena();
#if defined(CTR_INTERNAL)
	NativeCheckpoint_OnMempackArenaReset();
#endif

	printf("[CTR] MEMPACK pressure mode: %s\n", CTR_NATIVE_MEMPACK_RETAIL_PRESSURE ? "retail-PS1" : "expanded-native");
	return &s_mempackArena;
}

const struct PlatformMempackArena *Platform_GetMempackArena(void)
{
	return &s_mempackArena;
}

void *Platform_GetMempackBacking(void)
{
	return &s_mempackMemory[0];
}

int Platform_GetMempackBackingSize(void)
{
	return (int)sizeof(s_mempackMemory);
}

void Platform_RepairResidentPointers(s32 activeMempackIndex)
{
	if ((activeMempackIndex < 0) || (activeMempackIndex >= 4))
	{
		activeMempackIndex = 0;
	}

	// NOTE(aalhendi): Native keeps retail-shaped global data, but pointer aliases
	// must target this process's static storage. This also moves GCC's
	// initializer-only memcard helper global out of the live state graph so
	// checkpoints capture the actual memcard buffer.
	sdata = &sdata_static;
	sdata_static.gGT = &sdata_static.gameTracker;
	sdata_static.gGamepads = &sdata_static.gamepadSystem;
	sdata_static.PtrMempack = &sdata_static.mempack[activeMempackIndex];
	sdata_static.ptrToMemcardBuffer1 = &sdata_static.memcardBytes[0];
	sdata_static.ptrToMemcardBuffer2 = &sdata_static.memcardBytes[0];
}
