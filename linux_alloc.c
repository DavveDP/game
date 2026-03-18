// Allows easy calls if allocators have the same signature
// - compile-time dispatch

// align ptr to mult, see Hacker's Delight (removed unary for portability reasons)
#define align_up(ptr, mult) (typeof(ptr))(((uintptr_t)(ptr) + (mult) - 1) & ~(uintptr_t)((mult)-1))
#define alloc_array(Alloc, T, N) \
    alloc_array_aligned(Alloc, T, N, alignof(T))
#define alloc_array_aligned(Alloc, T, N, A) \
    ((T*) _Generic((Alloc), \
            arena_t*: arena_alloc \
            )((Alloc), sizeof(T) * N, A))
#define alloc(Alloc, T) \
    alloc_array(Alloc, T, 1)

typedef struct arena_alloc_t {
    u8* origin;
    u8* curr;
    size_t size;
} arena_t;

typedef struct arena_alloc_mark_t {
    u8* ptr;
} arena_mark_t;

void arena_init(arena_t* a, u8* origin, size_t size) {
    // this assumption makes our lives so much easier
    assert(align_up(origin, alignof(max_align_t)) == origin);
    a->origin = a->curr = origin;
    a->size = size;
}

void arena_reset(arena_t* a) {
    a->curr = a->origin;
}

void* arena_alloc(arena_t* a, size_t size, size_t alignment) {
    // alignment is power of two and >0
    u8* aligned = align_up(a->curr, alignment);
    u8* end = aligned + size;

    if (a->origin && end - a->origin > a->size) {
        assert(0); // crash for now
        return NULL;
    }
    a->curr = end;
    return a->origin ? aligned : NULL;
}

// marks
arena_mark_t arena_mark(arena_t* a) {
    return (arena_mark_t){ .ptr = a->curr};
}

void arena_reset_to(arena_t* a, arena_mark_t mark) {
    a->curr = mark.ptr;
}

//TODO: bitset at start that says which blocks are free
// determine leftmost 0 or one and use that
//
// make sure bitset is aligned to a ulonglong address and 
// that the subsquent block will be aligned to max_align after that
//
// intended to be used with swapchain recreation. MAX_FRAMES_IN_FLIGHT + 1 blocks,
// containing all info necessary to destroy a swapchain. Then, after if a frame detects
// an old swapchain, it allocates a new one and adds the current one to a pending destroy array
// that is of size MAX_FRAMES_IN_FLIGHT. it adds it to index:
// (currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT, ie the previous fence
// then, once a frame has finished waiting on its fence, it can destroy a swapchain if there
// is something != NULL in pendingDestroy.

typedef struct block_alloc_t {
    u8* origin;
    size_t nextWordIndex;
    size_t blockCount;
    size_t blockSize;
} block_alloc_t;

static inline size_t bitmap_size(size_t blockCount) {
    return (blockCount + 31) / 32 * sizeof(u32);
}

static inline u8* block_region(block_alloc_t* a) {
    return align_up(a->origin + bitmap_size(a->blockCount), alignof(max_align_t));
}

size_t block_alloc_bytes_required(size_t blockSize, size_t blockCount) {
    size_t size = bitmap_size(blockCount);
    size = align_up(size, alignof(max_align_t));
    size += align_up(blockSize, alignof(max_align_t)) * blockCount;
    return size;
}

void block_alloc_reset(block_alloc_t* a) {
    memset(a->origin, 0xFF, bitmap_size(a->blockCount));
    a->nextWordIndex = 0;

    u32 rem = a->blockCount % 32;
    u32 lastWordMask = rem ? ((1u << rem) - 1) : 0xFFFFFFFF;

    u32* lastWord = (u32*)a->origin + ((a->blockCount + 31) / 32) - 1;
    *lastWord &= lastWordMask;
}

void block_alloc_init(block_alloc_t* a, u8* origin, size_t blockCount, size_t blockSize) {
    // these assumptions make our lives so much easier
    assert(blockCount > 0);
    assert((uintptr_t)origin % alignof(u32) == 0);
    assert(blockSize >= alignof(max_align_t));

    blockSize = align_up(blockSize, alignof(max_align_t)); 
 
    a->origin = origin;
    a->blockCount = blockCount;
    a->blockSize = blockSize;
    a->nextWordIndex = 0;
    block_alloc_reset(a);
}

void* block_alloc(block_alloc_t* a) {
    u32* bitmap = (u32*)a->origin;
    u8* blocks = block_region(a);

    size_t wordCount = (a->blockCount + 31) / 32;
    for (size_t i = a->nextWordIndex; i < wordCount; i++) {
        u32 word = bitmap[i];

        if (word) {
            u32 bit = __builtin_ctz(word);
            bitmap[i] &= ~(1u << bit);
            u32 blockIndex = i * 32 + bit;

            assert(blockIndex < a->blockCount);

            if (!bitmap[i]) 
                a->nextWordIndex = i + 1;
            return (void*)(blocks + blockIndex * a->blockSize);
        }
    }
    // should be unreachable
    assert(0);
    return NULL;
}

void block_alloc_free(block_alloc_t* a, void* ptr) {
    u32* bitmap = (u32*)a->origin;
    u8* blocks = block_region(a);
    uintptr_t block_ptr = (uintptr_t)ptr;
    uintptr_t offset = block_ptr - (uintptr_t)blocks;

    assert(block_ptr >= (uintptr_t)blocks);
    assert(block_ptr < (uintptr_t)blocks + a->blockCount * a->blockSize);
    assert(offset % a->blockSize == 0);

    u32 blockIndex = offset / a->blockSize;
    u32 wordOffset = blockIndex / 32;
    u32 bitOffset = blockIndex & 31;
    assert(!(bitmap[wordOffset] & (1 << bitOffset))); // check double free
    a->nextWordIndex = MIN(a->nextWordIndex, wordOffset);
    bitmap[wordOffset] |= (1 << bitOffset);
}
