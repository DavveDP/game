// Input stuff
#pragma once

typedef enum {
    BTN_MOVE_FORWARD,
    BTN_MOVE_BACKWARD,
    BTN_MOVE_LEFT,
    BTN_MOVE_RIGHT,
    BTN_MOVE_DOWN,
    BTN_MOVE_UP,
    BTN_SUBDIV_INC,
    BTN_SUBDIV_DEC
} Button;

bool btn_released(Button btn);
bool btn_pressed(Button btn);
bool btn_held(Button btn);
bool btn_held_or_pressed(Button btn);
void mouse_moved_this_frame(float* dx, float* dy);
