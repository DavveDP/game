#define _POSIX_C_SOURCE 199309L // required for clock_gettime

// Essentials from C stdlib
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <defines.h>

// My platform agnostic files
#include <math.c> // links with math
#include <mesh.c> // plane subdivision
#include <game.h>
#include <game.c>

// Begin Platfrom specific code

#include <linux_alloc.c>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <game_vulkan.c> 

#define MAX_FRAMES_IN_FLIGHT 3

// Mouse input

Cursor invisible_cursor;

// raw input data
typedef struct {
    int majorOpcode, eventBase, errorBase;
} XI_t;

XI_t xi;

typedef struct {
    float prev_x, prev_y, x, y;
} cursor_position_t;

cursor_position_t cursor_position;

void mouse_moved_this_frame(float* dx, float* dy) {
    *dx = cursor_position.x - cursor_position.prev_x;
    *dy = cursor_position.y - cursor_position.prev_y;
}

static void end_of_frame_mouse_update(void) {
    cursor_position.prev_x = cursor_position.x;
    cursor_position.prev_y = cursor_position.y;
}

// Keyboard input

enum X11_KEY_CODES_t {
    X11_KEY_CODE_W = 25,
    X11_KEY_CODE_A = 38,
    X11_KEY_CODE_S = 39,
    X11_KEY_CODE_D = 40,
    X11_KEY_CODE_UP = 111,
    X11_KEY_CODE_LEFT = 113,
    X11_KEY_CODE_RIGHT = 114,
    X11_KEY_CODE_DOWN = 116,
    X11_KEY_CODE_LSHIFT = 50,
    X11_KEY_CODE_SPACE = 65,
    //...
};

u8 btn_to_key_code_table[256];
static void init_btn_key_code_table(void) {
    btn_to_key_code_table[BTN_SUBDIV_INC] = X11_KEY_CODE_UP;
    btn_to_key_code_table[BTN_SUBDIV_DEC] = X11_KEY_CODE_DOWN;
    btn_to_key_code_table[BTN_MOVE_FORWARD] = X11_KEY_CODE_W;
    btn_to_key_code_table[BTN_MOVE_LEFT] = X11_KEY_CODE_A;
    btn_to_key_code_table[BTN_MOVE_BACKWARD] = X11_KEY_CODE_S;
    btn_to_key_code_table[BTN_MOVE_RIGHT] = X11_KEY_CODE_D;
    btn_to_key_code_table[BTN_MOVE_DOWN] = X11_KEY_CODE_LSHIFT;
    btn_to_key_code_table[BTN_MOVE_UP] = X11_KEY_CODE_SPACE;
}

u8 key_states[256];

enum KEY_STATE {
    KEY_STATE_RELEASED_THIS_UPDATE = 1,
    KEY_STATE_PRESSED_THIS_UPDATE = 2,
    KEY_STATE_HELD = 3
};

bool btn_released(Button btn) {
    u8 key = btn_to_key_code_table[btn];
    return key_states[key] == KEY_STATE_RELEASED_THIS_UPDATE;
}

bool btn_pressed(Button btn) {
    u8 key = btn_to_key_code_table[btn];
    return key_states[key] == KEY_STATE_PRESSED_THIS_UPDATE;
}

bool btn_held(Button btn) {
    u8 key = btn_to_key_code_table[btn];
    return key_states[key] == KEY_STATE_HELD;
}

bool btn_held_or_pressed(Button btn) {
    u8 key = btn_to_key_code_table[btn];
    return key_states[key] & 2; // check second bit
}

void update_key_state(int event_type, u8 key) {
    //printf("key: %hhd\n", key);
    // set second bit
    u8* s = &key_states[key];
    switch(event_type) {
        case KeyPress: 
            *s = (*s >> 1) | 2;
            break; // 1
        case KeyRelease: 
            *s = *s != 0;
            break; // 0
    }
}

void end_of_frame_key_update(void) {
    for (int i = 0; i < 256; i++) {
        u8 *s = &key_states[i];
        *s = (*s >> 1) | (*s & 2);
    }
}

u64 get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// TODO: figure out how to pass generic allocators, 
// tagged union for allocator types?
u8* read_file(arena_t* allocator, const char* path, size_t* out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }

    size_t size = st.st_size;
    u8* data = alloc_array(allocator, u8, size);
    if (!data) {
        close(fd);
        return NULL;
    }

    FILE* f = fdopen(fd, "rb");
    if (!f) {
        close(fd);
        return NULL;
    }
    size_t n = fread(data, 1, size, f);
    fclose(f);

    if (n != size) {
        return NULL;
    }
    if (out_size) *out_size = size;
    return data;
}

Atom WM_DELETE_WINDOW;
Atom NET_WM_PING;
Atom WM_PROTOCOLS;

volatile sig_atomic_t running = 1;

void sigint_and_sigterm_handler(int sig) {
    running = 0;
}

int main(int argc, char** argv) {
    //world_t world;
    //world.main_camera = camera_looking_at_from((vec3){0,0,0}, (vec3){0,2,0,});
    //quat_print(world.main_camera.transform.rot, printf);
    //mat4 m = transform_to_view_matrix(&world.main_camera.transform);
    //mat4_print(&m, printf);
    //return 0;
    // Signal handling
    signal(SIGINT, sigint_and_sigterm_handler);
    signal(SIGTERM, sigint_and_sigterm_handler);
    init_btn_key_code_table();

    // Our only alloc call, (see allocators)
    void* mem = mmap(NULL, GB(1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // create app arena
    arena_t appArena;
    arena_init(&appArena, mem, GB(1));

    // create scratchArena (nested arena)
    arena_t scratchArena;
    arena_init(&scratchArena, 
            alloc_array(&appArena, u8, MB(16)),
            MB(16)
    );

    VulkanCtx ctx;
    // create vulkan instance
    VkResult res = vulkan_create_instance(&ctx.instance);
    assert(res == VK_SUCCESS);

    printf("instance created!\n");

    // x11_init function maybe?
    //
    // platform specific stuff to create surface and init input
    Display* display;
    Window window;
    Window root;
    {
        display = XOpenDisplay(NULL);
        root = RootWindow(display, DefaultScreen(display));

        // assert xkb support, disabling auto release events
        int supported;
        XkbSetDetectableAutoRepeat(display, true, &supported);
        assert(supported);

        window = XCreateSimpleWindow(
                display, 
                root,
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

        // create invisible cursor
        Pixmap bm_no;
        XColor black;
        const char no_data[] = {0,0,0,0,0,0,0,0};
        bm_no = XCreateBitmapFromData(display, window, no_data, 8, 8);
        invisible_cursor = XCreatePixmapCursor(display, bm_no, bm_no, &black, &black, 0, 0);

        // enable raw mouse input events
        assert(XQueryExtension(display, "XInputExtension", 
                &xi.majorOpcode,
                &xi.eventBase,
                &xi.errorBase));
        int major = 2, minor = 0;

        assert(XIQueryVersion(display, &major, &minor) == Success);

        unsigned char mask[XIMaskLen(XI_RawMotion)] = {0};
        XISetMask(mask, XI_RawMotion);

        XIEventMask emask = {
            .deviceid = XIAllMasterDevices,
            .mask_len = sizeof(mask),
            .mask = mask
        };

        XISelectEvents(display, root, &emask, 1);
    }

    // vulkan init
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
            vkGetPhysicalDeviceProperties(handles[i], &validDevice->props);
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
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(handles[i], ctx.surface, &validDevice->surfaceCapabilities);
            if (res != VK_SUCCESS) continue;

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
            // additional features
            VkPhysicalDeviceFeatures features;
            vkGetPhysicalDeviceFeatures(handles[i], &features);
            if (!features.fillModeNonSolid) continue;

            // this is a valid device
            printf("Found device: %s\n", validDevice->props.deviceName);
            // first device is assigned to the ptr
            if (ctx.physicalDevice.count == 0) {
                ctx.physicalDevice.all = validDevice;
            }
            ctx.physicalDevice.count++;
            // advance arena mark, ie "lock in" the device
            validDeviceMark = arena_mark(&appArena); 
        }
    }
    arena_reset(&scratchArena);
    assert(ctx.physicalDevice.all != NULL && ctx.physicalDevice.count > 0);
    // select device, can be changed later based on some scoring I guess
    ctx.physicalDevice.selected = &ctx.physicalDevice.all[0];
    printf("Selected device: %s\n", ctx.physicalDevice.selected->props.deviceName);
    vulkan_create_logical_device(&ctx);

    // get image count here to determine max frames in flight, does that even make sense?
    u32 imageCount = get_surface_capabilities_image_count(&ctx);
    u32 framesInFlight = MIN(imageCount, MAX_FRAMES_IN_FLIGHT);

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


    // create uniform buffers before pipeline kinda makes sense right?
    // or at least close to one another.
    // create uniform buffer (one for each frame)
    VkBuffer* uniformBuffers = alloc_array(&appArena, VkBuffer, framesInFlight);
    VkDeviceMemory* uniformBuffersMemory = alloc_array(&appArena, VkDeviceMemory, framesInFlight);
    void** uniformBuffersCPUMapped = alloc_array(&appArena, void*, framesInFlight);
    {
        for (int i = 0; i < framesInFlight; i++) {
            VkDeviceSize size = sizeof(UniformBufferObject);
            create_buffer(
                    &ctx, 
                    sizeof(UniformBufferObject),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                    VK_SHARING_MODE_EXCLUSIVE, 
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uniformBuffers + i,
                    uniformBuffersMemory + i);
            vkMapMemory(ctx.device.handle, uniformBuffersMemory[i], 0, size, 0, uniformBuffersCPUMapped + i);
            memset(uniformBuffersCPUMapped[i], 0, sizeof(UniformBufferObject));
        }
    }

    // descriptor sets
    VkDescriptorSetLayout descriptorSetLayout; // for pipeline
    VkDescriptorPool descriptorPool;
    VkDescriptorSet* descriptorSets = alloc_array(&appArena, VkDescriptorSet, framesInFlight);
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &uboLayoutBinding
        };

        res = vkCreateDescriptorSetLayout(ctx.device.handle, &layoutInfo, NULL, &descriptorSetLayout);
        assert(res == VK_SUCCESS);

        // descriptor pools
        VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = framesInFlight
        };
        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = framesInFlight,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize
        };

        res = vkCreateDescriptorPool(ctx.device.handle, &poolInfo, NULL, &descriptorPool);
        assert(res == VK_SUCCESS);

        // Allocate the same descriptor set for each frame
        VkDescriptorSetLayout* layouts = alloc_array(&scratchArena, VkDescriptorSetLayout, framesInFlight);
        for (int i = 0; i < framesInFlight; i++) {
            layouts[i] = descriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo setAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = framesInFlight,
            .pSetLayouts = layouts
        };
        res = vkAllocateDescriptorSets(ctx.device.handle, &setAllocInfo, descriptorSets);
        assert(res == VK_SUCCESS);

        // fill in descriptor set (table) with info about which buffer to use, offset and range 
        // (should obviously match the layout binding that we will put in the pipeline)
        for (int i = 0; i < framesInFlight; i++) {
            VkDescriptorBufferInfo bufferInfo = {
                .buffer = uniformBuffers[i],
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };
            VkWriteDescriptorSet descriptorWrite = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = &bufferInfo
            };
            // TODO: check what copy params mean and if this can be outside loop in that case
            vkUpdateDescriptorSets(ctx.device.handle, 1, &descriptorWrite, 0, NULL);
        }
    }
    arena_reset(&scratchArena);

    // create ctx.pipeline
    {
        // create shader stages

        size_t vertexCodeSize = 0;
        u8* vertexCode = read_file(&scratchArena, "vertex.spv", &vertexCodeSize);
        assert(vertexCode != NULL);
        size_t fragmentCodeSize = 0;
        u8* fragmentCode = read_file(&scratchArena, "fragment.spv", &fragmentCodeSize);
        assert(fragmentCode != NULL);

        VkPipelineShaderStageCreateInfo stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = create_shader_module(ctx.device.handle, (const u32*)vertexCode, vertexCodeSize, &res),
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = create_shader_module(ctx.device.handle, (const u32*)fragmentCode, fragmentCodeSize, &res),
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
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(Vertex_t, pos)     }
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
            .polygonMode = VK_POLYGON_MODE_LINE,
            .cullMode = VK_CULL_MODE_NONE,
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

        // Pipeline layout (descriptor sets)
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout
        };
        res = vkCreatePipelineLayout(ctx.device.handle, &pipelineLayoutInfo, NULL, &ctx.pipelineLayout);
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
            .layout = ctx.pipelineLayout,
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

    init_staging_buffers(&ctx);

    // allocate vertices
    const u8 n_index_buffers = 8; // subdiv count + 1
    u32 n_vertices = 0;
    u32* n_indices = NULL;
    VkBuffer patch_buffer;
    VkDeviceMemory patch_buffer_gpu_mem;
    {
        u8 n_subdiv = n_index_buffers - 1;
        n_indices = alloc_array(&appArena, u32, n_index_buffers);
        subdiv_plane(n_subdiv, NULL, 0, &n_vertices, NULL, n_indices);

        // allocate CPU temp buffers and count total size
        u64 total_size = n_vertices * sizeof(Vertex_t);
        Vertex_t* vertices = alloc_array(&scratchArena, Vertex_t, n_vertices);
        printf("total_size (verts): %lu\n", total_size);

        u16** indices = alloc_array(&scratchArena, u16*, n_index_buffers);
        for (u16 i = 0; i < n_index_buffers; i++) {
            indices[i] = alloc_array(&scratchArena, u16, n_indices[i]);
            total_size += n_indices[i] * sizeof(u16);
            printf("indices %hu: %u\n", i, n_indices[i]);
        }
        printf("total_size (verts + indices): %lu\n", total_size);

        // generate mesh
        subdiv_plane(n_subdiv, &vertices->pos, sizeof(*vertices), &n_vertices, indices, n_indices);

        // create gpu buffer
        create_buffer(&ctx,
                total_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &patch_buffer,
                &patch_buffer_gpu_mem);

        // copy all data to staging so that layout is:
        // vertex, ib1, ib2, ...
        memcpy(ctx.stagingBuffer_cpu_mem, vertices, n_vertices * sizeof(*vertices));
        u8* p = ctx.stagingBuffer_cpu_mem + n_vertices * sizeof(*vertices);
        for (u16 i = 0; i < n_index_buffers; i++) {
            memcpy(p, indices[i], n_indices[i] * sizeof(**indices));
            p += n_indices[i] * sizeof(**indices);
        }

        // upload
        staging_buffer_upload_t upload_info = {
            .copyRegion = (VkBufferCopy){0, 0, total_size},
            .src = ctx.stagingBuffer,
            .dst = patch_buffer
        };
        upload_staging_buffer(&ctx, &upload_info, transientPool);
    }
    arena_reset(&scratchArena);

    // Init window

    // show the window, should activate the WM
    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | 
            KeyPressMask | KeyReleaseMask |
            PointerMotionMask | FocusChangeMask);

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
    VkSemaphore* renderFinishedSemaphores = alloc_array(&appArena, VkSemaphore, imageCount);
    VkFence* inFlightFences = alloc_array(&appArena, VkFence, framesInFlight);
    {
        VkSemaphoreCreateInfo semInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT // first wait doesn't block
        };

        // image available and fences are tied to frame lifetime
        for (int i = 0; i < framesInFlight; i++) {
            res = vkCreateSemaphore(ctx.device.handle, &semInfo, NULL, &imageAvailableSemaphores[i]);
            assert(res == VK_SUCCESS);
            res = vkCreateFence(ctx.device.handle, &fenceInfo, NULL, &inFlightFences[i]);
            assert(res == VK_SUCCESS);
        }

        // render finished isn't and must be indexed based on what acquire returns
        for (int i = 0; i < imageCount; i++) {
            res = vkCreateSemaphore(ctx.device.handle, &semInfo, NULL, &renderFinishedSemaphores[i]);
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
    u64 start_time = get_time_ns();
    u64 curr_time = start_time;

    game_state_t game_state;
    game_state.main_camera = camera_looking_at_from((vec3){0.5f,0,0.5f}, (vec3){2,2,2});
    game_state.camera_speed = vec3_zero;
    game_state.current_subdiv = 1;
    float delta_time = 0;
    bool window_focused = false;

update_start:
    while(running) {
        u64 new_time = get_time_ns();
        float delta_time = ((float)(new_time - curr_time)/ 1000000000ull);
        curr_time = new_time; 
        float time = ((float)(curr_time - start_time)/ 1000000000ull);

        // wait for next sync objects
        VkFence fence = inFlightFences[frame];
        VkSemaphore imageAvailableSemaphore = imageAvailableSemaphores[frame];
        VkCommandBuffer cmdBuf = cmdBufs[frame];
        // wait for submission frame i - framesInFlight
        // means that semaphores and commandbuffers above are no longer in use by CPU
        vkWaitForFences(ctx.device.handle, 1, &fence, VK_TRUE, UINT64_MAX);

        // bookkeeping
        if (swapchainCooldown > 0) swapchainCooldown--;

        // pump x events
        while (XPending(display)) {
            XNextEvent(display, &e);
            switch (e.type) {
                case ConfigureNotify:
                    {
                        if(swapchainCooldown == 0 && (e.xconfigure.width != ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.width
                                    || e.xconfigure.height != ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.height)) {
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
                                goto update_start;
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
                    // handle keyboard input
                case KeyPress:
                case KeyRelease:
                    {
                        u8 code = (u8)e.xkey.keycode;
                        update_key_state(e.type, code);
                    }
                    break;
                    // capture pointer on focus
                case FocusIn:
                    {
                        // ignore events from popup windows, GLFW does this
                        if (e.xfocus.mode == NotifyGrab || 
                                e.xfocus.mode == NotifyUngrab) {
                            break;
                        }
                        window_focused = true;
                        XDefineCursor(display, window, invisible_cursor);
                    }
                    break;

                case FocusOut:
                    {
                        // ignore events from popup windows, GLFW does this
                        if (e.xfocus.mode == NotifyGrab || 
                                e.xfocus.mode == NotifyUngrab) {
                            break;
                        }
                        window_focused = false;
                        XUndefineCursor(display, window);
                    }
                    // handle mouse motion
                //case MotionNotify:
                //    {
                //        cursor_position.x = e.xmotion.x;
                //        cursor_position.y = e.xmotion.y;
                //        break;
                //    }
                case GenericEvent:
                    {
                        if (window_focused &&
                            e.xcookie.extension == xi.majorOpcode &&
                            XGetEventData(display, &e.xcookie) &&
                            e.xcookie.evtype == XI_RawMotion) 
                        {
                            XIRawEvent* re = e.xcookie.data;
                            if (re->valuators.mask_len) {
                                const double* values = re->raw_values;
                                if (XIMaskIsSet(re->valuators.mask, 0)) {
                                    cursor_position.x += (float)*values;
                                    values++;
                                }
                                if (XIMaskIsSet(re->valuators.mask, 0)) {
                                    cursor_position.y += (float)*values;
                                }
                            }
                            XFreeEventData(display, &e.xcookie);
                        }
                    }
                    break;

            }
            // TODO: handle mouse input events
        }

        //printf("fps: %f\n", 1/delta_time);
        //vec3_print(game_state.main_camera.transform.pos, printf);
        game_update(&game_state, delta_time, n_index_buffers);
        // warp cursor back to centre
        if (window_focused) {
            XWarpPointer(display, None, window, 0, 0, 0, 0, window_width(&ctx) / 2, window_height(&ctx) / 2);
        }
        end_of_frame_mouse_update();
        end_of_frame_key_update();

        // render start
        // early skip render
        if (recreateSwapchain || 
                VK_ERROR_OUT_OF_DATE_KHR == vkAcquireNextImageKHR(
                    ctx.device.handle, ctx.device.swapchain->handle, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex)) 
        {
            vkDeviceWaitIdle(ctx.device.handle);
            // update capabilities (extents)
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice.selected->handle, ctx.surface, &ctx.physicalDevice.selected->surfaceCapabilities);
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
        } else assert(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR);

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
            .renderArea.extent = ctx.physicalDevice.selected->surfaceCapabilities.currentExtent,
            .clearValueCount = 1,
            .pClearValues = &clearColor
        };
        vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);
        // dynamic states
        VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width  = ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.width,
            .height = ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

        VkRect2D scissor = {
            .offset = {0, 0},
            .extent = ctx.physicalDevice.selected->surfaceCapabilities.currentExtent
        };

        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
        VkBuffer vertexBuffers = {patch_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffers, offsets);
        u64 index_offset = n_vertices * sizeof(Vertex_t);
        for (u16 i = 0; i < game_state.current_subdiv; i++) {
            index_offset += n_indices[i] * sizeof(u16);
        }

        vkCmdBindIndexBuffer(cmdBuf, patch_buffer, index_offset, VK_INDEX_TYPE_UINT16);
        // update uniforms and bind them
        {
            UniformBufferObject* uniform = (UniformBufferObject*)uniformBuffersCPUMapped[frame];
            uniform->time = time;

            // recompute projection based on screen size
            float aspect = (float)ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.width / 
                ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.height;
            float fov_rads = PI/4;
            mat4_perspective(&game_state.main_camera.proj, fov_rads, aspect, 0.1f, 100.0f);

            // model rotation for fun :)
            float rot_speed = 0.0f;
            float rotation = 2 * PI * time;
            rotation *= rot_speed;
            mat4 model = mat4_rotation(rotation, (vec3){0.0f, 1.0f, 0.0f});

            // write ubo data
            uniform->model = model;
            // camera
            uniform->proj = game_state.main_camera.proj;
            uniform->view = transform_to_view_matrix(&game_state.main_camera.transform);
            // for debug

            //vec4 vi = mat4_apply(&uniform->view, (vec4){1,0,0,1});
            //vec4 clip = mat4_apply(&uniform->proj, vi);
            //vec3 ndc = {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
            //printf("vi: %f %f %f %f\n", vi.x, vi.y, vi.z, vi.w);
            //printf("clip: %f %f %f %f\n", clip.x, clip.y, clip.z, clip.w);
            //printf("ndc: %f %f %f\n", ndc.x, ndc.y, ndc.z);
        }
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipelineLayout, 0, 1, &descriptorSets[frame], 0,  NULL);
        vkCmdDrawIndexed(cmdBuf, n_indices[game_state.current_subdiv], 1, 0, 0, 0);
        vkCmdEndRenderPass(cmdBuf);
        res = vkEndCommandBuffer(cmdBuf);
        assert(res == VK_SUCCESS);


        VkSemaphore renderFinishedSemaphore = renderFinishedSemaphores[imageIndex];
        // do I need an additional fence here then, before passing it to submit?

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
