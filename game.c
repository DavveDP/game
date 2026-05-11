void main_camera_movement(input_t* input, game_state_t* game_state, float delta_time) {
    // wasd to move camera
    vec3 camera_acceleration = vec3_zero;
    if (was_btn_held_or_pressed(input, BTN_MOVE_FORWARD)) {
        camera_acceleration.z =-1.0f;
    } 
    if (was_btn_held_or_pressed(input, BTN_MOVE_LEFT)) {
        camera_acceleration.x =-1.0f;
    } 
    if (was_btn_held_or_pressed(input, BTN_MOVE_BACKWARD)) {
        camera_acceleration.z = 1.0f;
    } 
    if (was_btn_held_or_pressed(input, BTN_MOVE_RIGHT)) {
        camera_acceleration.x = 1.0f;
    }
    if (was_btn_held_or_pressed(input, BTN_MOVE_UP)) {
        camera_acceleration.y = 1.0f;
    }
    if (was_btn_held_or_pressed(input, BTN_MOVE_DOWN)) {
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
    const float dead_zone = 2e-2f;
    if (vec3_sq_magnitude(*speed) < dead_zone * dead_zone) {
        *speed = vec3_zero;
    }

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

    float dx = input->mouse_dx; float dy = input->mouse_dy;
    if (dx != 0 || dy != 0) {
        float yaw = -dx * sensitivity;
        float pitch = -dy * sensitivity;
        transform_rotate_world(cam_trans, vec3_up, yaw);
        transform_rotate_local(cam_trans, vec3_right, pitch);
    }
}

void update(input_t* input, game_state_t* game_state, float delta_time) {
    // arrows control subdivision for now
    //if (was_btn_pressed(input, BTN_SUBDIV_INC)) {
    //    game_state->current_subdiv = (game_state->current_subdiv + 1) % n_index_buffers;
    //}
    //if (was_btn_pressed(input, BTN_SUBDIV_DEC)) {
    //    game_state->current_subdiv = ((game_state->current_subdiv - 1) + n_index_buffers) % n_index_buffers;
    //}
    main_camera_movement(input, game_state, delta_time);
}
