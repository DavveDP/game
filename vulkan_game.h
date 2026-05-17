typedef struct PhysicalDevice {
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties memory_props;
    u32 presentIndex;

    struct {
        u32 index;
        VkQueueFamilyProperties props;
    } graphicsQF;

    struct {
        u32 index;
        VkQueueFamilyProperties props;
    } presentQF;
    VkPresentModeKHR presentMode;

    struct {
        u32 count;
        VkSurfaceFormatKHR* items;
        VkSurfaceFormatKHR selected;
    } formats;

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
} PhysicalDevice;

typedef struct Swapchain {
    VkSwapchainKHR handle;
    VkImage* images;
    VkImageView* imageViews;
    VkFramebuffer* framebuffers;
    u32 imageCount;
} Swapchain;

typedef struct LogicalDevice {
    VkDevice handle;
    struct {
        VkQueue graphics;
        VkQueue present;
    } queueHandles;
    Swapchain* swapchain;
    block_alloc_t swapchain_allocator;
} LogicalDevice;

typedef struct {
    VkPipeline handle;
    VkPipelineLayout layout;
} pipeline_t;

typedef struct {
    // indexed by frame
    VkCommandBuffer* cmd_buffers_graphics;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;

    VkCommandPool cmd_pool_graphics_auto_reset;
    VkCommandPool cmd_pool_graphics_transient;
    u32 frames_in_flight;
    u32 image_count;
    u32 curr_image_index;
    u32 frame;
    bool recreate_swapchain;
    u8 swapchain_cooldown;
} render_state_t;

typedef struct VulkanCtx {
    VkInstance instance;
    struct {
        PhysicalDevice* selected;
        PhysicalDevice* all;
        u32 count;
    } physicalDevice;

    LogicalDevice device;
    VkSurfaceKHR surface;
    VkRenderPass renderPass;
    // pools
    VkDescriptorPool descriptor_pool_permanent;
    // Heaps
    gpu_heap_t heap_ubo_ssbo;
    gpu_heap_t heap_staging;
    gpu_heap_t heap_local_mesh;

    // frame-by-frame state
    render_state_t render_state;
} VulkanCtx;

u32 window_height(VulkanCtx* ctx) {
    return ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.height;
}
u32 window_width(VulkanCtx* ctx) {
    return ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.width;
}

typedef struct {
    gpu_alloc_t gpu_mem_vertices;
    u32 n_vertices;
    gpu_alloc_t gpu_mem_indices[9];
    u32 n_indices[9];

    pipeline_t pipeline;
    VkDescriptorSetLayout descriptor_set_layout;
    // offset per frame 
    VkDescriptorSet* descriptor_sets;
    gpu_alloc_t* ubo_noise;
    gpu_alloc_t* ssbo_instance_data;
} terrain_gpu_t;

typedef struct {
    u32 grid_size;
    float y_scale;

    //fbm
    float H;
    u32 octaves;
    // terrain uniform data
} UBO_terrain_noise_t;

typedef struct {
    VulkanCtx ctx;
    // global uniforms
    VkDescriptorSet* ubo_global_descriptor_sets;
    VkDescriptorSetLayout ubo_global_descriptor_set_layout;
    gpu_alloc_t* ubo_global;
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
            file_data_t vertex_shader,
            file_data_t fragment_shader);
    void (*render)(vulkan_state_t* vulkan_state, game_state_t* game_state, float time);
} render_api_t;
