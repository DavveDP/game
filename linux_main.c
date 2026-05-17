//#define _POSIX_C_SOURCE 199309L // required for clock_gettime
#define _POSIX_C_SOURCE 200112L // required by readlink
                                

// Essentials from C stdlib
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <defines.h>

#include <alloc.c>
#include <math.c> // links with math
#include <algorithms.c>
#include <transforms.c>
// My platform agnostic files

#include <game.h>
// function pointer in both cases
#ifndef ENABLE_HOT_RELOAD
    #include <game.c>
#endif

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <alloc_gpu.c>
#include <vulkan_game.h>

#ifndef ENABLE_HOT_RELOAD
    #include <vulkan_game.c>
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#ifdef ENABLE_HOT_RELOAD
    #define GAME_LIB_FILENAME "game.so"
    #include <linux_hot_reload.c>
#endif

// Begin Platfrom specific code

// Linux
#include <sys/mman.h>
#include <libgen.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>

void set_proc_dir_to_exe_dir() {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    exe_path[len] = '\0';
    //printf("exe_path: %s\n", exe_path);
    char *exe_dir = dirname(exe_path);
    //printf("exe_dir: %s\n", exe_dir);
    int ret = chdir(exe_dir);
    //printf("chdir ret: %d\n", ret);  // 0 = success, -1 = fail

    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    //printf("cwd is now: %s\n", cwd);
}

// Input

input_t input;

// Mouse input

Cursor invisible_cursor;

// raw input data
typedef struct {
    int majorOpcode, eventBase, errorBase;
} XI_t;

XI_t xi;

typedef struct {
    float prev_x, prev_y, x, y;
} cursor_position_t;

cursor_position_t cursor_position;

static void end_of_frame_mouse_update(void) {
    cursor_position.prev_x = cursor_position.x;
    cursor_position.prev_y = cursor_position.y;
}

// Keyboard input

enum X11_KEY_CODES_t {
    X11_KEY_CODE_W = 25,
    X11_KEY_CODE_A = 38,
    X11_KEY_CODE_S = 39,
    X11_KEY_CODE_D = 40,
    X11_KEY_CODE_F = 41,
    X11_KEY_CODE_UP = 111,
    X11_KEY_CODE_LEFT = 113,
    X11_KEY_CODE_RIGHT = 114,
    X11_KEY_CODE_DOWN = 116,
    X11_KEY_CODE_LSHIFT = 50,
    X11_KEY_CODE_SPACE = 65,
    //...
};

// TODO: several keys to same button maybe?
u8 keycode_to_btn_table[256];
static void init_btn_key_code_table(void) {
    keycode_to_btn_table[X11_KEY_CODE_W] = BTN_MOVE_FORWARD;
    keycode_to_btn_table[X11_KEY_CODE_A] = BTN_MOVE_LEFT;
    keycode_to_btn_table[X11_KEY_CODE_S] = BTN_MOVE_BACKWARD;
    keycode_to_btn_table[X11_KEY_CODE_D] = BTN_MOVE_RIGHT;
    keycode_to_btn_table[X11_KEY_CODE_F] = BTN_FREEZE_TERRAIN;
    keycode_to_btn_table[X11_KEY_CODE_LSHIFT] = BTN_MOVE_DOWN;
    keycode_to_btn_table[X11_KEY_CODE_SPACE] = BTN_MOVE_UP;
}

u64 get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// TODO: figure out how to pass generic allocators, 
// tagged union for allocator types?
file_data_t read_file(arena_t* allocator, const char* path) {
    int fd = open(path, O_RDONLY);
    assert(fd > 0);
    struct stat st;
    assert(fstat(fd, &st) == 0);
    u8* data = alloc_array(allocator, u8, st.st_size);
    assert(data != NULL); // TODO: allocator should prob not crash, esp for files
    read(fd, data, st.st_size);
    close(fd);
    return (file_data_t) { .data = data, .size = st.st_size };
}

Atom WM_DELETE_WINDOW;
Atom NET_WM_PING;
Atom WM_PROTOCOLS;

volatile sig_atomic_t running = 1;

void sigint_and_sigterm_handler(int sig) {
    running = 0;
}

Display* display;
Window window, root;

// Only Vulkan Code in this file, required to create surface
VkResult create_x11_surface(VkInstance instance, VkSurfaceKHR* surface_out) {
    VkXlibSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = window
    };
    return vkCreateXlibSurfaceKHR(instance, &info, NULL, surface_out);
}

int main(int argc, char** argv) {
    set_proc_dir_to_exe_dir();
    //world_t world;
    //world.main_camera = camera_looking_at_from((vec3){0,0,0}, (vec3){0,2,0,});
    //quat_print(world.main_camera.transform.rot, printf);
    //mat4 m = transform_to_view_matrix(&world.main_camera.transform);
    //mat4_print(&m, printf);
    //return 0;
    // Signal handling
    signal(SIGINT, sigint_and_sigterm_handler);
    signal(SIGTERM, sigint_and_sigterm_handler);
    game_api_t game_api;
    render_api_t render_api;
#ifdef ENABLE_HOT_RELOAD
    hot_reload_sync(GAME_LIB_FILENAME, &game_api, &render_api);
#else
    game_api.update = update;
    render_api.render = render;
    render_api.init_rendering = init_rendering;
#endif
    init_btn_key_code_table();

    // Memory

    // Our only alloc call, (see allocators)
    void* mem = mmap(NULL, GB(1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mem != MAP_FAILED);
    // create app arena
    arena_t arena_permanent = arena_create(mem, GB(1));

    // create arena_scratch (nested arena)
    arena_t arena_scratch = arena_create(alloc_array(&arena_permanent, u8, MB(16)), MB(16));

    // X11 init

    display = XOpenDisplay(NULL);
    root = RootWindow(display, DefaultScreen(display));

    // assert xkb support, disabling auto release events
    int supported;
    XkbSetDetectableAutoRepeat(display, true, &supported);
    assert(supported);

    window = XCreateSimpleWindow(
            display, 
            root,
            0, 0, 
            800, 600, 
            0, 
            WhitePixel(display, 0),
            BlackPixel(display, 0)
            );

    // create invisible cursor
    Pixmap bm_no;
    XColor black;
    const char no_data[] = {0,0,0,0,0,0,0,0};
    bm_no = XCreateBitmapFromData(display, window, no_data, 8, 8);
    invisible_cursor = XCreatePixmapCursor(display, bm_no, bm_no, &black, &black, 0, 0);

    // enable raw mouse input events
    assert(XQueryExtension(display, "XInputExtension", 
                &xi.majorOpcode,
                &xi.eventBase,
                &xi.errorBase));
    int major = 2, minor = 0;
    assert(XIQueryVersion(display, &major, &minor) == Success);

    unsigned char mask[XIMaskLen(XI_RawMotion)] = {0};
    XISetMask(mask, XI_RawMotion);

    XIEventMask emask = {
        .deviceid = XIAllMasterDevices,
        .mask_len = sizeof(mask),
        .mask = mask
    };

    XISelectEvents(display, root, &emask, 1);

    // Init window

    // show the window, should activate the WM
    XMapWindow(display, window);
    XSelectInput(display, window, ExposureMask | StructureNotifyMask | 
            KeyPressMask | KeyReleaseMask |
            PointerMotionMask | FocusChangeMask);

    // set up WM message atoms
    WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);
    NET_WM_PING = XInternAtom(display, "_NET_WM_PING", False);
    WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom protocols[] = 
    {
        WM_DELETE_WINDOW,
        NET_WM_PING
    };
    XSetWMProtocols(display, window, protocols, LEN(protocols));

    XFlush(display);

    // Wait for the window to actually appear before querying surface caps
    {
        XEvent e;
        do { XNextEvent(display, &e); } while (e.type != ConfigureNotify);
    }

    arena_align(&arena_scratch, alignof(u32));
    file_data_t vertex_code   = read_file(&arena_scratch, "../shaders/bin/vertex.spv");
    arena_align(&arena_scratch, alignof(u32));
    file_data_t fragment_code = read_file(&arena_scratch, "../shaders/bin/fragment.spv");

    // Rendering
    vulkan_state_t* renderer = render_api.init_rendering(&arena_permanent, &arena_scratch, create_x11_surface, vertex_code, fragment_code);

    // Game Loop

    XEvent e;
    u64 start_time = get_time_ns();
    u64 curr_time = start_time;

    // game init?
    game_state_t game_state;
    game_state.main_camera = camera_looking_at_from((vec3){1.0f,0,1.0f}, (vec3){0,100,0});
    game_state.camera_speed = vec3_zero;
    game_state.current_subdiv = 1;
    float delta_time = 0;
    bool window_focused = false;

update_start:
    while(running) {
        u64 new_time = get_time_ns();
        delta_time = ((float)(new_time - curr_time)/ 1000000000ull);
        curr_time = new_time; 
        float time = ((float)(curr_time - start_time)/ 1000000000ull);
#ifdef ENABLE_HOT_RELOAD
        RUN_EVERY(hot_reload_sync(GAME_LIB_FILENAME, &game_api, &render_api), 1.0f, delta_time);
#endif

        // pump x events
        while (XPending(display)) {
            XNextEvent(display, &e);
            switch (e.type) {
                case ConfigureNotify:
                    {
                        if(renderer->ctx.render_state.swapchain_cooldown == 0 && (
                                    e.xconfigure.width != renderer->ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.width || 
                                    e.xconfigure.height != renderer->ctx.physicalDevice.selected->surfaceCapabilities.currentExtent.height)) 
                        {
                            renderer->ctx.render_state.recreate_swapchain = true;
                        }
                    }
                    break;

                case ClientMessage:
                    {
                        if (e.xclient.message_type == WM_PROTOCOLS) {
                            Atom msg = e.xclient.data.l[0];
                            if (msg == WM_DELETE_WINDOW) {
                                running = 0;
                                goto update_start;
                            }
                            // reply to WM ping if needed
                            else if (msg == NET_WM_PING) {
                                XEvent reply = e;
                                Window root = RootWindow(display, DefaultScreen(display)); // TODO: cache this
                                reply.xclient.window = root;
                                XSendEvent(display, root, false, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
                            }
                        }
                    }
                    break;
                    // handle keyboard input
                case KeyPress:
                    {
                        u8 code = (u8)e.xkey.keycode;
                        BUTTON b = keycode_to_btn_table[code];
                        if (b) {
                            register_btn_press(&input, b);
                        }
                    }
                    break;
                case KeyRelease:
                    {
                        u8 code = (u8)e.xkey.keycode;
                        BUTTON b = keycode_to_btn_table[code];
                        if (b) {
                            register_btn_release(&input, b);
                        }
                    }
                    break;
                    // capture pointer on focus
                case FocusIn:
                    {
                        // ignore events from popup windows, GLFW does this
                        if (e.xfocus.mode == NotifyGrab || 
                                e.xfocus.mode == NotifyUngrab) {
                            break;
                        }
                        window_focused = true;
                        XDefineCursor(display, window, invisible_cursor);
                    }
                    break;

                case FocusOut:
                    {
                        // ignore events from popup windows, GLFW does this
                        if (e.xfocus.mode == NotifyGrab || 
                                e.xfocus.mode == NotifyUngrab) {
                            break;
                        }
                        window_focused = false;
                        XUndefineCursor(display, window);
                    }
                    break;
                    // handle mouse motion
                    //case MotionNotify:
                    //    {
                    //        cursor_position.x = e.xmotion.x;
                    //        cursor_position.y = e.xmotion.y;
                    //        break;
                    //    }
                case GenericEvent:
                    {
                        if (window_focused &&
                                e.xcookie.extension == xi.majorOpcode &&
                                XGetEventData(display, &e.xcookie) &&
                                e.xcookie.evtype == XI_RawMotion) 
                        {
                            XIRawEvent* re = e.xcookie.data;
                            if (re->valuators.mask_len) {
                                const double* values = re->raw_values;
                                if (XIMaskIsSet(re->valuators.mask, 0)) {
                                    cursor_position.x += (float)*values;
                                    values++;
                                }
                                if (XIMaskIsSet(re->valuators.mask, 1)) {
                                    cursor_position.y += (float)*values;
                                }
                            }
                            XFreeEventData(display, &e.xcookie);
                        }
                    }
                    break;

            }
            // TODO: handle mouse input events
        }

        input.mouse_dx = cursor_position.x - cursor_position.prev_x;
        input.mouse_dy = cursor_position.y - cursor_position.prev_y;
        //printf("fps: %f\n", 1/delta_time);
        //vec3_print(game_state.main_camera.transform.pos, printf);
        game_api.update(&input, &game_state, delta_time);
        // warp cursor back to centre
        if (window_focused) {
            XWarpPointer(display, None, window, 0, 0, 0, 0, window_width(&renderer->ctx) / 2, window_height(&renderer->ctx) / 2);
        }
        // update prevs and whatnot
        end_of_frame_btn_update(&input);
        end_of_frame_mouse_update();

        render_api.render(renderer, &game_state, time);
    }

    printf("Exiting gracefully\n");

    return 0;
}
