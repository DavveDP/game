#include <dlfcn.h>
#include <sys/sendfile.h>

void copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    assert(in > 0);
    struct stat st;
    fstat(in, &st);
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    assert(out > 0);
    sendfile(out, in, NULL, st.st_size);
    close(in);
    close(out);
}

// Only compiled when hot reload is enabled
void load_game_lib(const char* path, game_api_t* game_api, render_api_t* render_api) {
    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    //printf("cwd at dlopen: %s\n", cwd);
    static void* handle = NULL;
    if (handle) dlclose(handle);

    // Copy to temp so Linux doesn't lock the original
    copy_file(path, "game_tmp.so");

    handle = dlopen("./game_tmp.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        assert(0);
    }
    assert(game_api->update = dlsym(handle, "update"));
    assert(render_api->init_rendering = dlsym(handle, "init_rendering"));
    assert(render_api->render = dlsym(handle, "render"));
}

void hot_reload_sync(const char* path, game_api_t* game_api, render_api_t* render_api) {
    static struct stat prev = {0};

    int in = open(path, O_RDONLY);
    assert(in > 0);
    struct stat st;
    fstat(in, &st);
    close(in);

    if (st.st_mtime > prev.st_mtime) {
        load_game_lib(path, game_api, render_api);
    }
}
