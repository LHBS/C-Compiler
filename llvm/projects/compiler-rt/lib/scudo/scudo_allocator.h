//===-- scudo_allocator.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Header for scudo_allocator.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ALLOCATOR_H_
#define SCUDO_ALLOCATOR_H_

#include "scudo_flags.h"

#include "sanitizer_common/sanitizer_allocator.h"

#if !SANITIZER_LINUX
# error "The Scudo hardened allocator is currently only supported on Linux."
#endif

namespace __scudo {

enum AllocType : u8 {
  FromMalloc    = 0, // Memory block came from malloc, realloc, calloc, etc.
  FromNew       = 1, // Memory block came from operator new.
  FromNewArray  = 2, // Memory block came from operator new [].
  FromMemalign  = 3, // Memory block came from memalign, posix_memalign, etc.
};

enum ChunkState : u8 {
  ChunkAvailable  = 0,
  ChunkAllocated  = 1,
  ChunkQuarantine = 2
};

// Our header requires 64 bits of storage. Having the offset saves us from
// using functions such as GetBlockBegin, that is fairly costly. Our first
// implementation used the MetaData as well, which offers the advantage of
// being stored away from the chunk itself, but accessing it was costly as
// well. The header will be atomically loaded and stored.
typedef u64 PackedHeader;
struct UnpackedHeader {
  u64 Checksum          : 16;
  u64 SizeOrUnusedBytes : 19; // Size for Primary backed allocations, amount of
                              // unused bytes in the chunk for Secondary ones.
  u64 FromPrimary       : 1;
  u64 State             : 2;  // available, allocated, or quarantined
  u64 AllocType         : 2;  // malloc, new, new[], or memalign
  u64 Offset            : 16; // Offset from the beginning of the backend
                              // allocation to the beginning of the chunk
                              // itself, in multiples of MinAlignment. See
                              // comment about its maximum value and in init().
  u64 Salt              : 8;
};

typedef atomic_uint64_t AtomicPackedHeader;
COMPILER_CHECK(sizeof(UnpackedHeader) == sizeof(PackedHeader));

// Minimum alignment of 8 bytes for 32-bit, 16 for 64-bit
const uptr MinAlignmentLog = FIRST_32_SECOND_64(3, 4);
const uptr MaxAlignmentLog = 24;  // 16 MB
const uptr MinAlignment = 1 << MinAlignmentLog;
const uptr MaxAlignment = 1 << MaxAlignmentLog;

const uptr ChunkHeaderSize = sizeof(PackedHeader);
const uptr AlignedChunkHeaderSize =
    (ChunkHeaderSize + MinAlignment - 1) & ~(MinAlignment - 1);

#if SANITIZER_CAN_USE_ALLOCATOR64
const uptr AllocatorSpace = ~0ULL;
const uptr AllocatorSize = 0x40000000000ULL;  // 4TB.
typedef DefaultSizeClassMap SizeClassMap;
struct AP {
  static const uptr kSpaceBeg = AllocatorSpace;
  static const uptr kSpaceSize = AllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef __scudo::SizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags =
      SizeClassAllocator64FlagMasks::kRandomShuffleChunks;
};
typedef SizeClassAllocator64<AP> PrimaryAllocator;
#else
// Currently, the 32-bit Sanitizer allocator has not yet benefited from all the
// security improvements brought to the 64-bit one. This makes the 32-bit
// version of Scudo slightly less toughened.
static const uptr RegionSizeLog = 20;
static const uptr NumRegions = SANITIZER_MMAP_RANGE_SIZE >> RegionSizeLog;
# if SANITIZER_WORDSIZE == 32
typedef FlatByteMap<NumRegions> ByteMap;
# elif SANITIZER_WORDSIZE == 64
typedef TwoLevelByteMap<(NumRegions >> 12), 1 << 12> ByteMap;
# endif  // SANITIZER_WORDSIZE
typedef DefaultSizeClassMap SizeClassMap;
typedef SizeClassAllocator32<0, SANITIZER_MMAP_RANGE_SIZE, 0, SizeClassMap,
    RegionSizeLog, ByteMap> PrimaryAllocator;
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

#include "scudo_allocator_secondary.h"

typedef SizeClassAllocatorLocalCache<PrimaryAllocator> AllocatorCache;
typedef ScudoLargeMmapAllocator SecondaryAllocator;
typedef CombinedAllocator<PrimaryAllocator, AllocatorCache, SecondaryAllocator>
    ScudoBackendAllocator;

void initScudo();

void *scudoMalloc(uptr Size, AllocType Type);
void scudoFree(void *Ptr, AllocType Type);
void scudoSizedFree(void *Ptr, uptr Size, AllocType Type);
void *scudoRealloc(void *Ptr, uptr Size);
void *scudoCalloc(uptr NMemB, uptr Size);
void *scudoMemalign(uptr Alignment, uptr Size);
void *scudoValloc(uptr Size);
void *scudoPvalloc(uptr Size);
int scudoPosixMemalign(void **MemPtr, uptr Alignment, uptr Size);
void *scudoAlignedAlloc(uptr Alignment, uptr Size);
uptr scudoMallocUsableSize(void *Ptr);

}  // namespace __scudo

#endif  // SCUDO_ALLOCATOR_H_
