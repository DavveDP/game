#pragma once
#include <game.h>
#include <vulkan_alloc_gpu.h>

// TODO: Current Physical device should have its own arena 
// that can be reset on device swap
typedef struct PhysicalDevice {
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties props_memory;
    //VkFormatProperties props_format;
    u32 present_index;

    gpu_arena_pool_t gpu_arena_pool; // one for each props.memoryTypes

    struct {
        u32 index;
        VkQueueFamilyProperties props;
    } qf_graphics;

    struct {
        u32 index;
        VkQueueFamilyProperties props;
    } qf_present;
    VkPresentModeKHR present_mode;

    struct {
        u32 count;
        VkSurfaceFormatKHR* items;
        VkSurfaceFormatKHR selected;
    } formats;

    VkSurfaceCapabilitiesKHR surface_capabilities;
} PhysicalDevice;

typedef struct Swapchain {
    VkSwapchainKHR handle;
    VkImage* images;
    VkImageView* image_views;
    VkFramebuffer* framebuffers;
    u32 image_count;

    // screen-size attachments - same lifetime as swapchain
    VkImage image_depth_buffer;
    VkImageView image_view_depth_buffer;
    gpu_arena_alloc_t gpu_mem_depth_buffer;
    gpu_arena_t arena_depth_buf;
} Swapchain;

typedef struct LogicalDevice {
    VkDevice handle;
    struct {
        VkQueue graphics;
        VkQueue present;
    } queue_handles;
    struct {
        Swapchain* current;
        gpu_arena_t arena_depth_buf;
        block_alloc_t allocator;
    } swapchain;
} LogicalDevice;

typedef struct {
    // indexed by frame
    VkCommandBuffer* cmd_buffers_graphics;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;

    VkRenderPass render_pass_main;
    VkQueryPool query_pool_timestamp;
    VkCommandPool cmd_pool_graphics_auto_reset;
    VkCommandPool cmd_pool_graphics_transient;
    u32 frames_in_flight;
    u32 image_count;
    u32 curr_image_index;
    u32 frame;
    u64 frame_count;
    bool recreate_swapchain;
    u8 swapchain_cooldown;
    float curr_render_time_ms;
} render_state_t;

typedef struct VulkanCtx {
    VkInstance instance;
    struct {
        PhysicalDevice* selected;
        PhysicalDevice* all;
        u32 count;
    } physical_device;

    LogicalDevice device;
    VkSurfaceKHR surface;
    // pools
    VkDescriptorPool descriptor_pool_permanent;
    // Heaps
    gpu_buffer_arena_t arena_ubo_ssbo;
    gpu_buffer_arena_t arena_staging;
    gpu_buffer_arena_t arena_local_mesh;

    // frame-by-frame state
    render_state_t render_state;
} VulkanCtx;

u32 window_height(VulkanCtx* ctx) {
    return ctx->physical_device.selected->surface_capabilities.currentExtent.height;
}
u32 window_width(VulkanCtx* ctx) {
    return ctx->physical_device.selected->surface_capabilities.currentExtent.width;
}

typedef enum {
    TERRAIN_PIPELINE_SHADED,
    TERRAIN_PIPELINE_WIREFRAME,
    TERRAIN_PIPELINE_TYPE_COUNT
} TERRAIN_PIPELINE_TYPE;

typedef struct {
    gpu_buffer_alloc_t gpu_mem_vertices;
    u32 n_vertices;
    gpu_buffer_alloc_t gpu_mem_indices[9];
    u32 n_indices[9];

    VkPipeline* pipelines;
    VkPipelineLayout* pipeline_layouts;
    u32 pipeline_count;

    VkDescriptorSetLayout descriptor_set_layout;
    // offset per frame 
    VkDescriptorSet* descriptor_sets;
    gpu_buffer_alloc_t* ssbo_instance_data;
} terrain_gpu_t;

typedef struct {
    VulkanCtx ctx;
    // global uniforms
    VkDescriptorSet* ubo_global_descriptor_sets;
    VkDescriptorSetLayout ubo_global_descriptor_set_layout;
    gpu_buffer_alloc_t* ubo_global;
    // terrain
    terrain_gpu_t terrain;
} vulkan_state_t;

// Files

typedef struct {
    u8* data;
    size_t size;
} file_data_t;

typedef struct {
    vulkan_state_t* (*init_rendering)(
            arena_t* arena_permanent, 
            arena_t* arena_scratch, 
            VkResult (*surface_factory)(VkInstance instance, VkSurfaceKHR* surface_out),
            const char** enabledInstanceExtensionNames,
            u32 n_enabledInstanceExtensionNames,
            file_data_t vertex_shader,
            file_data_t fragment_shader);
    void (*render)(vulkan_state_t* vulkan_state, game_state_t* game_state, float time);
} render_api_t;

typedef struct staging_buffer_upload_t {
    VkBufferCopy copyRegion;
    VkBuffer src, dst;
} staging_buffer_upload_t;

void upload_staging_buffer(VulkanCtx* ctx, staging_buffer_upload_t* uploadInfo) {
    VkCommandBufferAllocateInfo copyBufInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->render_state.cmd_pool_graphics_transient,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    // copyRegion, src, dst
    VkCommandBuffer copyBuf;
    VkResult res = vkAllocateCommandBuffers(ctx->device.handle, &copyBufInfo, &copyBuf);
    assert(res == VK_SUCCESS);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    res = vkBeginCommandBuffer(copyBuf, &beginInfo);
    assert(res == VK_SUCCESS);

    vkCmdCopyBuffer(copyBuf, uploadInfo->src, uploadInfo->dst, 1, &uploadInfo->copyRegion);
    res = vkEndCommandBuffer(copyBuf);
    assert(res == VK_SUCCESS);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &copyBuf
    };
    res = vkQueueSubmit(ctx->device.queue_handles.graphics, 1, &submitInfo, VK_NULL_HANDLE);
    assert(res == VK_SUCCESS);
    // TODO: add transfer fence maybe?
    vkQueueWaitIdle(ctx->device.queue_handles.graphics);
    assert(res == VK_SUCCESS);
}

VkShaderModule create_shader_module(VkDevice device, const u32* code, size_t codeSize, VkResult* res) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = code
    };
    VkShaderModule module;
    *res = vkCreateShaderModule(device, &info, NULL, &module);
    return module;
}

void allocate_descriptor_sets(arena_t* scratch, VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout, u32 count, VkDescriptorSet* out) {
    VkDescriptorSetLayout* layouts = alloc_array(scratch, VkDescriptorSetLayout, count);
    for (u32 i = 0; i < count; i++) layouts[i] = layout;
    VkDescriptorSetAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = count,
        .pSetLayouts = layouts
    };
    VkResult res = vkAllocateDescriptorSets(device, &info, out);
    assert(res == VK_SUCCESS);
}
