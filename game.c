typedef struct {
    vec3 pos;
    quat rot;
} transform_t;

typedef struct {
    transform_t transform;
    mat4 proj;
} camera_t;

typedef struct {
    vec2 wasd;
} input_t;

typedef struct {
    camera_t main_camera;
    vec3 camera_acceleration;
    vec3 camera_speed;
    u32 current_subdiv;
} game_state_t;

void transform_move_local(transform_t* trans, vec3 offset) {
    trans->pos = vec3_add(trans->pos, quat_rotate_vec3(trans->rot, offset));
}

void transform_rotate_world(transform_t* trans, vec3 axis, float angle) {
    quat delta = quat_rotate_around(axis, angle);
    trans->rot = quat_mul(delta, trans->rot);
}

void transform_rotate_local(transform_t* trans, vec3 axis, float angle) {
    quat delta = quat_rotate_around(axis, angle);
    trans->rot = quat_mul(trans->rot, delta);
}

mat4 transform_to_view_matrix(transform_t* trans) {
    quat inv_rot = quat_conjugate(trans->rot);
    mat4 r = quat_to_mat4(inv_rot); // matrix with inverse rotation
    r.c4 = quat_rotate_vec3(inv_rot, vec3_neg(trans->pos));
    r.m44 = 1.0f;
    return r;
}

camera_t camera_looking_at_from(vec3 target, vec3 from) {
    vec3 dir = vec3_normalized(vec3_sub(target, from));
    float yaw = atan2f(-dir.x, dir.z);
    float pitch = asinf(dir.y);
    camera_t cam = { .transform = {.pos = from, .rot = {1, 0, 0, 0}}};
    transform_rotate_world(&cam.transform, vec3_up, yaw);
    transform_rotate_local(&cam.transform, vec3_right, pitch);
    return cam; //rvo?
}
