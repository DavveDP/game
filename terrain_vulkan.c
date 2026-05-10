typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    u32 n_vertices;
    u32 n_indices;
    pipeline_t pipeline;
} terrain_gpu_t;

typedef struct {
    u32 some_int;
    u32 other_int;
    // terrain uniform data
} UBO_terrain_t;

// Rendering

pipeline_t terrain_create_pipeline(arena_t* arena_permanent, 
        arena_t* arena_scratch, 
        VulkanCtx* ctx, 
        VkDescriptorSetLayout ubo_global_descriptor_set_layout) 
{
    pipeline_t pipeline;
    VkResult res;
    // create uniform buffer (one for each frame)
    u64 ubo_terrain_gpu_stride = (u64)align_up(sizeof(UBO_terrain_t), ctx->physicalDevice.selected->props.limits.minUniformBufferOffsetAlignment);
    {
        // TODO: GPU allocator...
        create_buffer(
                ctx, 
                ubo_terrain_gpu_stride * ctx->render_state.frames_in_flight,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_SHARING_MODE_EXCLUSIVE, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &pipeline.uniform_buffer,
                &pipeline.uniform_memory);
        vkMapMemory(ctx->device.handle, 
                pipeline.uniform_memory, 
                0,
                VK_WHOLE_SIZE, 0, &pipeline.uniform_mapped);
    }

    // descriptor sets
    pipeline.descriptor_sets = alloc_array(arena_permanent, VkDescriptorSet, ctx->render_state.frames_in_flight);
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            {
                .binding = 0, // terrain specific
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
            }
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = LEN(bindings),
            .pBindings = bindings
        };

        res = vkCreateDescriptorSetLayout(ctx->device.handle, &layoutInfo, NULL, &pipeline.descriptor_set_layout);
        assert(res == VK_SUCCESS);

        allocate_descriptor_sets(arena_scratch, 
                ctx->device.handle, 
                ctx->descriptor_pool_permanent, 
                pipeline.descriptor_set_layout, 
                ctx->render_state.frames_in_flight, 
                pipeline.descriptor_sets);

        // fill in descriptor set (table) with info about which buffer to use, offset and range 
        // (should obviously match the layout binding that we will put in the pipeline)
        for (int i = 0; i < ctx->render_state.frames_in_flight; i++) {
            VkDescriptorBufferInfo bufferInfo = {
                .buffer = pipeline.uniform_buffer,
                .offset = ubo_terrain_gpu_stride * i,
                .range = sizeof(UBO_terrain_t)
            };
            VkWriteDescriptorSet descriptorWrite = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = pipeline.descriptor_sets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = &bufferInfo
            };
            // TODO: check what copy params mean and if this can be outside loop in that case
            vkUpdateDescriptorSets(ctx->device.handle, 1, &descriptorWrite, 0, NULL);
        }
    }
    // create pipeline
    {
        // create shader stages
        arena_align(arena_scratch, alignof(u32));
        file_data_t vertex_code   = read_file(arena_scratch, "vertex.spv");
        arena_align(arena_scratch, alignof(u32));
        file_data_t fragment_code = read_file(arena_scratch, "fragment.spv");
        VkPipelineShaderStageCreateInfo stages[] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = create_shader_module(ctx->device.handle, (const u32*)vertex_code.data, vertex_code.size, &res),
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = create_shader_module(ctx->device.handle, (const u32*)fragment_code.data, fragment_code.size, &res),
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
        VkDescriptorSetLayout layouts[] = {ubo_global_descriptor_set_layout, pipeline.descriptor_set_layout};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = LEN(layouts),
            .pSetLayouts = layouts
        };
        res = vkCreatePipelineLayout(ctx->device.handle, &pipelineLayoutInfo, NULL, &pipeline.layout);
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
            .layout = pipeline.layout,
            .renderPass = ctx->renderPass,
            .subpass = 0,
        };
        res = vkCreateGraphicsPipelines(ctx->device.handle, NULL/*TODO:cache required*/, 1, &pipelineInfo, NULL, &pipeline.handle);
        assert(res == VK_SUCCESS);

        // destroy shader modules once ctx->pipeline is created
        for (int i = 0; i < LEN(stages); i++) {
            vkDestroyShaderModule(ctx->device.handle, stages[i].module, NULL);
        }
    }
    return pipeline;
}

#define GRID_SIZE 16000
#define TERRAIN_MAX_Y 1000
#define LOD_COUNT 8

typedef struct {
    float min_lod_dist;
    float k;
} lod_to_dist_params_t;

static inline float lod_to_dist(u8 lod, const lod_to_dist_params_t* p) {
    return p->min_lod_dist * (1 - powf(lod / LOD_COUNT, p->k));
}

static float lod_to_dist_quantized_g[LOD_COUNT];

void lod_to_dist_set_params(const lod_to_dist_params_t* params) {
    for (u8 i = 0; i < LOD_COUNT; i++) {
        lod_to_dist_quantized_g[i] = lod_to_dist(i, params);
    }
}

terrain_gpu_t terrain_init(arena_t* arena_permanent, arena_t* arena_scratch, VulkanCtx* ctx, VkDescriptorSetLayout ubo_global_descriptor_set_layout) {
    const lod_to_dist_params_t default_params = {
        .min_lod_dist = GRID_SIZE/sqrtf(2),
        .k = 3.0f
    };
    lod_to_dist_set_params(&default_params);

    terrain_gpu_t terrain;
    terrain.pipeline = terrain_create_pipeline(arena_permanent, arena_scratch, ctx, ubo_global_descriptor_set_layout);
    // allocate vertices
    {
        mesh_subdiv_plane(5, NULL, 0, &terrain.n_vertices, NULL, &terrain.n_indices);

        // allocate CPU temp buffers
        Vertex_t* vertices = alloc_array(arena_scratch, Vertex_t, terrain.n_vertices);
        u16* indices = alloc_array(arena_scratch, u16, terrain.n_indices);
        u64 total_size = sizeof(*vertices) * terrain.n_vertices + sizeof(*indices) * terrain.n_indices;

        // generate mesh
        mesh_subdiv_plane(5, &vertices->pos, sizeof(*vertices), &terrain.n_vertices, indices, &terrain.n_indices);

        // create gpu buffer (alignment is fine, since u16 will always be aligned after verts)
        create_buffer(ctx,
                total_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &terrain.buffer,
                &terrain.memory);

        // copy all data to staging so that layout is:
        // vertex, ib1, ib2, ...
        u8* p = ctx->staging_buffer_cpu_mem;
        memcpy(p, vertices, terrain.n_vertices * sizeof(*vertices));
        p += terrain.n_vertices * sizeof(*vertices);
        memcpy(p, indices, terrain.n_indices * sizeof(*indices));

        // upload
        staging_buffer_upload_t upload_info = {
            .copyRegion = (VkBufferCopy){0, 0, total_size},
            .src = ctx->staging_buffer,
            .dst = terrain.buffer
        };
        upload_staging_buffer(ctx, &upload_info);
    }
    arena_reset(arena_scratch);
    return terrain;
}

typedef struct {
    float size;
    float x, z;
} qt_node;

static inline void output_instance(qt_node node, u8 buf_index) {
    // write actual instace data to some buffer, maybe set a global?
}

// 0, not visible
// 1, visible but not subdivided further
// 2, visible and subdivided

typedef enum {
    CHUNKED_LOD_RESULT_CULLED,
    CHUNKED_LOD_RESULT_STOPPED,
    CHUNKED_LOD_RESULT_SUBDIVIDED
} ChunkedLodResult;

static u8 fill_instance(qt_node node, vec2 camera_xz_pos, frustum_t* frustum, u8 lod) {
    // frustum cull
    // compute AABB
    aabb_t aabb = {
        .xmin = node.x - node.size/2,
        .xmax = node.x + node.size/2,
        .ymin = 0,
        .ymax = TERRAIN_MAX_Y,
        .zmin = node.z - node.size/2,
        .zmax = node.z + node.size/2,
    };

    if (!frustum_intersects_aabb(frustum, &aabb)) {
        return CHUNKED_LOD_RESULT_CULLED; // culled
    }

    if (lod == LOD_COUNT - 1) {
        return CHUNKED_LOD_RESULT_STOPPED;
    }

    // check if centre of node is too far from the camera, given our LOD level(depth)
    float d = vec2_sq_dist(((vec2){node.x, node.z}), camera_xz_pos);
    float min_dist_for_lod = lod_to_dist_quantized_g[lod];
    if (d >= min_dist_for_lod) {
        assert(d < lod_to_dist_quantized_g[lod+1]); // otherwise we dropped 2 lod levels, breaking the invariant
        return CHUNKED_LOD_RESULT_STOPPED;
    }

    // recurse 4 children

    u8 result[4];
    result[0] = fill_instance((qt_node) {.size = node.size/2, .x = node.x - node.size/4, .z = node.z - node.size/4}, camera_xz_pos, frustum, lod+1);
    result[1] = fill_instance((qt_node) {.size = node.size/2, .x = node.x + node.size/4, .z = node.z - node.size/4}, camera_xz_pos, frustum, lod+1);
    result[2] = fill_instance((qt_node) {.size = node.size/2, .x = node.x - node.size/4, .z = node.z + node.size/4}, camera_xz_pos, frustum, lod+1);
    result[3] = fill_instance((qt_node) {.size = node.size/2, .x = node.x + node.size/4, .z = node.z + node.size/4}, camera_xz_pos, frustum, lod+1);
    // at least one child should be non-culled, else we would ourselves be culled
    assert(result[0] | result[1] | result[2] | result[3]); 

    // children are indexed:
    // 0 = (-x, -z)  1 = (x, -z)
    // 2 = (-x,  z)  3 = (x,  z)

    static const u8 neighbour_x[4] = { 1, 0, 3, 2 };
    static const u8 neighbour_z[4] = { 2, 3, 0, 1 };

    for (u8 i = 0; i < 4; i++) {
        if (result[i] != CHUNKED_LOD_RESULT_STOPPED) continue; // ignore culled and subdivided
        u8 nx = result[neighbour_x[i]];
        u8 nz = result[neighbour_z[i]];

        bool finer_x = (nx == CHUNKED_LOD_RESULT_SUBDIVIDED);
        bool finer_z = (nz == CHUNKED_LOD_RESULT_SUBDIVIDED);

        /* Index buffer layouts (0 normal indices, 1 stitched)
         * 0    1    2    3    4    5    6    7    8
         * 0 0  0 1  1 0  0 0  1 1  0 1  1 0  1 1  1 1
         * 0 0  0 1  1 0  1 1  0 0  1 1  1 1  0 1  1 0
         */

        int buf_index;
        int key = (finer_x << 1) | finer_z;
        switch (key) {
            case 0b00: buf_index = 0;            break;
            case 0b01: buf_index = 3 + (i >> 1); break; // z finer
            case 0b10: buf_index = 1 + (i & 1);  break; // x finer
            case 0b11: buf_index = 5 + i;        break; // corner
            default: assert(false);              break;
        }
        output_instance(node, buf_index);
    }

    return CHUNKED_LOD_RESULT_SUBDIVIDED;
}

void terrain_fill_instance_data(camera_t* cam) {
    vec2 fwd = vec2_normalized((vec2){cam->transform.pos.x, cam->transform.pos.z});
    float corner_x = fwd.x >= 0
        ? (floorf(cam->transform.pos.x / GRID_SIZE) + 1) * GRID_SIZE
        : (floorf(cam->transform.pos.x / GRID_SIZE)    ) * GRID_SIZE;
    float corner_z = fwd.y >= 0
        ? (floorf(cam->transform.pos.z / GRID_SIZE) + 1) * GRID_SIZE
        : (floorf(cam->transform.pos.z / GRID_SIZE)    ) * GRID_SIZE;

    qt_node root = {
        .size = GRID_SIZE * 2,
        .x = corner_x, 
        .z = corner_z
    };

    frustum_t f = camera_get_frustum(cam);
    fill_instance(root, (vec2){cam->transform.pos.x, cam->transform.pos.z}, &f, 0);
}
