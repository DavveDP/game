typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    u64 required_device_alignment;
    u64 size;
    u64 used;
    void* mapped; // NULL for device-local heaps
} gpu_heap_t;

typedef struct {
    gpu_heap_t* heap;
    void* mapped;
    u64 offset;
} gpu_alloc_t;

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

gpu_heap_t gpu_heap_create(
        VkDevice device,
        VkPhysicalDevice phys_device,
        VkPhysicalDeviceMemoryProperties* mem_props,
        u64 size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties)
{
    gpu_heap_t heap = { .size = size };

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(device, &buf_info, NULL, &heap.buffer);

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, heap.buffer, &reqs);
    heap.required_device_alignment = reqs.alignment;

    VkMemoryAllocateInfo mem_info = {  
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = reqs.size, 
        .memoryTypeIndex = find_memory_type(phys_device, mem_props, reqs.memoryTypeBits, properties)
    };
    vkAllocateMemory(device, &mem_info, NULL, &heap.memory);
    vkBindBufferMemory(device, heap.buffer, heap.memory, 0);

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(device, heap.memory, 0, VK_WHOLE_SIZE, 0, &heap.mapped);
    }

    return heap;
}

void gpu_heap_reset(gpu_heap_t* heap) {
    heap->used = 0;
}

gpu_alloc_t gpu_heap_alloc(gpu_heap_t* heap, u64 size) {
    u64 offset_aligned = align_up(heap->used, heap->required_device_alignment);
    assert(offset_aligned + size <= heap->size);
    heap->used = offset_aligned + size;
    return (gpu_alloc_t){ 
        .heap = heap, 
        .mapped = (void*)((u8*)(heap->mapped) + offset_aligned), 
        .offset = offset_aligned };
}

gpu_alloc_t gpu_heap_alloc_unaligned(gpu_heap_t* heap, u64 size) {
    gpu_alloc_t result = { 
        .heap = heap, 
        .mapped = (void*)((u8*)(heap->mapped) + heap->used), 
        .offset = heap->used
    };
    heap->used += size;
    return result;
}
