typedef struct {
    vec3 pos;
    quat rot;
} transform_t;

typedef struct {
    transform_t transform;
    mat4 proj;
} camera_t;

typedef struct {
    float xmin, xmax, ymin, ymax, zmin, zmax;
} aabb_t;

typedef struct {
    union {
        struct {
            vec4 plane_left, plane_right, plane_bottom, plane_top, plane_near, plane_far;
        };
        vec4 planes[6];
    };
} frustum_t;

void transform_move_local(transform_t* trans, vec3 offset) {
    trans->pos = vec3_add(trans->pos, quat_rotate_vec3(trans->rot, offset));
}

void transform_move_global(transform_t* trans, vec3 offset) {
    trans->pos = vec3_add(trans->pos, offset);
}

void transform_rotate_world(transform_t* trans, vec3 axis, float angle) {
    quat delta = quat_rotate_around(axis, angle);
    trans->rot = quat_mul(delta, trans->rot);  // delta on the left for world space
}

void transform_rotate_local(transform_t* trans, vec3 axis, float angle) {
    quat delta = quat_rotate_around(axis, angle);
    trans->rot = quat_mul(trans->rot, delta);
}

mat4 transform_to_view_matrix(transform_t* trans) {
    quat inv_rot = quat_conjugate(trans->rot);
    mat4 r = quat_to_mat4(inv_rot); // matrix with inverse rotation
    *(vec3*)(&r.c4) = quat_rotate_vec3(inv_rot, vec3_neg(trans->pos)); // inverse translation
    r.m44 = 1.0f;
    return r;
}

camera_t camera_looking_at_from(vec3 target, vec3 from) {
    vec3 dir = vec3_normalized(vec3_sub(target, from));
    float yaw = atan2f(dir.x,dir.z) + PI; // tan is from z forward and our forward is -z, so add PI
    float pitch = asinf(dir.y);
    camera_t cam = { .transform = {.pos = from, .rot = {1, 0, 0, 0}}};
    transform_rotate_world(&cam.transform, vec3_up, yaw);
    transform_rotate_local(&cam.transform, vec3_right, pitch);
    return cam;
}

// frustum stuff

// inward facing normals
frustum_t camera_get_frustum(camera_t* cam) {
    mat4 view = transform_to_view_matrix(&cam->transform);
    mat4 M = mat4_mul(&cam->proj, &view);
    mat4 M_T = mat4_transposed(&M);
    frustum_t f = {0};
    f.plane_left   = vec4_add(M_T.c4, M_T.c1);
    f.plane_right  = vec4_sub(M_T.c4, M_T.c1);
    f.plane_bottom = vec4_add(M_T.c4, M_T.c2);
    f.plane_top    = vec4_sub(M_T.c4, M_T.c2);
    f.plane_near   = vec4_add(M_T.c4, M_T.c3);
    f.plane_far    = vec4_sub(M_T.c4, M_T.c3);
    return f;
}

bool frustum_intersects_aabb(frustum_t* f, aabb_t* box) {
    for (u32 i = 0; i < LEN(f->planes); i++) {
        vec4 positive;
        positive.x = (f->planes[i].x >= 0) ? box->xmax : box->xmin;
        positive.y = (f->planes[i].y >= 0) ? box->ymax : box->ymin;
        positive.z = (f->planes[i].z >= 0) ? box->zmax : box->zmin;
        positive.w = 1.0f;
        // behind plane -> outside
        if (vec4_dot(f->planes[i], positive) < 0) { 
            return false;
        }
    }
    return true;
}
