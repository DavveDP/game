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
    u32 mem_type_bits;
} gpu_arena_t;

typedef struct {
    gpu_arena_t* arena;
    u64 size;
    u64 offset;
} gpu_arena_alloc_t;

gpu_arena_t gpu_arena_create(VkDevice device, VkPhysicalDevice phys_handle, VkPhysicalDeviceMemoryProperties* device_mem_props, VkMemoryPropertyFlags req_mem_props, VkMemoryRequirements* mem_reqs, u32 n_mem_reqs) {
    gpu_arena_t arena;
    u32 mask = 0;
    for (u32 i = 0; i < n_mem_reqs; i++) { 
        mask |= mem_reqs[i].memoryTypeBits; 
        arena.size = align_up(arena.size, mem_reqs[i].alignment);
        arena.size += mem_reqs[i].size;
    }
    VkMemoryAllocateInfo mem_info = {  
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = arena.size, 
        .memoryTypeIndex = find_memory_type(phys_handle, device_mem_props, mask, req_mem_props)
    };
    vkAllocateMemory(device, &mem_info, NULL, &arena.memory);
    return arena;
}

gpu_arena_alloc_t gpu_arena_alloc_image(gpu_arena_t* arena, VkDevice device, VkImage image) {
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, image, &mem_reqs);

    assert((mem_reqs.memoryTypeBits & arena->mem_type_bits) == mem_reqs.memoryTypeBits);
    u64 offset_aligned = align_up(arena->used, mem_reqs.alignment);
    assert(offset_aligned + mem_reqs.size <= arena->size);

    vkBindImageMemory(device, image, arena->memory, offset_aligned);
    arena->used = offset_aligned + mem_reqs.size;
    return (gpu_arena_alloc_t) {
        .arena = arena,
        .size = mem_reqs.size,
        .offset = offset_aligned
    };
}
