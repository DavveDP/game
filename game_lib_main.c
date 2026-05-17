#define _POSIX_C_SOURCE 199309L // required for clock_gettime

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
#include <game.h>
#include <game.c>
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <alloc_gpu.c>
#include <vulkan_game.h>
#include <vulkan_game.c>
