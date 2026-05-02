const char* enabledInstanceExtensionNames[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME
};

const char* enabledDeviceExtensionNames[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

const char* enabledLayerNames[] = {
    "VK_LAYER_KHRONOS_validation"
};

typedef struct PhysicalDevice {
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties props;
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

size_t get_swapchain_size(u32 imageCount) {
    arena_t a = {0};
    arena_init(&a, NULL, SIZE_MAX);

    alloc(&a, Swapchain);
    alloc_array(&a, VkImage, imageCount);
    alloc_array(&a, VkImageView, imageCount);
    alloc_array(&a, VkFramebuffer, imageCount);
    return (size_t)(uintptr_t)a.curr;
}

typedef struct LogicalDevice {
    VkDevice handle;
    struct {
        VkQueue graphics;
        VkQueue present;
    } queueHandles;
    Swapchain* swapchain;
} LogicalDevice;

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
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    // staging
    u8* stagingBuffer_cpu_mem;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
} VulkanCtx;

//typedef struct Vertex_t {
//    vec3 pos;
//    vec3 normal;
//    vec2 uv;
//} Vertex_t;
// placeholder for testing
typedef struct Vertex_t {
    vec3 pos;
} Vertex_t;

typedef struct UniformBufferObject {
    alignas(16) mat4 proj;
    alignas(16) mat4 view; // camera
    alignas(16) mat4 model;
    alignas(4) float time;
} UniformBufferObject;

u32 window_height(VulkanCtx* ctx) {
    return ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.height;
}
u32 window_width(VulkanCtx* ctx) {
    return ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.width;
}

VkResult vulkan_create_instance(VkInstance* instance) {
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "My Game",
        .applicationVersion = 0,
        .pEngineName = "None",
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = LEN(enabledLayerNames),
        .ppEnabledLayerNames = enabledLayerNames,
        .enabledExtensionCount = LEN(enabledInstanceExtensionNames),
        .ppEnabledExtensionNames = enabledInstanceExtensionNames
    };
    return vkCreateInstance(&info, NULL, instance);
}

void vulkan_create_logical_device(VulkanCtx* ctx) {
    PhysicalDevice* physDevice = ctx->physicalDevice.selected;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo infos[2] = {
        // graphics
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = physDevice->graphicsQF.index,
            .queueCount = 1,
            .pQueuePriorities = &priority
        }
    };
    u32 qCount = 1;
    if (physDevice->presentQF.index != physDevice->graphicsQF.index) {
        infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        infos[1].queueFamilyIndex = physDevice->presentQF.index;
        infos[1].queueCount = 1;
        infos[1].pQueuePriorities = &priority;
        qCount++;
    }

    VkPhysicalDeviceFeatures enabledFeatures;
    vkGetPhysicalDeviceFeatures(physDevice->handle, &enabledFeatures);

    VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = qCount,
        .pQueueCreateInfos = infos,
        .enabledExtensionCount = LEN(enabledDeviceExtensionNames),
        .ppEnabledExtensionNames = enabledDeviceExtensionNames,
        .pEnabledFeatures = &enabledFeatures
    };
    VkResult res = vkCreateDevice(physDevice->handle, &info, NULL, &ctx->device.handle);
    assert(res == VK_SUCCESS);
    vkGetDeviceQueue(ctx->device.handle, physDevice->graphicsQF.index, 0, &ctx->device.queueHandles.graphics);
    vkGetDeviceQueue(ctx->device.handle, physDevice->presentQF.index, 0, &ctx->device.queueHandles.present);
    ctx->device.swapchain = NULL;
}

u32 get_surface_capabilities_image_count(VulkanCtx* ctx) {
    PhysicalDevice* device = ctx->physicalDevice.selected;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice.selected->handle, ctx->surface, &device->surfaceCapabilities);
    assert(res == VK_SUCCESS);
    assert(device->surfaceCapabilities.currentExtent.width != UINT32_MAX); // TODO: need to handle different if not X11

    // decide on image count, according to:
    // https://vulkan-tutorial.com/Drawing_a_triangle/Presentation/Swap_chain
    // "However, simply sticking to this minimum means that we may sometimes have to wait on the driver to complete internal 
    // operations before we can acquire another image to render to. Therefore it is recommended to request at least one 
    // more image than the minimum:"
    u32 imageCount = device->surfaceCapabilities.minImageCount + 1;
    if (device->surfaceCapabilities.maxImageCount > 0 && imageCount > device->surfaceCapabilities.maxImageCount) {
        imageCount = device->surfaceCapabilities.maxImageCount;
    }
    return imageCount;
}

typedef struct staging_buffer_upload_t {
    VkBufferCopy copyRegion;
    VkBuffer src, dst;
} staging_buffer_upload_t;

void upload_staging_buffer(VulkanCtx* ctx, staging_buffer_upload_t* uploadInfo, VkCommandPool transientPool) {
    VkCommandBufferAllocateInfo copyBufInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = transientPool,
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
    res = vkQueueSubmit(ctx->device.queueHandles.graphics, 1, &submitInfo, VK_NULL_HANDLE);
    assert(res == VK_SUCCESS);
    // TODO: add transfer fence maybe?
    vkQueueWaitIdle(ctx->device.queueHandles.graphics);
    assert(res == VK_SUCCESS);
}

void create_and_set_new_swapchain(arena_t* swapchainArena, u32 imageCount, VulkanCtx* ctx) {
    PhysicalDevice* physDevice = ctx->physicalDevice.selected;
    LogicalDevice* device = &ctx->device;
    VkSwapchainCreateInfoKHR createInfo = {
        .sType =                 VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface =               ctx->surface,
        .minImageCount =         imageCount,
        .imageFormat =           physDevice->formats.selected.format,
        .imageColorSpace =       physDevice->formats.selected.colorSpace,
        .imageExtent =           physDevice->surfaceCapabilities.currentExtent,
        .imageArrayLayers =      1,
        .imageUsage =            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode =      VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices =   NULL,
        .preTransform =          physDevice->surfaceCapabilities.currentTransform,
        .compositeAlpha =        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode =           physDevice->presentMode,
        .clipped =               VK_TRUE,
        .oldSwapchain =          device->swapchain ? device->swapchain->handle : VK_NULL_HANDLE
    };
    // if graphics and presents are from different queue indices
    if (device->queueHandles.graphics != device->queueHandles.present) {
        u32 indices[2] = {physDevice->graphicsQF.index, physDevice->presentQF.index};
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = indices;
    }

    device->swapchain = (Swapchain*)alloc(swapchainArena, Swapchain);
    VkResult res = vkCreateSwapchainKHR(device->handle, &createInfo, NULL, &device->swapchain->handle);

    assert(device->swapchain->handle != VK_NULL_HANDLE);
    assert(res == VK_SUCCESS);

    // create images, views and framebuffers
    res = vkGetSwapchainImagesKHR(device->handle, device->swapchain->handle, &imageCount, NULL);
    assert(res == VK_SUCCESS);
    device->swapchain->images = alloc_array(swapchainArena, VkImage, imageCount);
    device->swapchain->imageViews = alloc_array(swapchainArena, VkImageView, imageCount);
    device->swapchain->framebuffers = alloc_array(swapchainArena, VkFramebuffer, imageCount);
    res = vkGetSwapchainImagesKHR(device->handle, device->swapchain->handle, &imageCount, device->swapchain->images);
    assert(res == VK_SUCCESS);
    for (int i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo imageViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = device->swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = physDevice->formats.selected.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        res = vkCreateImageView(device->handle, &imageViewInfo, NULL, &device->swapchain->imageViews[i]);
        assert(res == VK_SUCCESS);

        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->renderPass,
            .attachmentCount = 1,
            .pAttachments = &device->swapchain->imageViews[i],
            .width = physDevice->surfaceCapabilities.currentExtent.width,
            .height = physDevice->surfaceCapabilities.currentExtent.height,
            .layers = 1
        };
        res = vkCreateFramebuffer(device->handle, &framebufferInfo, NULL, &device->swapchain->framebuffers[i]);
        assert(res == VK_SUCCESS);
    }
    device->swapchain->imageCount = imageCount;
};

void destroy_swapchain(VkDevice handle, Swapchain* swapchain) {
    for (int i = 0; i < swapchain->imageCount; i++) {
        vkDestroyFramebuffer(handle, swapchain->framebuffers[i], NULL);
        vkDestroyImageView(handle, swapchain->imageViews[i], NULL);
        // images destroyed as part of the swapchain
    }
    vkDestroySwapchainKHR(handle, swapchain->handle, NULL);
}

// TODO: use vulkan allocator to allocate memory
void create_buffer(
        VulkanCtx* ctx, 
        u32 size,
        VkBufferUsageFlags usage,
        VkSharingMode sharingMode, 
        VkMemoryPropertyFlags properties, 
        VkBuffer* buffer, 
        VkDeviceMemory* memory
        ) {
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = sharingMode
    };
    VkResult res = vkCreateBuffer(ctx->device.handle, &info, NULL, buffer);
    assert(res == VK_SUCCESS);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(ctx->device.handle, *buffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice.selected->handle, &memProperties);

    int memTypeIndex = -1;
    for (int i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memTypeIndex = i;
            break;
        }
    }
    assert(memTypeIndex != -1);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memTypeIndex
    };

    // TODO: create allocator (that offsets using bind instead), can't make unlimited of these calls
    res = vkAllocateMemory(ctx->device.handle, &allocInfo, NULL, memory);
    assert(res == VK_SUCCESS);

    vkBindBufferMemory(ctx->device.handle, *buffer, *memory, 0);
}

// init staging buffers, say 16 Mb for now
void init_staging_buffers(VulkanCtx* ctx) {
    const u64 size = MB(16);
    create_buffer(ctx, 
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &ctx->stagingBuffer,
            &ctx->stagingBufferMemory);
    vkMapMemory(ctx->device.handle, ctx->stagingBufferMemory, 0, size, 0, (void**)&ctx->stagingBuffer_cpu_mem);
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
