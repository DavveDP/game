 #include <mesh.c> // plane subdivision
// Rendering

typedef struct {
    vec3 pos;
} terrain_vertex_t;


#define TERRAIN_MAX_INSTANCES 4096
#define TERRAIN_MAX_SPLINE_SEGMENTS 128 // across all terrain splines
#define GRID_SIZE 2048
#define TERRAIN_MAX_Y 150
#define LOD_COUNT 10

typedef struct {
    float min_lod_dist;
    float k;
} lod_to_dist_params_t;

typedef struct {
    float x, z;
    float size;
    u8 buf_index;
    // padding...
} terrain_instance_data_t;


//static inline float lod_to_sq_dist(u8 lod, const lod_to_dist_params_t* p) {
//    float d = p->min_lod_dist * (1 - powf((float)lod / LOD_COUNT, p->k));
//    return d*d;
//}

static float lod_to_sq_dist_quantized_g[LOD_COUNT];

void lod_to_sq_dist_set_params(float finest_dist) {
    for (u8 i = 0; i < LOD_COUNT; i++) {
        u8 levels_from_finest = (LOD_COUNT - 1) - i;
        float d = finest_dist * (float)(1 << levels_from_finest);
        lod_to_sq_dist_quantized_g[i] = d * d;
    }
}
//void lod_to_sq_dist_set_params(const lod_to_dist_params_t* params) {
//    for (u8 i = 0; i < LOD_COUNT; i++) {
//        lod_to_sq_dist_quantized_g[i] = lod_to_sq_dist(i, params);
//    }
//}
//

void create_shaded_pipeline(
        VulkanCtx* ctx, 
        VkDescriptorSetLayout* descriptor_set_layouts, 
        u32 descriptor_set_layout_count, 
        file_data_t vert_shader, file_data_t frag_shader,
        bool wireframe,
        VkPipeline* out_pipeline,
        VkPipelineLayout* out_layout) 
{
    VkResult res;
    // create shader stages
    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = create_shader_module(ctx->device.handle, (const u32*)vert_shader.data, vert_shader.size, &res),
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = create_shader_module(ctx->device.handle, (const u32*)frag_shader.data, frag_shader.size, &res),
            .pName = "main"
        }
    };
    assert(res == VK_SUCCESS);

    // vertex input (single vertex buffer)
    VkVertexInputBindingDescription vertexBindings[] = {
        { .binding = 0, .stride = sizeof(terrain_vertex_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
    };

    VkVertexInputAttributeDescription vertexAttr[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,    .offset = offsetof(terrain_vertex_t, pos)     }
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
        .depthClampEnable = VK_TRUE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
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
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };

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
        .setLayoutCount = descriptor_set_layout_count,
        .pSetLayouts = descriptor_set_layouts
    };
    res = vkCreatePipelineLayout(ctx->device.handle, &pipelineLayoutInfo, NULL, out_layout);
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
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &blendStateInfo,
        .pDynamicState = &dynamicStateInfo,
        // skipping dynamic state
        .layout = *out_layout,
        .renderPass = ctx->render_state.render_pass_main,
        .subpass = 0,
    };
    // TODO: create all pipelines at once instead
    res = vkCreateGraphicsPipelines(ctx->device.handle, NULL/*TODO:cache required*/, 1, &pipelineInfo, NULL, out_pipeline);
    assert(res == VK_SUCCESS);

    // destroy shader modules once ctx->pipeline is created
    for (int i = 0; i < LEN(stages); i++) {
        vkDestroyShaderModule(ctx->device.handle, stages[i].module, NULL);
    }
}

void terrain_init(
        terrain_gpu_t* terrain,
        arena_t* arena_permanent, 
        arena_t* arena_scratch, 
        VulkanCtx* ctx, 
        VkDescriptorSetLayout ubo_global_descriptor_set_layout, 
        file_data_t vert_shader, 
        file_data_t frag_shader) 
{
    terrain->ubo_noise = alloc_array(arena_permanent, gpu_buffer_alloc_t, ctx->render_state.frames_in_flight);
    terrain->ssbo_spline_segments = alloc_array(arena_permanent, gpu_buffer_alloc_t, ctx->render_state.frames_in_flight);
    terrain->ssbo_instance_data = alloc_array(arena_permanent, gpu_buffer_alloc_t, ctx->render_state.frames_in_flight);
    for (u32 i = 0; i < ctx->render_state.frames_in_flight; i++) {
        // alloc uniform buffer (one for each frame)
        terrain->ubo_noise[i] = gpu_buffer_arena_alloc(&ctx->arena_ubo_ssbo, sizeof(UBO_terrain_noise_t));
        // alloc spline segment buffer (for terrain shaping)
        terrain->ssbo_spline_segments[i] = gpu_buffer_arena_alloc(&ctx->arena_ubo_ssbo, sizeof(terrain_spline_segment_t) * TERRAIN_MAX_SPLINE_SEGMENTS);
        // alloc instance data buffer
        terrain->ssbo_instance_data[i] = gpu_buffer_arena_alloc(&ctx->arena_ubo_ssbo, sizeof(terrain_instance_data_t) * TERRAIN_MAX_INSTANCES);
    }

    VkResult res;
    // descriptor sets
    terrain->descriptor_sets = alloc_array(arena_permanent, VkDescriptorSet, ctx->render_state.frames_in_flight);
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            // noise
            {
                .binding = 0, // terrain specific
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
            },
            // spline segments
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
            },
            // instance data
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
            }
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = LEN(bindings),
            .pBindings = bindings
        };

        res = vkCreateDescriptorSetLayout(ctx->device.handle, &layoutInfo, NULL, &terrain->descriptor_set_layout);
        assert(res == VK_SUCCESS);

        allocate_descriptor_sets(arena_scratch, 
                ctx->device.handle, 
                ctx->descriptor_pool_permanent, 
                terrain->descriptor_set_layout, 
                ctx->render_state.frames_in_flight, 
                terrain->descriptor_sets);

        // fill in descriptor set (table) with info about which buffer to use, offset and range 
        // (should obviously match the layout binding that we will put in the pipeline)
        for (int i = 0; i < ctx->render_state.frames_in_flight; i++) {
            VkDescriptorBufferInfo ubo_buffer_info = {
                .buffer = terrain->ubo_noise[i].arena->buffer,
                .offset = terrain->ubo_noise[i].offset,
                .range = sizeof(UBO_terrain_noise_t)
            };
            VkDescriptorBufferInfo ssbo_spline_segments_buffer_info = {
                .buffer = terrain->ssbo_spline_segments[i].arena->buffer,
                .offset = terrain->ssbo_spline_segments[i].offset,
                .range = sizeof(terrain_spline_segment_t) * TERRAIN_MAX_SPLINE_SEGMENTS
            };
            VkDescriptorBufferInfo ssbo_buffer_info = {
                .buffer = terrain->ssbo_instance_data[i].arena->buffer,
                .offset = terrain->ssbo_instance_data[i].offset,
                .range = VK_WHOLE_SIZE
            };
            VkWriteDescriptorSet descriptor_writes[] = 
            {
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = terrain->descriptor_sets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &ubo_buffer_info
                },
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = terrain->descriptor_sets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &ssbo_spline_segments_buffer_info
                },
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = terrain->descriptor_sets[i],
                    .dstBinding = 2,
                    .dstArrayElement = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .pBufferInfo = &ssbo_buffer_info
                }
            };
            // TODO: check what copy params mean and if this can be outside loop in that case
            vkUpdateDescriptorSets(ctx->device.handle, LEN(descriptor_writes), descriptor_writes, 0, NULL);
        }
    }

    // create pipeline
    {
        VkDescriptorSetLayout descriptor_set_layouts[] = {ubo_global_descriptor_set_layout, terrain->descriptor_set_layout};
        terrain->pipelines = alloc_array(arena_permanent, VkPipeline, TERRAIN_PIPELINE_TYPE_COUNT);
        terrain->pipeline_layouts = alloc_array(arena_permanent, VkPipelineLayout, TERRAIN_PIPELINE_TYPE_COUNT);
        create_shaded_pipeline(ctx, 
                descriptor_set_layouts, LEN(descriptor_set_layouts), 
                vert_shader, frag_shader, 
                false, 
                &terrain->pipelines[TERRAIN_PIPELINE_SHADED],
                &terrain->pipeline_layouts[TERRAIN_PIPELINE_SHADED]);
        create_shaded_pipeline(ctx, 
                descriptor_set_layouts, LEN(descriptor_set_layouts), 
                vert_shader, frag_shader, 
                true, 
                &terrain->pipelines[TERRAIN_PIPELINE_WIREFRAME],
                &terrain->pipeline_layouts[TERRAIN_PIPELINE_WIREFRAME]);
    }

    // allocate vertices
    {
        // generate vertices
        mesh_subdiv_plane(5, NULL, 0, &terrain->n_vertices, NULL, NULL);
        terrain_vertex_t* vertices = alloc_array(arena_scratch, terrain_vertex_t, terrain->n_vertices);
        mesh_subdiv_plane(5, &vertices->pos, sizeof(*vertices), &terrain->n_vertices, NULL, NULL);

        // shift all vertices from xz [0-1] to xz [-0.5, 0.5] so that it matches quadtree node center
        for (u32 i = 0; i < terrain->n_vertices; i++) {
            vertices[i].pos.x -= 0.5f;
            vertices[i].pos.z -= 0.5f;
        }

        // generate indices
        mesh_create_chunked_lod_instances(5, NULL, terrain->n_indices);
        u32 n_total_indices = 0;
        for (u32 i = 0; i < 9; i++) { n_total_indices += terrain->n_indices[i]; }
        u16* indices = alloc_array(arena_scratch, u16, n_total_indices);
        mesh_create_chunked_lod_instances(5, indices, terrain->n_indices);
        n_total_indices = 0;
        for (u32 i = 0; i < 9; i++) { n_total_indices += terrain->n_indices[i]; }
        // copy all data to staging so that layout is:
        // vertex, ib1, ib2, ...
        u64 size_vertices = sizeof(*vertices) * terrain->n_vertices;
        u64 size_indices = sizeof(*indices) * n_total_indices;

        gpu_buffer_alloc_t staging_vertices = gpu_buffer_arena_alloc(&ctx->arena_staging, size_vertices);
        gpu_buffer_alloc_t staging_indices = gpu_buffer_arena_alloc_unaligned(&ctx->arena_staging, size_indices);
        terrain->gpu_mem_vertices = gpu_buffer_arena_alloc(&ctx->arena_local_mesh, size_vertices);
        for (u32 i = 0; i < 9; i++) {
            terrain->gpu_mem_indices[i] = gpu_buffer_arena_alloc_unaligned(&ctx->arena_local_mesh, terrain->n_indices[i] * sizeof(*indices));
        }

        memcpy(staging_vertices.mapped, vertices, size_vertices);
        memcpy(staging_indices.mapped, indices, size_indices);

        // upload
        staging_buffer_upload_t upload_info = {
            .copyRegion = (VkBufferCopy){staging_vertices.offset, terrain->gpu_mem_vertices.offset, size_vertices + size_indices},
            .src = staging_vertices.arena->buffer,
            .dst = terrain->gpu_mem_vertices.arena->buffer
        };
        upload_staging_buffer(ctx, &upload_info);
    }
    gpu_buffer_arena_reset(&ctx->arena_staging);
    arena_reset(arena_scratch);
}

typedef struct {
    float size;
    float x, z;
} qt_node;

static u32 instance_count_g;
static u32 instance_index_buf_freq_count_g[9];
static u32 lod_instance_counts_g[LOD_COUNT];
static terrain_instance_data_t instance_data_buffer_g[TERRAIN_MAX_INSTANCES];

// smaller index buffer first
bool terrain_instance_compare_buf_index(u32 a, u32 b) {
    return instance_data_buffer_g[a].buf_index > instance_data_buffer_g[b].buf_index;
}

// smaller size first
bool terrain_instance_compare_size(u32 a, u32 b) {
    return instance_data_buffer_g[a].size > instance_data_buffer_g[b].size;
}

void terrain_instance_swap(u32 a, u32 b) {
    terrain_instance_data_t temp = instance_data_buffer_g[a];
    instance_data_buffer_g[a] = instance_data_buffer_g[b];
    instance_data_buffer_g[b] = temp;
}

static inline void output_instance(qt_node node, u8 lod) {
    instance_data_buffer_g[instance_count_g].x = node.x;
    instance_data_buffer_g[instance_count_g].z = node.z;
    instance_data_buffer_g[instance_count_g].size = node.size;
    instance_data_buffer_g[instance_count_g].buf_index = 0;
    lod_instance_counts_g[lod]++;
    instance_count_g++;
}

// 0, not visible
// 1, visible but not subdivided further
// 2, visible and subdivided

#include <stdio.h>

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

    //if (!frustum_intersects_aabb(frustum, &aabb)) {
    //    return CHUNKED_LOD_RESULT_CULLED; // culled
    //}

    if (lod == LOD_COUNT - 1) {
        output_instance(node, lod);
        return CHUNKED_LOD_RESULT_STOPPED;
    }


    // chebichev
    vec2 diff = {
        fabsf(camera_xz_pos.x - node.x) - node.size/2,
        fabsf(camera_xz_pos.y - node.z) - node.size/2
    };
    float d = fmaxf(0.0f, fmaxf(diff.x, diff.y));
    d = d * d;
    //vec2 center = { node.x, node.z };
    //float d = vec2_sq_dist(center, camera_xz_pos);


    //printf("%f %f %f %f\n", aabb.xmin, aabb.xmax, aabb.zmin, aabb.zmax);
    //printf("lod: %hhu, d: %f, comparing to: %f\n", lod, sqrtf(d), sqrtf(lod_to_sq_dist_quantized_g[lod]));
    float min_dist_for_lod = lod_to_sq_dist_quantized_g[lod];
    // node is too far from the camera, given our LOD level(depth)
    if (d >= min_dist_for_lod) {
        assert(lod > 0);
        output_instance(node, lod);
        //printf("lod: %hhu, d: %f, quant: %f, prev: %f\n", lod, sqrtf(d), sqrtf(lod_to_sq_dist_quantized_g[lod]), sqrtf(lod_to_sq_dist_quantized_g[lod-1]));
        //assert(d < lod_to_sq_dist_quantized_g[lod-1]); // otherwise we dropped 2 lod levels, breaking the invariant
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
    return CHUNKED_LOD_RESULT_SUBDIVIDED;
}

#include <stdio.h>

typedef enum {
    TERRAIN_NEIGHBOUR_NONE  = 0,
    TERRAIN_NEIGHBOUR_RIGHT = 1,
    TERRAIN_NEIGHBOUR_LEFT  = 2,
    TERRAIN_NEIGHBOUR_UP    = 4,
    TERRAIN_NEIGHBOUR_DOWN  = 8
} TerrainNeighbourType;

void terrain_fill_instance_data(camera_t* cam, terrain_instance_data_t* instance_data_buffer) {
    //printf("LOD distances\n");
    //for (u32 i = 0; i < LOD_COUNT; i++) {
    //    printf("%f ", sqrtf(lod_to_sq_dist_quantized_g[i]));
    //}
    //printf("\n");

    // root covers 4 tiles

    // snap pos to nearest grid corner, ie round
    float corner_x = floorf(cam->transform.pos.x / GRID_SIZE + 0.5f) * GRID_SIZE;
    float corner_z = floorf(cam->transform.pos.z / GRID_SIZE + 0.5f) * GRID_SIZE;

    qt_node root = {
        .size = GRID_SIZE * 2,
        .x = corner_x, 
        .z = corner_z
    };
    //printf("qt root: %f, %f, cam: %f %f, grid size: %f\n", root.x, root.z, cam->transform.pos.x, cam->transform.pos.z, (float)GRID_SIZE);

    frustum_t f = camera_get_frustum(cam);
    instance_count_g = 0;
    memset(lod_instance_counts_g, 0, sizeof(lod_instance_counts_g));

    fill_instance(root, (vec2){cam->transform.pos.x, cam->transform.pos.z}, &f, 0);

    assert(instance_count_g > 0);
    // sort based on size
    quicksort(instance_count_g, terrain_instance_compare_size, terrain_instance_swap);


    memset(instance_index_buf_freq_count_g, 0, sizeof(instance_index_buf_freq_count_g));

    const static float epsilon = 0.001f;
    u32 index_fine_start = 0;
    // scan and adjust index buffers based on neighbours
    for (u32 lod = LOD_COUNT - 1; lod > 1; lod--) {  // fine lods
        u32 n_fine   = lod_instance_counts_g[lod];
        u32 n_coarse = lod_instance_counts_g[lod - 1];
        if (n_fine == 0 || n_coarse == 0) { index_fine_start += n_fine; continue; }

        terrain_instance_data_t* fine_tiles   = &instance_data_buffer_g[index_fine_start];
        terrain_instance_data_t* coarse_tiles = &instance_data_buffer_g[index_fine_start + n_fine];
        index_fine_start += n_fine;

        for (u32 f = 0; f < n_fine; f++) {
            u8 neighbour_mask = 0;
            for (u32 c = 0; c < n_coarse; c++) {
                float s = fine_tiles[f].size / 2;
                assert(4 * s == coarse_tiles[c].size);

                float dx = fabsf(fine_tiles[f].x - coarse_tiles[c].x);
                float dz = fabsf(fine_tiles[f].z - coarse_tiles[c].z);

                // borders a coarser lod tile in the x dimension
                if (fabsf(dx - 3*s) < epsilon && fabsf(dz - s) < epsilon) {
                    neighbour_mask |= (coarse_tiles[c].x > fine_tiles[f].x) ? TERRAIN_NEIGHBOUR_RIGHT : TERRAIN_NEIGHBOUR_LEFT;
                }
                if (fabsf(dz - 3*s) < epsilon && fabsf(dx - s) < epsilon) {
                    neighbour_mask |= (coarse_tiles[c].z > fine_tiles[f].z) ? TERRAIN_NEIGHBOUR_DOWN  : TERRAIN_NEIGHBOUR_UP;
                }

            }

            /* Index buffer layouts (0 normal indices, 1 stitched)
             * 0    1    2    3    4    5    6    7    8
             * 0 0  0 1  1 0  0 0  1 1  0 1  1 0  1 1  1 1
             * 0 0  0 1  1 0  1 1  0 0  1 1  1 1  0 1  1 0
             */
            u8 buf_index = 0;
            switch (neighbour_mask) {
                case TERRAIN_NEIGHBOUR_NONE:    buf_index = 0; break;
                case TERRAIN_NEIGHBOUR_RIGHT:   buf_index = 1; break;
                case TERRAIN_NEIGHBOUR_LEFT:    buf_index = 2; break;
                case TERRAIN_NEIGHBOUR_DOWN:    buf_index = 3; break;
                case TERRAIN_NEIGHBOUR_UP:      buf_index = 4; break;
                case TERRAIN_NEIGHBOUR_RIGHT | TERRAIN_NEIGHBOUR_DOWN:
                                                buf_index = 5; break;
                case TERRAIN_NEIGHBOUR_LEFT | TERRAIN_NEIGHBOUR_DOWN:
                                                buf_index = 6; break;
                case TERRAIN_NEIGHBOUR_RIGHT | TERRAIN_NEIGHBOUR_UP:
                                                buf_index = 7; break;
                case TERRAIN_NEIGHBOUR_LEFT | TERRAIN_NEIGHBOUR_UP:
                                                buf_index = 8; break;
                                                //default: assert(0);                            break;
            }
            // modify based on neighbour
            fine_tiles[f].buf_index = buf_index;
        }
    }

    memset(instance_index_buf_freq_count_g, 0, sizeof(instance_index_buf_freq_count_g));
    for (u32 i = 0; i < instance_count_g; i++) {
        instance_index_buf_freq_count_g[instance_data_buffer_g[i].buf_index]++;
    }
}

static bool frozen;
static camera_t frozen_cam;
void terrain_render(VulkanCtx* ctx, game_state_t* game_state, terrain_gpu_t* terrain, VkCommandBuffer cmd_buf, VkDescriptorSet ubo_global_descriptor_set, u32 frame) {
    if (game_state->freeze_terrain) {
        frozen = !frozen;
        frozen_cam = game_state->main_camera;
    } 

    //vec3_print(game_state->main_camera.transform.pos, printf);

    float coarsest_dist = GRID_SIZE * 2; //* sqrtf(2.0f);
    float finest_dist = coarsest_dist / (float)(1 << (LOD_COUNT - 1));
    finest_dist *= 2.0f; // finest LOD radius tuning exp dropoff
    lod_to_sq_dist_set_params(finest_dist);

    // instance generation
    terrain_fill_instance_data(frozen ? &frozen_cam : &game_state->main_camera, instance_data_buffer_g);
    quicksort(instance_count_g, terrain_instance_compare_buf_index, terrain_instance_swap);
    memcpy(terrain->ssbo_instance_data[frame].mapped, instance_data_buffer_g, sizeof(terrain_instance_data_t) * instance_count_g);

    // noise uniforms
    UBO_terrain_noise_t* noise = terrain->ubo_noise[frame].mapped;
    noise->grid_size = GRID_SIZE*2;
    noise->y_scale = 15.0f;

    const float weights[] = {0.65f, 0.25f, 0.1f};
    bezier2d_t a_segments[] = {
        {{0.0f, 0.0f}, {6.667f, 0.0f}, {12.33f, 0.03f}, {18, 0.03f}},
        {{18, 0.03f}, {32.667f, 0.03f}, {47.33f, 0.03f}, {62, 0.03f}},
        {{62, 0.03f}, {64.667f, 0.03f}, {67.33f, 0.378f}, {70, 0.378f}},
        {{70, 0.378f}, {82.33f, 0.378f}, {94.667f, 0.378f}, {107, 0.378f}},
        {{107, 0.378f}, {111.33f, 0.378f}, {115.667f, 0.886f}, {120, 0.887f}},
        {{120, 0.887f}, {123.31f, 0.887f}, {125.907f, 0.9f}, {130, 0.904f}},
        {{130, 0.904f}, {146, 0.948f}, {162.75f, 0.943f}, {178, 0.958f}},
        {{178, 0.958f}, {196.75f, 0.977f}, {214, 0.981f}, {232, 1.0f}},
        //{{0.0f, 0.0f}, {0.33f, 0.0f}, {0.66f, 1.0f}, {1.0f, 1.0f}}
    };
    for (u32 i = 0; i < LEN(a_segments); i++) {
        a_segments[i] = bezier2d_scale(a_segments[i], 1.0/(a_segments[LEN(a_segments) - 1]).p3.x, TERRAIN_MAX_Y * weights[0]);
        ////printf("%u: [p0.x: %f p1.x: %f]", i, a_segments[i].p0.x, a_segments[i].p3.x);
    }
    //printf("\n");
    bezier2d_t b_segments[] = {
        //{{0.0f, 1.0f}, {0.33f, 1.0f}, {0.66f, 1.0f}, {1.0f, 1.0f}},
    };
    for (u32 i = 0; i < LEN(a_segments); i++) {
        //bezier2d_scale(b_segments[i], 1.0/232, TERRAIN_MAX_Y * weights[1]);
    }
    bezier2d_t c_segments[] = {
        //{{0.0f, 1.0f}, {0.33f, 1.0f}, {0.66f, 1.0f}, {1.0f, 1.0f}},
    };
    for (u32 i = 0; i < LEN(c_segments); i++) {
        //bezier2d_scale(b_segments[i], 1.0/232, TERRAIN_MAX_Y * weights[2]);
    }
    UBO_terrain_noise_params_t* A = &noise->A;
    A->first_segment_index = 0;
    A->segment_count = LEN(a_segments);
    UBO_terrain_noise_params_t* B = &noise->B;
    B->first_segment_index = A->segment_count;
    B->segment_count = LEN(b_segments);;
    UBO_terrain_noise_params_t* C = &noise->C;
    C->first_segment_index = A->segment_count + B->segment_count;
    C->segment_count = LEN(c_segments);

    // write segments
    terrain_spline_segment_t* s = terrain->ssbo_spline_segments[frame].mapped;
    for (u32 i = 0; i < LEN(a_segments); i++) {
        s->curve = a_segments[i];
        s++;
    }
    for (u32 i = 0; i < LEN(b_segments); i++) {
        s->curve = b_segments[i];
        s++;
    }
    for (u32 i = 0; i < LEN(c_segments); i++) {
        s->curve = c_segments[i];
        s++;
    }

    // first to CPU buffer, also freq count
    //

    TERRAIN_PIPELINE_TYPE selected_pipeline = game_state->show_wireframe ? TERRAIN_PIPELINE_WIREFRAME : TERRAIN_PIPELINE_SHADED;

    // Draw terrain
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain->pipelines[selected_pipeline]);
    // dynamic states
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width  = ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.width,
        .height = ctx->physicalDevice.selected->surfaceCapabilities.currentExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = ctx->physicalDevice.selected->surfaceCapabilities.currentExtent
    };

    VkDescriptorSet sets[] = {ubo_global_descriptor_set, terrain->descriptor_sets[frame]};
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain->pipeline_layouts[selected_pipeline], 0, LEN(sets), sets, 0,  NULL);

    // patch geometry
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
    VkBuffer vertex_buffers[] = {terrain->gpu_mem_vertices.arena->buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, vertex_buffers, offsets);
    u32 instance_count_acc = 0;

    for (u32 i = 0; i < 9; i++) {
        u64 index_offset = terrain->gpu_mem_indices[i].offset;
        vkCmdBindIndexBuffer(cmd_buf, terrain->gpu_mem_indices[i].arena->buffer, index_offset, VK_INDEX_TYPE_UINT16);
        u64 instance_count = instance_index_buf_freq_count_g[i];
        vkCmdDrawIndexed(cmd_buf, terrain->n_indices[i], instance_count, 0, 0, instance_count_acc);
        instance_count_acc += instance_count;
    }
    //vkCmdBindIndexBuffer(cmd_buf, terrain->gpu_mem_indices.arena->buffer, terrain->gpu_mem_indices.offset, VK_INDEX_TYPE_UINT16);
    //vkCmdDrawIndexed(cmd_buf, terrain->n_indices, instance_count_g, 0, 0, 0);
}
