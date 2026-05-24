#pragma once

// Input

typedef enum {
    BTN_NONE = 0,
    BTN_MOVE_FORWARD,
    BTN_MOVE_BACKWARD,
    BTN_MOVE_LEFT,
    BTN_MOVE_RIGHT,
    BTN_MOVE_DOWN,
    BTN_MOVE_UP,
    BTN_FREEZE_TERRAIN,
    BTN_TOGGLE_WIREFRAME,
    BTN_COUNT
} BUTTON;

typedef enum {
    BTN_STATE_RELEASED_THIS_UPDATE = 1,
    BTN_STATE_PRESSED_THIS_UPDATE = 2,
    BTN_STATE_HELD = 3
} BTN_STATE;

typedef struct {
    u8 btn_states[BTN_COUNT];
    float mouse_dx, mouse_dy;
} input_t;
// called by game
static inline bool was_btn_released(input_t* input, BUTTON btn) {
    return input->btn_states[btn] == BTN_STATE_RELEASED_THIS_UPDATE;
}

static inline bool was_btn_pressed(input_t* input, BUTTON btn) {
    return input->btn_states[btn] == BTN_STATE_PRESSED_THIS_UPDATE;
}

static inline bool was_btn_held(input_t* input, BUTTON btn) {
    return input->btn_states[btn] == BTN_STATE_HELD;
}

static inline bool was_btn_held_or_pressed(input_t* input, BUTTON btn) {
    return input->btn_states[btn] & 2; // check second bit
}

// called by platform
static inline void register_btn_press(input_t* input, BUTTON btn) {
    u8* s = &input->btn_states[btn];
    *s = (*s >> 1) | 2;
}

static inline void register_btn_release(input_t* input, BUTTON btn) {
    u8* s = &input->btn_states[btn];
    *s = *s != 0;
}

static inline void end_of_frame_btn_update(input_t* input) {
    for (int i = 0; i < BTN_COUNT; i++) {
        u8* s = &input->btn_states[i];
        *s = (*s >> 1) | (*s & 2);
    }
}


// API types

typedef struct {
    camera_t main_camera;
    vec3 camera_speed;
    u32 current_subdiv;
    bool freeze_terrain;
    bool show_wireframe;
} game_state_t;

typedef struct {
    void (*update)(input_t* input, game_state_t* state, float delta_time);
} game_api_t;
