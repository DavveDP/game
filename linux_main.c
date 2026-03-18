// Essentials from C stdlib
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char  u8;

typedef long long i64;
typedef int i32;
typedef short i16;
typedef char i8;

#define false 0
#define true 0

#define MAX_FRAMES_IN_FLIGHT 3
#define GB(n) (1ull << 30)
#define MB(n) (1ull << 20)
#define KB(n) (1ull << 10)
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define MIN(X, Y) (X) < (Y) ? (X) : (Y)
//#define CLAMP(X, Min, Max) \
//    (X) > (Max) ? (Max) : ((X) < Min ? Min : (X))

#include <models.c> // just a bunch of embed directives

// Begin Platfrom specific code

#include <linux_alloc.c>

#include <sys/mman.h>
#include <signal.h>
#include <stdio.h>
#include <X11/Xlib.h>
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <game_vulkan.c> 

// audio
// input
// time
// disk, only required for save games
// (network)

Atom WM_DELETE_WINDOW;
Atom NET_WM_PING;
Atom WM_PROTOCOLS;

volatile sig_atomic_t running = 1;

void sigint_and_sigterm_handler(int sig) {
    running = 0;
}

int main(int argc, char** argv) {
    // Signal handling
    signal(SIGINT, sigint_and_sigterm_handler);
    signal(SIGTERM, sigint_and_sigterm_handler);

    // Our only alloc call, (see allocators)
    void* mem = mmap(NULL, GB(1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // create app arena
    arena_t appArena;
    arena_init(&appArena, mem, GB(1));

    // create scratchArena (nested arena)
    arena_t scratchArena;
    arena_init(&scratchArena, 
            alloc_array(&appArena, u8, MB(1)),
            MB(1)
    );

    VulkanCtx ctx;
    // create vulkan instance
    VkResult res = vulkan_create_instance(&ctx.instance);
    assert(res == VK_SUCCESS);

    printf("instance created!\n");

    // platform specific stuff to create surface
    Display* display;
    Window window;
    {
        display = XOpenDisplay(NULL);
        window = XCreateSimpleWindow(
                display, 
                RootWindow(display, DefaultScreen(display)), 
                0, 0, 
                800, 600, 
                0, 
                WhitePixel(display, 0),
                BlackPixel(display, 0)
        );

        VkXlibSurfaceCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = display,
            .window = window
        };
        VkResult res = vkCreateXlibSurfaceKHR(ctx.instance, &info, NULL, &ctx.surface);
        assert(res == VK_SUCCESS);
    }
    
    // from here on out, just plain vulkan (sort of lol)
    
    // find phys devices, TODO: score and sort them
    {
        u32 nPhys;
        VkResult res = vkEnumeratePhysicalDevices(ctx.instance, &nPhys, NULL);
        assert(res == VK_SUCCESS);
        VkPhysicalDevice* handles = alloc_array(&scratchArena, VkPhysicalDevice, nPhys);
        res = vkEnumeratePhysicalDevices(ctx.instance, &nPhys, handles);
        assert(res == VK_SUCCESS);
        printf("phys device count: %u\n", nPhys);

        arena_mark_t validDeviceMark = arena_mark(&appArena);

        for (int i = 0; i < nPhys; i++) {
            arena_reset_to(&appArena, validDeviceMark);
            PhysicalDevice* validDevice = alloc(&appArena, PhysicalDevice);

            validDevice->handle = handles[i];
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(handles[i], &props);
            u32 propCount;
            vkEnumerateDeviceExtensionProperties(handles[i], NULL, &propCount, NULL);
            VkExtensionProperties* extProps = alloc_array(&scratchArena, VkExtensionProperties, propCount);
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
                alloc_array(&scratchArena, VkQueueFamilyProperties, nQf);
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
                            handles[i], q, ctx.surface, &supported) == VK_SUCCESS && 
                        supported) {
                    validDevice->presentQF.index = q;
                    validDevice->presentQF.props = pqfs[q];
                    break;
                }
            }

            //// surface capabilities (only to check min image count or whatever, likely not that important)
            //res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(handles[i], surface, &validDevice->capabilities);
            //if (res != VK_SUCCESS) continue;
            
            // formats
            res = vkGetPhysicalDeviceSurfaceFormatsKHR(handles[i], ctx.surface, &validDevice->formats.count, NULL);
            if (res != VK_SUCCESS) continue;
            validDevice->formats.items = alloc_array(&scratchArena /*discarded*/, VkSurfaceFormatKHR, validDevice->formats.count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(handles[i], ctx.surface, &validDevice->formats.count, validDevice->formats.items);
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
            res = vkGetPhysicalDeviceSurfacePresentModesKHR(handles[i], ctx.surface, &nPresentModes, NULL);
            if (res != VK_SUCCESS) continue;
            VkPresentModeKHR* presentModes = alloc_array(&appArena /*saved*/, VkPresentModeKHR, nPresentModes);
            vkGetPhysicalDeviceSurfacePresentModesKHR(handles[i], ctx.surface, &nPresentModes, presentModes);
            // get preferred present mode
            validDevice->presentMode = VK_PRESENT_MODE_FIFO_KHR;
            for (int p = 0; p < nPresentModes; p++) {
                if (presentModes[p] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    validDevice->presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                }
            }
            
            // this is a valid device
            printf("Found device: %s\n", props.deviceName);
            // first device is assigned to the ptr
            if (ctx.physicalDevice.count == 0) {
                ctx.physicalDevice.all = validDevice;
            }
            ctx.physicalDevice.count++;
            // advance arena mark, ie "lock in" the device
            validDeviceMark = arena_mark(&appArena); 
        }
    }
    assert(ctx.physicalDevice.all != NULL && ctx.physicalDevice.count > 0);
    arena_reset(&scratchArena);
    // select device, can be changed later based on some scoring I guess
    ctx.physicalDevice.selected = &ctx.physicalDevice.all[0];
    vulkan_create_logical_device(&ctx);

    // create render pass
    {
        VkAttachmentDescription attachments[] = {
            // swapchain image view
            {
                .format = ctx.physicalDevice.selected->formats.selected.format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            }
        };

        VkAttachmentReference colorAttachmentRefs[] = {
            { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } // swapchain image view
        };

        VkSubpassDescription subpasses[] = {
            // main pass
            {
                .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments = colorAttachmentRefs
            }
        };

        // since we will allowing command buffer execution before we have an aquired image attachment
        // we say that the renderpass may not start transitioning the attachment until the color_attachment_output
        // stage, where we will wait on a semaphore to make sure the image is acquired
        VkSubpassDependency dependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
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
        res = vkCreateRenderPass(ctx.device.handle, &renderPassInfo, NULL, &ctx.renderPass);
        assert(res == VK_SUCCESS);
    }

    // create ctx.pipeline
    {
        // create shader stages
        static alignas(4) const u8 vertexCode[] = {
#embed "vertex.spv"
        };
        static alignas(4) const u8 fragmentCode[] = {
#embed "fragment.spv"
        };

        VkPipelineShaderStageCreateInfo stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = create_shader_module(ctx.device.handle, (const u32*)vertexCode, sizeof(vertexCode), &res),
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = create_shader_module(ctx.device.handle, (const u32*)fragmentCode, sizeof(fragmentCode), &res),
                .pName = "main"
            }
        };
        assert(res == VK_SUCCESS);

        // vertex input (single vertex buffer)
        VkVertexInputBindingDescription vertexBindings[] = {
            { .binding = 0, .stride = sizeof(Vertex_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
        };

        //VkVertexInputAttributeDescription vertexAttr[] = {
        //    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex_t, pos)    },
        //    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex_t, normal) },
        //    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex_t, uv)     },
        //};
        // placeholder
        VkVertexInputAttributeDescription vertexAttr[] = {
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex_t, pos)     },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex_t, color)   },
        };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = LEN(vertexBindings),
            .pVertexBindingDescriptions = vertexBindings,
            .vertexAttributeDescriptionCount = LEN(vertexAttr),
            .pVertexAttributeDescriptions = vertexAttr
        };

        // input assembly 
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        // Tessellation (Skip)

        // Dynamic State (Swapchain may change)
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = LEN(dynamicStates),
            .pDynamicStates = dynamicStates
        };

        VkPipelineViewportStateCreateInfo viewportInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        // Rasterization
        VkPipelineRasterizationStateCreateInfo rasterizerInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f
        };

        // Multisample (antialiasing?, thought that was done in PP not per material, but cool...)
        VkPipelineMultisampleStateCreateInfo multisampleInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
        };

        // DepthStencil (Skip)

        // Color Blending
        VkPipelineColorBlendAttachmentState blendAttachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        VkPipelineColorBlendStateCreateInfo blendStateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &blendAttachment
        };
        // Dynamic State?

        // Pipeline layout (uniforms and shit)
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
                // TODO: uniforms go here
        };
        VkPipelineLayout pipelineLayout;
        res = vkCreatePipelineLayout(ctx.device.handle, &pipelineLayoutInfo, NULL, &pipelineLayout);
        assert(res == VK_SUCCESS);

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = LEN(stages),
            .pStages = stages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssemblyInfo,
            // skipping tesselation
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizerInfo,
            .pMultisampleState = &multisampleInfo,
            // skipping stencil
            .pColorBlendState = &blendStateInfo,
            .pDynamicState = &dynamicStateInfo,
            // skipping dynamic state
            .layout = pipelineLayout,
            .renderPass = ctx.renderPass,
            .subpass = 0,
        };
        res = vkCreateGraphicsPipelines(ctx.device.handle, NULL/*TODO:cache required*/, 1, &pipelineInfo, NULL, &ctx.pipeline);
        assert(res == VK_SUCCESS);

        // destroy shader modules once ctx.pipeline is created
        for (int i = 0; i < LEN(stages); i++) {
            vkDestroyShaderModule(ctx.device.handle, stages[i].module, NULL);
        }
    }

    VkCommandPool pool;
    {
        VkCommandPoolCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx.physicalDevice.selected->graphicsQF.index
        };
        res = vkCreateCommandPool(ctx.device.handle, &info, NULL, &pool);
        assert(res == VK_SUCCESS);
    }

    VkCommandPool transientPool;
    {
        VkCommandPoolCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = ctx.physicalDevice.selected->graphicsQF.index
        };
        res = vkCreateCommandPool(ctx.device.handle, &info, NULL, &transientPool);
        assert(res == VK_SUCCESS);
    }
    // allocate vertices
    Vertex_t vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}}
    };

    u16 indices[] = {
        0, 1, 2, 2, 3, 0
    };

    // TODO: staging buffer, learn what that even is lol

    // create vertex buffer
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    {
        // create staging buffer
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        create_buffer(&ctx, 
                sizeof(vertices), 
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &stagingBuffer, 
                &stagingBufferMemory);

        create_buffer(&ctx,
                sizeof(vertices),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &vertexBuffer,
                &vertexBufferMemory);

        create_buffer(&ctx,
                sizeof(indices),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &indexBuffer,
                &indexBufferMemory);

        // upload vertex data
        void* data;
        vkMapMemory(ctx.device.handle, stagingBufferMemory, 0, sizeof(vertices), 0, &data);
        memcpy(data, vertices, (size_t) sizeof(vertices));
        vkUnmapMemory(ctx.device.handle, stagingBufferMemory);

        staging_buffer_upload_t uploadInfo = {
            .copyRegion = {0, 0, sizeof(vertices)},
            .src = stagingBuffer,
            .dst = vertexBuffer
        };
        upload_staging_buffer(&ctx, &uploadInfo, transientPool); // waits on device idle

        // upload index data
        vkMapMemory(ctx.device.handle, stagingBufferMemory, 0, sizeof(indices), 0, &data);
        memcpy(data, indices, (size_t) sizeof(indices));
        vkUnmapMemory(ctx.device.handle, stagingBufferMemory);

        uploadInfo.copyRegion = (VkBufferCopy){0, 0, sizeof(indices)};
        uploadInfo.src = stagingBuffer;
        uploadInfo.dst = indexBuffer;
        upload_staging_buffer(&ctx, &uploadInfo, transientPool); // waits on device idle

        res = vkResetCommandPool(ctx.device.handle, transientPool, 0);
        vkDestroyBuffer(ctx.device.handle, stagingBuffer, NULL);
        vkFreeMemory(ctx.device.handle, stagingBufferMemory, NULL);
    }

    // Init window

    // show the window, should activate the WM
    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);

    // set up WM message atoms
    WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);
    NET_WM_PING = XInternAtom(display, "_NET_WM_PING", False);
    WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom protocols[] = 
    {
        WM_DELETE_WINDOW,
        NET_WM_PING
    };
    XSetWMProtocols(display, window, protocols, LEN(protocols));

    // wait for the first resize event
    {
        XEvent e;
        do { XNextEvent(display, &e); } while (e.type != ConfigureNotify);
    }

    // get image count here to determine max frames in flight, does that even make sense?
    u32 imageCount = get_surface_capabilities_image_count(&ctx);
    u32 framesInFlight = MIN(imageCount, MAX_FRAMES_IN_FLIGHT);

    // create command buffers
    VkCommandBuffer* cmdBufs = alloc_array(&appArena, VkCommandBuffer, framesInFlight);
    {
        VkCommandBufferAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = framesInFlight
        };
        res = vkAllocateCommandBuffers(ctx.device.handle, &info, cmdBufs);
        assert(res == VK_SUCCESS);
    }

    VkSemaphore* imageAvailableSemaphores = alloc_array(&appArena, VkSemaphore, framesInFlight);
    VkSemaphore* renderFinishedSemaphores = alloc_array(&appArena, VkSemaphore, framesInFlight);
    VkFence* inFlightFences = alloc_array(&appArena, VkFence, framesInFlight);
    {
        for (int i = 0; i < framesInFlight; i++) {
            VkSemaphoreCreateInfo semInfo = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
            };
            VkFenceCreateInfo fenceInfo = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT // first wait doesn't block
            };
            res = vkCreateSemaphore(ctx.device.handle, &semInfo, NULL, &imageAvailableSemaphores[i]);
            assert(res == VK_SUCCESS);
            res = vkCreateSemaphore(ctx.device.handle, &semInfo, NULL, &renderFinishedSemaphores[i]);
            assert(res == VK_SUCCESS);
            res = vkCreateFence(ctx.device.handle, &fenceInfo, NULL, &inFlightFences[i]);
            assert(res == VK_SUCCESS);
        }
    }

    // block allocator, with space for MAX_FRAMES + 1 swapchain info
    block_alloc_t swapchainAlloc;
    {
        size_t nBlocks = framesInFlight + 1; // replace with max images
        size_t blockSize = get_swapchain_size(imageCount);
        printf("initializing block alloc\n");
        block_alloc_init(&swapchainAlloc, 
                alloc_array_aligned(&appArena, u8, block_alloc_bytes_required(blockSize, nBlocks), alignof(Swapchain)),
                nBlocks,
                blockSize);

        arena_t swapchainArena;
        arena_init(&swapchainArena, block_alloc(&swapchainAlloc), blockSize);
        printf("creating swapchain\n");
        create_and_set_new_swapchain(&swapchainArena, imageCount, &ctx);
    }
    //printf("created alloc\n");

    u32 imageIndex = 0;
    u32 frame = 0;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    u8 swapchainCooldown = 0;
    bool recreateSwapchain = false;
    XEvent e;

    while(running) {
        // wait for next sync objects
        VkFence fence = inFlightFences[frame];
        VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[frame];
        VkSemaphore renderFinishedSemaphore = renderFinishedSemaphores[frame];
        VkCommandBuffer cmdBuf = cmdBufs[frame];
        vkWaitForFences(ctx.device.handle, 1, &fence, VK_TRUE, UINT64_MAX);

        // bookkeeping
        if (swapchainCooldown > 0) swapchainCooldown--;

        // pump x events
        while (XPending(display)) {
            XNextEvent(display, &e);
            switch (e.type) {
                case ConfigureNotify:
                    {
                        if(swapchainCooldown == 0 && (e.xconfigure.width != ctx.device.surfaceCapabilities.currentExtent.width
                                    || e.xconfigure.height != ctx.device.surfaceCapabilities.currentExtent.height)) {
                            recreateSwapchain = true;
                        }
                    }
                    break;

                case ClientMessage:
                    {
                        if (e.xclient.message_type == WM_PROTOCOLS) {
                            Atom msg = e.xclient.data.l[0];
                            if (msg == WM_DELETE_WINDOW) {
                                running = 0;
                            }
                            // reply to WM ping if needed
                            else if (msg == NET_WM_PING) {
                                XEvent reply = e;
                                Window root = RootWindow(display, DefaultScreen(display)); // TODO: cache this
                                reply.xclient.window = root;
                                XSendEvent(display, root, false, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
                            }
                        }
                    }
                    break;
            }
            // TODO: handle input events
        }
        if (!running) break;


        res = vkAcquireNextImageKHR(ctx.device.handle, ctx.device.swapchain->handle, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || recreateSwapchain) {
            vkDeviceWaitIdle(ctx.device.handle);
            // update capabilities (extents)
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice.selected->handle, ctx.surface, &ctx.device.surfaceCapabilities);
            // create new (transition from old)
            arena_t swapchainArena;
            arena_init(&swapchainArena, block_alloc(&swapchainAlloc), swapchainAlloc.blockSize);
            Swapchain* old = ctx.device.swapchain;
            create_and_set_new_swapchain(&swapchainArena, imageCount, &ctx);

            // destroy old
            destroy_swapchain(ctx.device.handle, old);
            block_alloc_free(&swapchainAlloc, old);

            swapchainCooldown = framesInFlight;
            recreateSwapchain = false;

            continue;
        } 

        vkResetFences(ctx.device.handle, 1, &fence);

        // record command buffer
        vkResetCommandBuffer(cmdBuf, 0);
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        res = vkBeginCommandBuffer(cmdBuf, &beginInfo);
        assert(res == VK_SUCCESS);

        VkRenderPassBeginInfo renderPassBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.renderPass,
            .framebuffer = ctx.device.swapchain->framebuffers[imageIndex],
            .renderArea.offset = {0,0},
            .renderArea.extent = ctx.device.surfaceCapabilities.currentExtent,
            .clearValueCount = 1,
            .pClearValues = &clearColor
        };
        vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);
        // dynamic states
        VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width  = ctx.device.surfaceCapabilities.currentExtent.width,
            .height = ctx.device.surfaceCapabilities.currentExtent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

        VkRect2D scissor = {
            .offset = {0, 0},
            .extent = ctx.device.surfaceCapabilities.currentExtent
        };

        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
        VkBuffer vertexBuffers = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmdBuf, LEN(indices), 1, 0, 0, 0);
        vkCmdEndRenderPass(cmdBuf);
        res = vkEndCommandBuffer(cmdBuf);
        assert(res == VK_SUCCESS);

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
        res = vkQueueSubmit(ctx.device.queueHandles.graphics, 1, &submitInfo, fence);
        assert(res == VK_SUCCESS);

        // present
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore, 
            .swapchainCount = 1,
            .pSwapchains = &ctx.device.swapchain->handle,
            .pImageIndices = &imageIndex
        };
        res = vkQueuePresentKHR(ctx.device.queueHandles.present, &presentInfo);
        if ((res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR) && swapchainCooldown == 0) {
            recreateSwapchain = true;
        }
        frame = (frame + 1) % framesInFlight;
    }

    printf("Exiting gracefully\n");

    return 0;
}
