void mesh_subdiv_plane(u8 subdiv, void* vertices, u32 stride, u32* nvertices, u16* indices, u32* nindices) {
    assert(subdiv <= 7); // max index 16640, fits u16
    u32 n = (1 << subdiv) + 1;
    if (nvertices) *nvertices = n * n;
    u32 n_triangles = (1 << (2 * subdiv + 1));
    if (nindices) *nindices = n_triangles * 3;
    if (vertices != NULL) {
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
    }
    if (indices != NULL) {
        // generate indices
        u32 i = 0;
        u32 dx = 1;
        u32 dy = n;
        // tl -> br tris
        for (u32 row = 0; row < n - 1; row++) {
            for (u32 col = row & 1; col < n - 1; col+=2) {
                u32 x = row * dy + col * dx;
                indices[i++] = x;
                indices[i++] = x + dx;
                indices[i++] = x + dx + dy;
                indices[i++] = x;
                indices[i++] = x + dx + dy;
                indices[i++] = x + dy;
            }
        }
        // bl -> tr tris
        for (u32 row = 0; row < n - 1; row++) {
            for (u32 col = (row & 1)^1; col < n - 1; col+=2) {
                u32 x = row * dy + col * dx;
                indices[i++] = x;
                indices[i++] = x + dx;
                indices[i++] = x + dy;
                indices[i++] = x + dy;
                indices[i++] = x + dx;
                indices[i++] = x + dx + dy;
            }
        }
    }
}

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

//void mesh_create_index_buffer_permutations(u8 subdiv, u16* indices, u16* counts) {
//    u32 n_triangles = (1 << (2 * subdiv + 1));
//    u32 nindices = n_triangles * 3;
//    u32 tris_to_remove_per_side = (subdiv * 2) * 2;
//    counts[0] = counts[1] = counts[2] = counts[3] = nindices - tris_to_remove_per_side * 3;
//    counts[4] = counts[5] = counts[6] = counts[7] = nindices - tris_to_remove_per_side * 2 * 3;
//    if (indices == NULL) return;
//
//    u32 n = (1 << subdiv) + 1;
//    u32 dx = 1;
//    u32 dy = n;
//
//    u32 tl = 0;
//    u32 tr = (n-1)*dx;
//    u32 bl = (n-1)*dy;
//    
//    // top
//    u16* p = indices;
//    for (int c = 0; c < 8; c++) {
//        mesh_subdiv_plane(subdiv, NULL, 0, NULL, p, nindices);
//        u32 count = 0;
//        u16 indices_to_remove[counts[c]];
//        for (u32 i = 1; i < tr; i++) ir1[count++]; // parametrize this?
//        for (u32 i = 0; i < count; i++) {
//            remove_indices_containing(p, nindices, ir1[i]);
//        }
//        p += counts[0];
//    }
//}
