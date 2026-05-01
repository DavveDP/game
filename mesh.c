// s_max -> vertices, indices[s_max + 1]

void subdiv_plane(u8 s_max, void* vertices, u32 stride, u32* nvertices, u16** indices, u32* nindices) {
    assert(s_max <= 7); // max index 16640, fits u16
    assert(s_max < 32);
    u32 n = (1 << s_max) + 1;
    *nvertices = n * n;
    for (u32 s = 0; s <= s_max; s++) {
        u32 n_tri = 1 << (2 * s + 1);
        nindices[s] = n_tri * 3;
    }
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
    for (u32 s = 0; s <= s_max; s++) {
        u32 i = 0;
        u32 dx = (1 << (s_max - s));
        u32 dy = n * dx;
        // iterate over coarse rows and columns only
        u32 n_coarse = (1 << s); // number of quads per row at this subdivision
        for (u32 row = 0; row < n_coarse; row++) {
            for (u32 col = 0; col < n_coarse; col++) {
                u32 x = row * dy + col * dx; // top-left vertex of this quad
                indices[s][i++] = x;
                indices[s][i++] = x + dx;
                indices[s][i++] = x + dx + dy;
                indices[s][i++] = x;
                indices[s][i++] = x + dx + dy;
                indices[s][i++] = x + dy;
            }
        }
    }
}
