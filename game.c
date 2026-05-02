typedef struct {
    vec3 pos;
    quat rot;
} transform_t;

typedef struct {
    transform_t transform;
    mat4 proj;
} camera_t;

typedef struct {
    camera_t main_camera;
    vec3 camera_speed;
    u32 current_subdiv;
} game_state_t;

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
    r.c4 = quat_rotate_vec3(inv_rot, vec3_neg(trans->pos)); // inverse translation
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
    return cam; //rvo?
}

void game_update(game_state_t* game_state, float delta_time, u32 n_index_buffers /*TODO: remove direct dependency on this*/) {
        // arrows control subdivision for now
        if (btn_pressed(BTN_SUBDIV_INC)) {
            game_state->current_subdiv = (game_state->current_subdiv + 1) % n_index_buffers;
        }
        if (btn_pressed(BTN_SUBDIV_DEC)) {
            game_state->current_subdiv = ((game_state->current_subdiv - 1) + n_index_buffers) % n_index_buffers;
        }
        // wasd to move camera, shift for sprint
        vec3 camera_acceleration = vec3_zero;
        if (btn_held_or_pressed(BTN_MOVE_FORWARD)) {
            camera_acceleration.z =-1.0f;
        } 
        if (btn_held_or_pressed(BTN_MOVE_LEFT)) {
            camera_acceleration.x =-1.0f;
        } 
        if (btn_held_or_pressed(BTN_MOVE_BACKWARD)) {
            camera_acceleration.z = 1.0f;
        } 
        if (btn_held_or_pressed(BTN_MOVE_RIGHT)) {
            camera_acceleration.x = 1.0f;
        }
        if (btn_held_or_pressed(BTN_MOVE_UP)) {
            camera_acceleration.y = 1.0f;
        }
        if (btn_held_or_pressed(BTN_MOVE_DOWN)) {
            camera_acceleration.y =-1.0f;
        }
        if (!vec3_is_zero(camera_acceleration)) {
            camera_acceleration = vec3_normalized(camera_acceleration);
        }

        //printf("delta_time: %f\n", delta_time);
        transform_t* cam_trans = &game_state->main_camera.transform;
        // camera move
        vec3* speed = &game_state->camera_speed;
        const float max_speed = 3.0f;
        vec3 target_velocity = vec3_scale(camera_acceleration, max_speed);
        //vec3_print(target_velocity, printf);

        // exponential smoothing toward target
        float responsiveness = 10.0f;
        *speed = vec3_lerp(*speed, target_velocity, 1.0f - expf(-responsiveness * delta_time));

        vec3 speed_xz = {
            speed->x,
            0.0f,
            speed->z
        };
        vec3 speed_y = {
            .y = speed->y
        };

        transform_move_local(cam_trans, vec3_scale(speed_xz, delta_time));
        transform_move_global(cam_trans, vec3_scale(speed_y, delta_time));
        //vec3_print(cam_trans->pos, printf);

        // camera rotation
        const float sensitivity = 0.001f;

        float dx, dy;
        mouse_moved_this_frame(&dx, &dy);
        if (dx != 0 || dy != 0) {
            float yaw = -dx * sensitivity;
            float pitch = -dy * sensitivity;
            transform_rotate_world(cam_trans, vec3_up, yaw);
            transform_rotate_local(cam_trans, vec3_right, pitch);
        }
}
