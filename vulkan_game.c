#define MAX_FRAMES_IN_FLIGHT 3

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

size_t get_swapchain_size(u32 imageCount) {
    arena_t a = arena_create(NULL, SIZE_MAX);
    alloc(&a, Swapchain);
    alloc_array(&a, VkImage, imageCount);
    alloc_array(&a, VkImageView, imageCount);
    alloc_array(&a, VkFramebuffer, imageCount);
    return (size_t)(uintptr_t)a.curr;
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
    ctx->device.swapchain.current = NULL;
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
        .oldSwapchain =          device->swapchain.current ? device->swapchain.current->handle : VK_NULL_HANDLE
    };
    // if graphics and presents are from different queue indices
    if (device->queueHandles.graphics != device->queueHandles.present) {
        u32 indices[2] = {physDevice->graphicsQF.index, physDevice->presentQF.index};
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = indices;
    }

    device->swapchain.current = (Swapchain*)alloc(swapchainArena, Swapchain);
    VkResult res = vkCreateSwapchainKHR(device->handle, &createInfo, NULL, &device->swapchain.current->handle);

    assert(device->swapchain.current->handle != VK_NULL_HANDLE);
    assert(res == VK_SUCCESS);

    // create images, views and framebuffers
    res = vkGetSwapchainImagesKHR(device->handle, device->swapchain.current->handle, &imageCount, NULL);
    assert(res == VK_SUCCESS);
    device->swapchain.current->images = alloc_array(swapchainArena, VkImage, imageCount);
    device->swapchain.current->imageViews = alloc_array(swapchainArena, VkImageView, imageCount);
    device->swapchain.current->framebuffers = alloc_array(swapchainArena, VkFramebuffer, imageCount);
    res = vkGetSwapchainImagesKHR(device->handle, device->swapchain.current->handle, &imageCount, device->swapchain.current->images);
    assert(res == VK_SUCCESS);

    // depth buffer, sized to extent
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx->physicalDevice.selected->handle, VK_FORMAT_D32_SFLOAT, &props);
    assert(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkExtent2D extent = ctx->physicalDevice.selected->surfaceCapabilities.currentExtent;
    VkImageCreateInfo depth_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    res = vkCreateImage(ctx->device.handle, &depth_info, NULL, &device->swapchain.current->image_depth_buffer);
    assert(res == VK_SUCCESS);

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device.handle, device->swapchain.current->image_depth_buffer, &mem_reqs);

    gpu_arena_reset(&device->swapchain.arena_depth_buf);
    device->swapchain.current->gpu_mem_depth_buffer = gpu_arena_suballoc(
            &device->swapchain.arena_depth_buf,
            mem_reqs.size,
            mem_reqs.alignment);

    gpu_arena_alloc_bind_image(&device->swapchain.current->gpu_mem_depth_buffer, ctx->device.handle, device->swapchain.current->image_depth_buffer);

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = device->swapchain.current->image_depth_buffer,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCreateImageView(device->handle, &depth_view_info, NULL, &device->swapchain.current->image_view_depth_buffer);

    for (int i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo imageViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = device->swapchain.current->images[i],
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
        res = vkCreateImageView(device->handle, &imageViewInfo, NULL, &device->swapchain.current->imageViews[i]);
        assert(res == VK_SUCCESS);

        VkImageView attachments[] = {
            device->swapchain.current->imageViews[i],
            device->swapchain.current->image_view_depth_buffer // same depth for all frames
        };
        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->render_state.render_pass_main,
            .attachmentCount = LEN(attachments),
            .pAttachments = attachments,
            .width = extent.width,
            .height = extent.height,
            .layers = 1
        };
        res = vkCreateFramebuffer(device->handle, &framebufferInfo, NULL, &device->swapchain.current->framebuffers[i]);
        assert(res == VK_SUCCESS);
    }
    device->swapchain.current->imageCount = imageCount;
};

void destroy_swapchain(VkDevice device, Swapchain* old) {
    for (int i = 0; i < old->imageCount; i++) {
        vkDestroyFramebuffer(device, old->framebuffers[i], NULL);
        vkDestroyImageView(device, old->imageViews[i], NULL);
        // images destroyed as part of the swapchain
    }
    vkDestroyImageView(device, old->image_view_depth_buffer, NULL);
    vkDestroyImage(device, old->image_depth_buffer, NULL);
    vkDestroySwapchainKHR(device, old->handle, NULL);
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

VkMemoryRequirements get_image_mem_reqs(VkDevice device, VkImageCreateInfo* info) {
    VkMemoryRequirements reqs;
    VkImage dummy;
    vkCreateImage(device, info, NULL, &dummy);
    vkGetImageMemoryRequirements(device, dummy, &reqs);
    vkDestroyImage(device, dummy, NULL);
    return reqs;
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

// Begin game specific vulkan stuff

// game data

typedef struct {
    alignas(16) mat4 proj;
    alignas(16) mat4 view; // camera
    alignas(16) vec3 sun_dir;
    alignas(16) vec3 sun_col;
    alignas(16) vec3 sky_col;
    alignas(4) float fog_start;
    alignas(4) float fog_end;
    alignas(4) float time;
} UBO_global_t;

#include <terrain_vulkan.c>

vulkan_state_t* init_rendering(
        arena_t* arena_permanent, 
        arena_t* arena_scratch, 
        VkResult (*surface_factory)(VkInstance instance, VkSurfaceKHR* surface_out),
        file_data_t vert_shader,
        file_data_t frag_shader) 
{
    vulkan_state_t* state = alloc(arena_permanent, vulkan_state_t);
    VulkanCtx* ctx = &state->ctx;

    VkResult res;
    // create instance
    {
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
        res = vkCreateInstance(&info, NULL, &ctx->instance);
        assert(res == VK_SUCCESS);
    }
    res = surface_factory(ctx->instance, &ctx->surface);
    assert(res == VK_SUCCESS);

    // find phys devices, TODO: score and sort them
    {
        u32 nPhys;
        VkResult res = vkEnumeratePhysicalDevices(ctx->instance, &nPhys, NULL);
        assert(res == VK_SUCCESS);
        VkPhysicalDevice* handles = alloc_array(arena_scratch, VkPhysicalDevice, nPhys);
        res = vkEnumeratePhysicalDevices(ctx->instance, &nPhys, handles);
        assert(res == VK_SUCCESS);
        //printf("phys device count: %u\n", nPhys);

        arena_mark_t validDeviceMark = arena_mark(arena_permanent);

        for (int i = 0; i < nPhys; i++) {
            arena_reset_to(arena_permanent, validDeviceMark);
            PhysicalDevice* validDevice = alloc(arena_permanent, PhysicalDevice);

            validDevice->handle = handles[i];
            vkGetPhysicalDeviceProperties(handles[i], &validDevice->props);
            u32 propCount;
            vkEnumerateDeviceExtensionProperties(handles[i], NULL, &propCount, NULL);
            VkExtensionProperties* extProps = alloc_array(arena_scratch, VkExtensionProperties, propCount);
            vkEnumerateDeviceExtensionProperties(handles[i], NULL, &propCount, extProps);
            VkBool32 extensionsSupported = VK_TRUE;
            for (int e = 0; e < LEN(enabledDeviceExtensionNames); e++) {
                VkBool32 found = VK_FALSE;
                for (int p = 0; p < propCount; p++) {
                    if (strcmp(enabledDeviceExtensionNames[e], extProps[p].extensionName) == 0) {
                        found = VK_TRUE;
                        break;
                    }
                }
                // found unsupported extension
                if (!found) {
                    extensionsSupported = VK_FALSE;
                    break;
                }
            }
            if (!extensionsSupported) continue;

            // find present and graphics queue families
            u32 nQf = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(handles[i], &nQf, NULL);
            VkQueueFamilyProperties* pqfs = 
                alloc_array(arena_scratch, VkQueueFamilyProperties, nQf);
            vkGetPhysicalDeviceQueueFamilyProperties(handles[i], &nQf, pqfs);
            for (int q = 0; q < nQf; q++) {
                // grab first graphics queue family, beware can be several!
                if (pqfs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    // save graphicsQF info
                    validDevice->graphicsQF.index = q;
                    validDevice->graphicsQF.props = pqfs[q]; // copy
                    break;
                }
            }
            for (int q = 0; q < nQf; q++) {
                // grab first presents queue family, beware can be several!
                VkBool32 supported = 0;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(
                            handles[i], q, ctx->surface, &supported) == VK_SUCCESS && 
                        supported) {
                    validDevice->presentQF.index = q;
                    validDevice->presentQF.props = pqfs[q];
                    break;
                }
            }

            //// surface capabilities (only to check min image count or whatever, likely not that important)
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(handles[i], ctx->surface, &validDevice->surfaceCapabilities);
            if (res != VK_SUCCESS) continue;

            // surface formats
            res = vkGetPhysicalDeviceSurfaceFormatsKHR(handles[i], ctx->surface, &validDevice->formats.count, NULL);
            if (res != VK_SUCCESS) continue;
            validDevice->formats.items = alloc_array(arena_scratch /*discarded*/, VkSurfaceFormatKHR, validDevice->formats.count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(handles[i], ctx->surface, &validDevice->formats.count, validDevice->formats.items);
            // get preferred format
            validDevice->formats.selected = validDevice->formats.items[0];
            for (int f = 0; f < validDevice->formats.count; f++) {
                VkSurfaceFormatKHR format = validDevice->formats.items[f];
                if (format.format == VK_FORMAT_R8G8B8A8_SRGB && 
                        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    validDevice->formats.selected = format;
                }
            }


            // present mode
            u32 nPresentModes;
            res = vkGetPhysicalDeviceSurfacePresentModesKHR(handles[i], ctx->surface, &nPresentModes, NULL);
            if (res != VK_SUCCESS) continue;
            VkPresentModeKHR* presentModes = alloc_array(arena_permanent /*saved*/, VkPresentModeKHR, nPresentModes);
            vkGetPhysicalDeviceSurfacePresentModesKHR(handles[i], ctx->surface, &nPresentModes, presentModes);
            // get preferred present mode
            validDevice->presentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (int p = 0; p < nPresentModes; p++) {
                if (presentModes[p] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    validDevice->presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                }
            }
            // additional features
            VkPhysicalDeviceFeatures features;
            vkGetPhysicalDeviceFeatures(handles[i], &features);
            if (!features.fillModeNonSolid) continue;

            // memory requirements and pool init
            vkGetPhysicalDeviceMemoryProperties(handles[i], &validDevice->props_memory);
            validDevice->gpu_arena_pool.arena_count = validDevice->props_memory.memoryTypeCount;
            validDevice->gpu_arena_pool.arenas = alloc_array(arena_permanent, gpu_arena_t, validDevice->props_memory.memoryTypeCount);

            // this is a valid device
            //printf("Found device: %s\n", validDevice->props.deviceName);
            // first device is assigned to the ptr
            if (ctx->physicalDevice.count == 0) {
                ctx->physicalDevice.all = validDevice;
            }
            ctx->physicalDevice.count++;
            // advance arena mark, ie "lock in" the device
            validDeviceMark = arena_mark(arena_permanent); 
        }
    }
    assert(ctx->physicalDevice.all != NULL && ctx->physicalDevice.count > 0);
    // select device, can be changed later based on some scoring I guess
    ctx->physicalDevice.selected = &ctx->physicalDevice.all[0];
    //printf("Selected device: %s\n", ctx->physicalDevice.selected->props.deviceName);
    vulkan_create_logical_device(ctx);

    // get image count here to determine max frames in flight, does that even make sense?
    ctx->render_state.image_count = get_surface_capabilities_image_count(ctx);
    ctx->render_state.frames_in_flight = MIN(ctx->render_state.image_count, MAX_FRAMES_IN_FLIGHT);
    assert(ctx->render_state.frames_in_flight > 0);

    // create query pool, TODO: pipeline statistics maybe?
    {
        VkQueryPoolCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = ctx->render_state.frames_in_flight * 2 // one before render and one after
        };

        res = vkCreateQueryPool(ctx->device.handle, &info, NULL, &ctx->render_state.query_pool_timestamp);
        assert(res == VK_SUCCESS);
    }

    // create render pass
    {

        VkAttachmentDescription attachments[] = {
            // swapchain image view
            {
                .format = ctx->physicalDevice.selected->formats.selected.format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            },
            // depth buffer
            {
                .format = VK_FORMAT_D32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };

        VkAttachmentReference colorAttachmentRefs[] = {
            { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } // swapchain image view
        };

        VkAttachmentReference depth_attach_ref = {
            .attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };


        VkSubpassDescription subpasses[] = {
            {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = colorAttachmentRefs,
                .pDepthStencilAttachment = &depth_attach_ref
            }
        };

        // since we will allowing command buffer execution before we have an aquired image attachment
        // we say that the renderpass may not start transitioning the attachment until the color_attachment_output
        // stage, where we will wait on a semaphore to make sure the image is acquired
        VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        };
        VkRenderPassCreateInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = LEN(attachments),
            .pAttachments = attachments,
            .subpassCount = LEN(subpasses),
            .pSubpasses = subpasses,
            .dependencyCount = 1,
            .pDependencies = &dependency
        };
        res = vkCreateRenderPass(ctx->device.handle, &renderPassInfo, NULL, &ctx->render_state.render_pass_main);
        assert(res == VK_SUCCESS);
    }

    // buffer arenas 
    ctx->arena_ubo_ssbo = gpu_buffer_arena_create(
            ctx->device.handle, 
            ctx->physicalDevice.selected->handle, 
            &ctx->physicalDevice.selected->props_memory, 
            MB(16), 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ctx->arena_staging = gpu_buffer_arena_create(
            ctx->device.handle, 
            ctx->physicalDevice.selected->handle, 
            &ctx->physicalDevice.selected->props_memory, 
            MB(16), 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ctx->arena_local_mesh = gpu_buffer_arena_create(
            ctx->device.handle, 
            ctx->physicalDevice.selected->handle, 
            &ctx->physicalDevice.selected->props_memory, 
            MB(16), 
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // create swapchain depth buffer arena
    {
        // arena for depth images I guess
        VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = { 1, 1, 1 }, // size doesn't affect memory type, only alignment/size
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        VkMemoryRequirements mem_reqs = get_image_mem_reqs(ctx->device.handle, &image_info);
        gpu_arena_init(
                &ctx->physicalDevice.selected->gpu_arena_pool, 
                ctx->device.handle, 
                &ctx->physicalDevice.selected->props_memory, 
                &mem_reqs,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                MB(256)); // not sure here tbh
        ctx->device.swapchain.arena_depth_buf = gpu_arena_alloc_subarena(
                &ctx->physicalDevice.selected->gpu_arena_pool, 
                ctx->device.handle,
                &ctx->physicalDevice.selected->props_memory, 
                &mem_reqs,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                MB(16));
    }


    // command pools
    {
        VkCommandPoolCreateInfo info1 = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx->physicalDevice.selected->graphicsQF.index
        };
        res = vkCreateCommandPool(ctx->device.handle, &info1, NULL, &ctx->render_state.cmd_pool_graphics_auto_reset);
        assert(res == VK_SUCCESS);

        VkCommandPoolCreateInfo info2 = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = ctx->physicalDevice.selected->graphicsQF.index
        };
        res = vkCreateCommandPool(ctx->device.handle, &info2, NULL, &ctx->render_state.cmd_pool_graphics_transient);
        assert(res == VK_SUCCESS);
    }

    // create command buffers
    ctx->render_state.cmd_buffers_graphics = alloc_array(arena_permanent, VkCommandBuffer, ctx->render_state.frames_in_flight);
    {
        VkCommandBufferAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx->render_state.cmd_pool_graphics_auto_reset,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = ctx->render_state.frames_in_flight
        };
        res = vkAllocateCommandBuffers(ctx->device.handle, &info, ctx->render_state.cmd_buffers_graphics);
        assert(res == VK_SUCCESS);
    }

    ctx->render_state.image_available_semaphores = alloc_array(arena_permanent, VkSemaphore, ctx->render_state.frames_in_flight);
    ctx->render_state.render_finished_semaphores = alloc_array(arena_permanent, VkSemaphore, ctx->render_state.image_count);
    ctx->render_state.in_flight_fences = alloc_array(arena_permanent, VkFence, ctx->render_state.frames_in_flight);
    {
        VkSemaphoreCreateInfo semInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT // first wait doesn't block
        };

        // image available and fences are tied to frame lifetime
        for (int i = 0; i < ctx->render_state.frames_in_flight; i++) {
            res = vkCreateSemaphore(ctx->device.handle, &semInfo, NULL, &ctx->render_state.image_available_semaphores[i]);
            assert(res == VK_SUCCESS);
            res = vkCreateFence(ctx->device.handle, &fenceInfo, NULL, &ctx->render_state.in_flight_fences[i]);
            assert(res == VK_SUCCESS);
        }

        // render finished isn't and must be indexed based on what acquire returns
        for (int i = 0; i < ctx->render_state.image_count; i++) {
            res = vkCreateSemaphore(ctx->device.handle, &semInfo, NULL, &ctx->render_state.render_finished_semaphores[i]);
            assert(res == VK_SUCCESS);
        }
    }
    // block allocator for swapchain data, with space for MAX_FRAMES + 1 swapchain infos
    {
        size_t nBlocks = ctx->render_state.frames_in_flight + 1; // replace with max images
        size_t blockSize = get_swapchain_size(ctx->render_state.image_count);
        //printf("initializing block alloc\n");
        ctx->device.swapchain.allocator = block_alloc_create(
                alloc_array_aligned(arena_permanent, u8, block_alloc_bytes_required(blockSize, nBlocks), alignof(Swapchain)),
                nBlocks,
                blockSize);

        arena_t temp = arena_create(block_alloc(&ctx->device.swapchain.allocator), blockSize);
        //printf("creating swapchain\n");
        create_and_set_new_swapchain(&temp, ctx->render_state.image_count, ctx);
    }
    // descriptor pools
    const u32 desc_limit = 16; // plenty for now
    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = ctx->render_state.frames_in_flight * desc_limit
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = ctx->render_state.frames_in_flight * desc_limit
        }
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = LEN(pool_sizes) * ctx->render_state.frames_in_flight * desc_limit,
        .poolSizeCount = LEN(pool_sizes),
        .pPoolSizes = pool_sizes
    };

    res = vkCreateDescriptorPool(ctx->device.handle, &pool_info, NULL, &ctx->descriptor_pool_permanent);
    assert(res == VK_SUCCESS);

    // global uniforms
    {
        // buffer and mapping
        state->ubo_global = alloc_array(arena_permanent, gpu_buffer_alloc_t, ctx->render_state.frames_in_flight);
        for (u32 i = 0; i < ctx->render_state.frames_in_flight; i++) {
            state->ubo_global[i] = gpu_buffer_arena_alloc(&ctx->arena_ubo_ssbo, sizeof(UBO_global_t));
        }

        // descriptor set
        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        };
        VkDescriptorSetLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding
        };
        res = vkCreateDescriptorSetLayout(ctx->device.handle, &info, NULL, &state->ubo_global_descriptor_set_layout);
        assert(res == VK_SUCCESS);

        state->ubo_global_descriptor_sets = alloc_array(arena_permanent, VkDescriptorSet, ctx->render_state.frames_in_flight);
        allocate_descriptor_sets(arena_scratch, 
                ctx->device.handle, 
                ctx->descriptor_pool_permanent, 
                state->ubo_global_descriptor_set_layout, 
                ctx->render_state.frames_in_flight,
                state->ubo_global_descriptor_sets);

        VkDescriptorBufferInfo* buffer_info = alloc_array(arena_scratch, VkDescriptorBufferInfo, ctx->render_state.frames_in_flight);
        VkWriteDescriptorSet* writes = alloc_array(arena_scratch, VkWriteDescriptorSet, ctx->render_state.frames_in_flight);
        for (u32 i = 0; i < ctx->render_state.frames_in_flight; i++) {
            buffer_info[i].buffer = state->ubo_global[i].arena->buffer,
                buffer_info[i].offset = state->ubo_global[i].offset,
                buffer_info[i].range = sizeof(UBO_global_t);

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = state->ubo_global_descriptor_sets[i];
            writes[i].dstBinding = 0;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo = &buffer_info[i];
            writes[i].descriptorCount = 1;
        }
        vkUpdateDescriptorSets(ctx->device.handle, ctx->render_state.frames_in_flight, writes, 0, NULL);
    }
    terrain_init(&state->terrain, arena_permanent, arena_scratch, ctx, state->ubo_global_descriptor_set_layout, vert_shader, frag_shader);
    return state;
}

void render(vulkan_state_t* vulkan_state, game_state_t* game_state, float time) {
    VulkanCtx* ctx = &vulkan_state->ctx;
    VkResult res;
    render_state_t* state = &ctx->render_state;
    u32 frame = state->frame;
    // wait for next sync objects
    VkFence fence = state->in_flight_fences[frame];
    VkSemaphore imageAvailableSemaphore = state->image_available_semaphores[frame];
    VkCommandBuffer cmdBuf = state->cmd_buffers_graphics[frame];
    // wait for submission frame i - framesInFlight
    // means that semaphores and commandbuffers above are no longer in use by CPU
    vkWaitForFences(ctx->device.handle, 1, &fence, VK_TRUE, UINT64_MAX);

    if (state->frame_count >= state->frames_in_flight) {
        u64 timestamps[2];
        vkGetQueryPoolResults(ctx->device.handle, state->query_pool_timestamp, frame * LEN(timestamps), 
                LEN(timestamps), sizeof(timestamps), timestamps, sizeof(u64), 
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        state->curr_render_time_ms = (timestamps[1] - timestamps[0]) * ctx->physicalDevice.selected->props.limits.timestampPeriod * 1e-6f;
    }

    // bookkeeping
    if (state->swapchain_cooldown > 0) state->swapchain_cooldown--;
    if (state->recreate_swapchain || 
            (res = vkAcquireNextImageKHR(
                                         ctx->device.handle, 
                                         ctx->device.swapchain.current->handle, UINT64_MAX, 
                                         imageAvailableSemaphore, VK_NULL_HANDLE, &state->curr_image_index)) == VK_ERROR_OUT_OF_DATE_KHR) 
    {
        vkDeviceWaitIdle(ctx->device.handle);
        // update capabilities (extents)
        res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice.selected->handle, ctx->surface, &ctx->physicalDevice.selected->surfaceCapabilities);
        // create new (transition from old)
        arena_t temp = arena_create(block_alloc(&ctx->device.swapchain.allocator), ctx->device.swapchain.allocator.blockSize);
        Swapchain* old = ctx->device.swapchain.current;
        create_and_set_new_swapchain(&temp, state->image_count, ctx);

        // destroy old
        destroy_swapchain(ctx->device.handle, old);
        block_alloc_free(&ctx->device.swapchain.allocator, old);

        state->swapchain_cooldown = state->frames_in_flight;
        state->recreate_swapchain = false;

        return;
    } else assert(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR);

    vkResetFences(ctx->device.handle, 1, &fence);

    // rendering begin

    const vec3 sky_col = (vec3){0.4, 0.6, 0.9};

    // update global uniforms
    {
        UBO_global_t* uniform = (UBO_global_t*)vulkan_state->ubo_global[frame].mapped;
        uniform->time = time;

        // recompute projection based on screen size
        float aspect = (float)ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.width / 
            ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.height;
        float fov_rads = PI/2;
        game_state->main_camera.proj = mat4_perspective(fov_rads, aspect, 0.1f, 1000.0f);

        // camera
        uniform->proj = game_state->main_camera.proj;
        uniform->view = transform_to_view_matrix(&game_state->main_camera.transform);
        // for debug
        uniform->sun_dir = vec3_normalized((vec3){0.6, 1.0, 0.4});
        uniform->sun_col = (vec3){1.0, 0.95, 0.8};
        uniform->sky_col = sky_col;
        uniform->fog_start = 700.0f;
        uniform->fog_end = 900.0f;

        //vec4 vi = mat4_apply(&uniform->view, (vec4){1,0,0,1});
        //vec4 clip = mat4_apply(&uniform->proj, vi);
        //vec3 ndc = {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
        //printf("vi: %f %f %f %f\n", vi.x, vi.y, vi.z, vi.w);
        //printf("clip: %f %f %f %f\n", clip.x, clip.y, clip.z, clip.w);
        //printf("ndc: %f %f %f\n", ndc.x, ndc.y, ndc.z);
    }

    // record command buffer
    vkResetCommandBuffer(cmdBuf, 0);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    res = vkBeginCommandBuffer(cmdBuf, &beginInfo);
    assert(res == VK_SUCCESS);


    VkClearValue clearValues[] = {
        {{sky_col.x, sky_col.y, sky_col.z, 1.0f}},
        {.depthStencil = {1.0f, 0}},

    };
    VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->render_state.render_pass_main,
        .framebuffer = ctx->device.swapchain.current->framebuffers[state->curr_image_index],
        .renderArea.offset = {0,0},
        .renderArea.extent = ctx->physicalDevice.selected->surfaceCapabilities.currentExtent,
        .clearValueCount = LEN(clearValues),
        .pClearValues = clearValues
    };
    vkCmdResetQueryPool(cmdBuf, state->query_pool_timestamp, frame * 2, 2);
    vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdWriteTimestamp(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, state->query_pool_timestamp, frame * 2);
    terrain_render(&vulkan_state->ctx, game_state, &vulkan_state->terrain, cmdBuf, vulkan_state->ubo_global_descriptor_sets[frame], frame);

    // TODO: render other things here

    vkCmdEndRenderPass(cmdBuf);
    vkCmdWriteTimestamp(cmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->query_pool_timestamp, frame * 2 + 1);
    res = vkEndCommandBuffer(cmdBuf);
    assert(res == VK_SUCCESS);

    // render more things here...

    VkSemaphore renderFinishedSemaphore = state->render_finished_semaphores[state->curr_image_index];

    // submit to cmd buf
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphore,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphore 
    };
    res = vkQueueSubmit(ctx->device.queueHandles.graphics, 1, &submitInfo, fence);
    assert(res == VK_SUCCESS);

    // present
    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore, 
        .swapchainCount = 1,
        .pSwapchains = &ctx->device.swapchain.current->handle,
        .pImageIndices = &state->curr_image_index
    };
    res = vkQueuePresentKHR(ctx->device.queueHandles.present, &presentInfo);
    if ((res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) && state->swapchain_cooldown == 0) {
        state->recreate_swapchain = true;
    }
    state->frame = (state->frame + 1) % state->frames_in_flight;
    state->frame_count++;
}
