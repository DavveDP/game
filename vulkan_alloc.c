typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    u64 size;
    u64 used;
} gpu_heap_t;

typedef struct {
    VkDeviceMemory memory;
    u64 offset;
} gpu_alloc_t;

u32 find_memory_type(VkPhysicalDeviceMemoryProperties* phys_dev_mem_props, u32 memory_type_bits, VkMemoryPropertyFlags properties) {
    for (u32 i = 0; i < phys_dev_mem_props->memoryTypeCount; i++) {
        if ((memory_type_bits & (1 << i)) &&
            (phys_dev_mem_props->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    assert(0); // no compatible type found
    return -1;
}

gpu_heap_t gpu_heap_create(VkDevice device, u64 size, VkPhysicalDeviceMemoryProperties* phys_dev_mem_props, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBuffer buffer;
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(device, &info, NULL, &buffer);
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, buffer, &reqs);
    u32 memory_type_index = find_memory_type(phys_dev_mem_props, reqs.memoryTypeBits, properties);
    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index
    };
    gpu_heap_t heap = { .size = size, .memory_type_index = memory_type_index };
    vkAllocateMemory(device, &info, NULL, &heap.memory);
    return heap;
}

gpu_alloc_t gpu_heap_alloc(gpu_heap_t* heap, VkMemoryRequirements reqs) {
    assert(reqs.memoryTypeBits & (1 << heap->memory_type_index)); // compatible
    u64 aligned = align_up(heap->used, alignment);
    assert(aligned + size <= heap->size);
    heap->used = aligned + size;
    return (gpu_alloc_t) { heap->memory, aligned, size };
}
