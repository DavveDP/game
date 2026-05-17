#include <dlfcn.h>
#include <sys/file.h>
#include <sys/sendfile.h>

void copy_file(int fd_in, const char* dst) {
    struct stat st;
    fstat(fd_in, &st);
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    assert(out > 0);
    sendfile(out, fd_in, NULL, st.st_size);
    close(fd_in);
    close(out);
}

// Only compiled when hot reload is enabled
void load_game_lib(int fd, game_api_t* game_api, render_api_t* render_api) {
    char cwd[4096];
    getcwd(cwd, sizeof(cwd));
    //printf("cwd at dlopen: %s\n", cwd);
    static void* handle = NULL;
    if (handle) dlclose(handle);

    // Copy to temp so Linux doesn't lock the original
    copy_file(fd, "game_tmp.so");

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
    printf("checking time\n");

    struct stat st;
    if (stat(path, &st) != 0) return;

    if (st.st_mtime > prev.st_mtime) {
        // Wait and re-stat to confirm the file size has settled
        struct timespec rem = {0, 50000000};
        int res;
        do {
            res = nanosleep(&rem, &rem);
        }
        while (res == -1);

        struct stat st2;
        if (stat(path, &st2) != 0) return;

        if (st2.st_size != st.st_size || st2.st_mtime != st.st_mtime) {
            printf("file still changing, skipping\n");
            return; // Will retry next poll
        }

        printf("reloading\n");
        int in = open(path, O_RDONLY);
        assert(in > 0);
        load_game_lib(in, game_api, render_api);
        prev.st_mtime = st2.st_mtime;
    }
}
