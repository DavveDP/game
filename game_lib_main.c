#define _POSIX_C_SOURCE 199309L // required for clock_gettime

// Essentials from C stdlib
#include <std.h>
#include <game.h>
#include <game.c>
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan_game.h>
#include <vulkan_game.c>
