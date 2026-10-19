#include "native_loader.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static HMODULE sdb_library;
#else
#include <dlfcn.h>
static void *sdb_library;
#endif

static int (*native_open)(const char *, const char **, SeekdbHandle *);
static const char *(*native_last_open_error)(void);
static int (*native_set_binary_path)(const char *);
static int (*native_connection_options)(SeekdbHandle, SeekdbConnectionOptions *);
static int (*native_close)(SeekdbHandle);

static void unload_library(void)
{
#ifdef _WIN32
    FreeLibrary(sdb_library);
#else
    dlclose(sdb_library);
#endif
    sdb_library = NULL;
    native_open = NULL;
    native_last_open_error = NULL;
    native_set_binary_path = NULL;
    native_connection_options = NULL;
    native_close = NULL;
}

static void copy_error(char *destination, size_t size, const char *message)
{
    if (size != 0)
        snprintf(destination, size, "%s", message ? message : "unknown native error");
}

#ifdef _WIN32
static void *symbol(const char *name)
{
    return (void *)GetProcAddress(sdb_library, name);
}
#else
static void *symbol(const char *name)
{
    return dlsym(sdb_library, name);
}
#endif

int sdb_load(const char *path, char *error, size_t error_size)
{
    if (sdb_library)
        return SEEKDB_SUCCESS;
#ifdef _WIN32
    sdb_library = LoadLibraryA(path);
    if (!sdb_library) {
        char message[128];
        snprintf(message, sizeof(message), "LoadLibrary failed (Windows error %lu)",
                 (unsigned long)GetLastError());
        copy_error(error, error_size, message);
        return SEEKDB_INTERNAL_ERROR;
    }
#else
    sdb_library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!sdb_library) {
        copy_error(error, error_size, dlerror());
        return SEEKDB_INTERNAL_ERROR;
    }
#endif

#define LOAD(name)                                                                                \
    do {                                                                                          \
        native_##name = symbol("seekdb_" #name);                                                  \
        if (!native_##name) {                                                                      \
            copy_error(error, error_size, "missing seekdb_" #name " in libseekdb");             \
            unload_library();                                                                      \
            return SEEKDB_INTERNAL_ERROR;                                                         \
        }                                                                                         \
    } while (0)
    LOAD(open);
    LOAD(last_open_error);
    LOAD(set_binary_path);
    LOAD(connection_options);
    LOAD(close);
#undef LOAD
    return SEEKDB_SUCCESS;
}

int sdb_set_binary_path(const char *path)
{
    return native_set_binary_path ? native_set_binary_path(path) : SEEKDB_INTERNAL_ERROR;
}

int sdb_open(const char *db_dir, const char **parameters, SeekdbHandle *handle,
             char *error, size_t error_size)
{
    if (!native_open)
        return SEEKDB_INTERNAL_ERROR;
    int rc = native_open(db_dir, parameters, handle);
    if (rc != SEEKDB_SUCCESS)
        copy_error(error, error_size, native_last_open_error());
    return rc;
}

int sdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *options)
{
    return native_connection_options ? native_connection_options(handle, options)
                                     : SEEKDB_INTERNAL_ERROR;
}

int sdb_close(SeekdbHandle handle)
{
    return native_close ? native_close(handle) : SEEKDB_INTERNAL_ERROR;
}
