typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    u64 required_device_alignment;
    u64 size;
    u64 used;
    void* mapped; // NULL for device-local arenas
} gpu_buffer_arena_t;

typedef struct {
    gpu_buffer_arena_t* arena;
    void* mapped;
    u64 offset;
} gpu_buffer_alloc_t;

u32 find_memory_type(VkPhysicalDevice phys_handle, VkPhysicalDeviceMemoryProperties* device_mem_props, u32 mem_type_bits, VkMemoryPropertyFlags req_mem_props) {
    for (int i = 0; i < device_mem_props->memoryTypeCount; i++) {
        if ((mem_type_bits & (1 << i)) && 
            (device_mem_props->memoryTypes[i].propertyFlags & req_mem_props) == req_mem_props) {
            return i;
        }
    }
    assert(0); // no compatible type found
    return -1;
}

gpu_buffer_arena_t gpu_buffer_arena_create(
        VkDevice device,
        VkPhysicalDevice phys_device,
        VkPhysicalDeviceMemoryProperties* mem_props,
        u64 size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties)
{
    gpu_buffer_arena_t arena = { .size = size };

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(device, &buf_info, NULL, &arena.buffer);

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, arena.buffer, &reqs);
    arena.required_device_alignment = reqs.alignment;

    VkMemoryAllocateInfo mem_info = {  
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = reqs.size, 
        .memoryTypeIndex = find_memory_type(phys_device, mem_props, reqs.memoryTypeBits, properties)
    };
    vkAllocateMemory(device, &mem_info, NULL, &arena.memory);
    vkBindBufferMemory(device, arena.buffer, arena.memory, 0);

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(device, arena.memory, 0, VK_WHOLE_SIZE, 0, &arena.mapped);
    }

    return arena;
}

void gpu_buffer_arena_reset(gpu_buffer_arena_t* arena) {
    arena->used = 0;
}

#define gpu_buffer_arena_alloc(H, size) (gpu_buffer_arena_alloc_aligned((H), (size), ((H)->required_device_alignment)))

gpu_buffer_alloc_t gpu_buffer_arena_alloc_aligned(gpu_buffer_arena_t* arena, u64 size, u64 alignment) {
    u64 offset_aligned = align_up(arena->used, alignment);
    assert(offset_aligned + size <= arena->size);
    arena->used = offset_aligned + size;
    return (gpu_buffer_alloc_t){ 
        .arena = arena, 
        .mapped = (void*)((u8*)(arena->mapped) + offset_aligned), 
        .offset = offset_aligned };
}

gpu_buffer_alloc_t gpu_buffer_arena_alloc_unaligned(gpu_buffer_arena_t* arena, u64 size) {
    gpu_buffer_alloc_t result = { 
        .arena = arena, 
        .mapped = (void*)((u8*)(arena->mapped) + arena->used), 
        .offset = arena->used
    };
    arena->used += size;
    return result;
}

typedef struct {
    VkDeviceMemory memory;
    u64 size;
    u64 used;
    u32 mem_type_index;
    u64 base_offset; // used for nested arenas
    void* mapped; // non-null if host-visible
} gpu_arena_t;

typedef struct {
    gpu_arena_t* arenas;
    u32 arena_count;
} gpu_arena_pool_t;

typedef struct {
    gpu_arena_t* arena; // could save mem by putting arena index here instead
    u64 size;
    u64 offset;
    void* mapped;
} gpu_arena_alloc_t;

gpu_arena_alloc_t gpu_arena_suballoc(gpu_arena_t* arena, u64 size, u64 alignment) {
    u64 offset_aligned = align_up(arena->used, alignment);
    assert(offset_aligned + size <= arena->size);
    arena->used = offset_aligned + size;
    return (gpu_arena_alloc_t){ 
        .arena = arena, 
        .mapped = arena->mapped ? (void*)((u8*)(arena->mapped) + offset_aligned) : NULL,
        .offset = arena->base_offset + offset_aligned };
}

void gpu_arena_init(gpu_arena_pool_t* pool, VkDevice device, VkPhysicalDeviceMemoryProperties* device_mem_props, VkMemoryRequirements* mem_reqs, VkMemoryPropertyFlags req_mem_props, VkDeviceSize arena_size) {

    for (u32 i = 0; i < device_mem_props->memoryTypeCount; i++) {
        if (!(mem_reqs->memoryTypeBits & (1u << i))) continue; // check the type is correct
        if ((device_mem_props->memoryTypes[i].propertyFlags & req_mem_props) != req_mem_props) continue; // check the properties too
        
        gpu_arena_t* arena = &pool->arenas[i];

        assert(arena->memory == VK_NULL_HANDLE);
        VkMemoryAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = arena_size,
            .memoryTypeIndex = i
        };
        VkResult res = vkAllocateMemory(device, &info, NULL, &arena->memory);
        assert(res == VK_SUCCESS);

        arena->size = arena_size;
        arena->used = 0;
        arena->mem_type_index = i;

        // map once if host-visible
        if (req_mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            vkMapMemory(device, arena->memory, 0, VK_WHOLE_SIZE, 0, &arena->mapped);
        }
        return;
    }
    assert(0);
}

void gpu_arena_alloc_bind_image(gpu_arena_alloc_t* alloc, VkDevice device, VkImage image) {
    VkResult res = vkBindImageMemory(device, image, alloc->arena->memory, alloc->offset);
    assert(res == VK_SUCCESS);
}

// finds the right arena and asserts it to be initialized
gpu_arena_alloc_t gpu_arena_alloc(gpu_arena_pool_t* pool, VkPhysicalDeviceMemoryProperties* device_mem_props, VkMemoryRequirements* mem_reqs, VkMemoryPropertyFlags req_mem_props) {
    for (u32 i = 0; i < device_mem_props->memoryTypeCount; i++) {
        if (!(mem_reqs->memoryTypeBits & (1u << i))) continue; // check the type is correct
        if ((device_mem_props->memoryTypes[i].propertyFlags & req_mem_props) != req_mem_props) continue; // check the properties too
        
        gpu_arena_t* arena = &pool->arenas[i];

        assert(arena->memory != VK_NULL_HANDLE);
        return gpu_arena_suballoc(arena, mem_reqs->size, mem_reqs->alignment);
    }
    assert(0);
}

gpu_arena_t gpu_arena_alloc_subarena(gpu_arena_pool_t* pool, VkDevice device, VkPhysicalDeviceMemoryProperties* device_mem_props, VkMemoryRequirements* mem_reqs, VkMemoryPropertyFlags req_mem_props, VkDeviceSize subarena_size) {

    for (u32 i = 0; i < device_mem_props->memoryTypeCount; i++) {
        if (!(mem_reqs->memoryTypeBits & (1u << i))) continue; // check the type is correct
        if ((device_mem_props->memoryTypes[i].propertyFlags & req_mem_props) != req_mem_props) continue; // check the properties too

        gpu_arena_t* parent = &pool->arenas[i];

        assert(parent->memory != VK_NULL_HANDLE);

        u64 offset_aligned = align_up(parent->used, mem_reqs->alignment);
        assert(offset_aligned + subarena_size <= parent->size);
        parent->used = offset_aligned + subarena_size;
        return (gpu_arena_t){
            .memory = parent->memory,
                .size = subarena_size,
                .used = 0,
                .mem_type_index = i,
                .mapped = parent->mapped ? (u8*)parent->mapped + offset_aligned : NULL,
                .base_offset = offset_aligned
        };
    }
    assert(0);
}

void gpu_arena_reset(gpu_arena_t* arena) {
    arena->used = 0;
}
