#include <std.h>

u32 mesh_generate_indices_stitched(u32 n, i32 stitched_row, i32 stitched_col, u16* indices) {
        // generate indices
    u32 i = 0;
    i32 dx = 1;
    i32 dy = n;
    // tl -> br tris
    for (i32 row = 0; row < n - 1; row++) {
        // only does stitching on even rows
        for (i32 col = ODD(row); col < n - 1; col+=2) {
            u32 x = row * dy + col * dx;

            //// 1
            if (!(ODD(col) && col == stitched_col)) {
                indices[i++] = x;
                indices[i++] = x + (1 + (EVEN(row) && row == stitched_row)) * dx;
                indices[i++] = x + dx + dy;
            }

            //// 2
            if (!(ODD(row) && row == stitched_row)) {
                indices[i++] = x;
                indices[i++] = x + dx + dy;
                indices[i++] = x + (1 + (EVEN(col) && col == stitched_col)) * dy;
            }
        }
    }
    // bl -> tr tris
    for (u32 row = 0; row < n - 1; row++) {
        // only does stitching on odd rows
        for (u32 col = EVEN(row); col < n - 1; col+=2) {
            u32 x = row * dy + col * dx;
            //// 1
            if (!(EVEN(row) && row == stitched_row) && !(EVEN(col) && col == stitched_col)) {
                indices[i++] = x;
                indices[i++] = x + dx;
                indices[i++] = x + dy;
            }

            // 2
            indices[i++] = x + dy;
            indices[i++] = x + dx;
            indices[i++] = x + (1 + (ODD(row) && row == stitched_row)) * dx + (1 + (ODD(col) && col == stitched_col)) * dy;
        }
    }
    return i;
}

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

void mesh_create_chunked_lod_instances(u8 subdiv, u16* indices, u32* nindices) {
    u32 n_first = 0;
    mesh_subdiv_plane(subdiv, NULL, 0, NULL, indices, &n_first);

    u32 n = (1 << subdiv) + 1;
    u32 removed_per_side = (n / 2) * 3; // 3 indices per tri

    if (nindices) { 
        nindices[0] = n_first;
        nindices[1] = nindices[2] = nindices[3] = nindices[4] = n_first - removed_per_side;
        nindices[5] = nindices[6] = nindices[7] = nindices[8] = n_first - 2 * removed_per_side;
    }
    if (indices == NULL) return;

    u32 count = 0;
    u32 acc = 0;
    u16* p = indices + n_first; // first already generated for us

    /* Index buffer layouts (0 normal indices, 1 stitched)
     * 0    1    2    3    4    5    6    7    8
     * 0 0  0 1  1 0  0 0  1 1  0 1  1 0  1 1  1 1
     * 0 0  0 1  1 0  1 1  0 0  1 1  1 1  0 1  1 0
     */

    // rightmost col (n - 1)
    nindices[1] = count = mesh_generate_indices_stitched(n,  -1, n-2, p += count);
    // leftmost col (0)
    nindices[2] = count = mesh_generate_indices_stitched(n,  -1,   0, p += count);
    // bottommost row
    nindices[3] = count = mesh_generate_indices_stitched(n, n-2,  -1, p += count);
    // topmost row
    nindices[4] = count = mesh_generate_indices_stitched(n,   0,  -1, p += count);
    // br
    nindices[5] = count = mesh_generate_indices_stitched(n, n-2, n-2, p += count);
    // bl
    nindices[6] = count = mesh_generate_indices_stitched(n, n-2,   0, p += count);
    // tr
    nindices[7] = count = mesh_generate_indices_stitched(n,   0, n-2, p += count);
    // tl
    nindices[8] = count = mesh_generate_indices_stitched(n,   0,   0, p += count);
}

