// s_max -> vertices, indices[s_max + 1]
u32 remove_indices_containing(u16* indices, u32 nindices, u16 index) {
    u32 shift = 0;
    for (u32 i = 0; i < nindices; i++) {
        indices[i-shift] = indices[i];
        if (indices[i] == index) {
            shift++;
        }
    }
    return shift; // removed
}

void subdiv_plane(u8 subdiv, void* vertices, u32 stride, u32* nvertices, u16* indices, u32* nindices) {
    assert(subdiv <= 7); // max index 16640, fits u16
    u32 n = (1 << subdiv) + 1;
    *nvertices = n * n;
    u32 n_triangles = (1 << (2 * subdiv + 1));
    *nindices = n_triangles * 3;
    if (vertices == NULL || indices == NULL) {
        return;
    }
    // generate vertices
    u8* _vertices = vertices;
    u32 i_vert = 0;
    for (float z = 0; z < n; z++) {
        for (float x = 0; x < n; x++) {
            float* vertex_start = (float*)_vertices;
            vertex_start[0] = x / (n - 1);
            vertex_start[1] = 0.0f;
            vertex_start[2] = z / (n - 1);
            _vertices += stride;
        }
    }
    // generate indices
    u32 i = 0;
    u32 dx = 1;
    u32 dy = n;
    for (u32 row = 0; row < n - 1; row++) {
        for (u32 col = 0; col < n - 1; col++) {
            u32 x = row * dy + col * dx;
            indices[i++] = x;
            indices[i++] = x + dx;
            indices[i++] = x + dx + dy;
            indices[i++] = x;
            indices[i++] = x + dx + dy;
            indices[i++] = x + dy;
        }
    }
}
