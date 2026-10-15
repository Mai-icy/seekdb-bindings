#include <node_api.h>

#include "seekdb/seekdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SeekdbHandle handle;
} NativeInstance;

typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    char *db_dir;
    char **parameters;
    size_t parameter_count;
    SeekdbHandle handle;
    int return_code;
    char message[2048];
} OpenTask;

static napi_value make_error(napi_env env, const char *message, int return_code)
{
    napi_value text = NULL;
    napi_value error = NULL;
    napi_value code = NULL;
    if (napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &text) != napi_ok ||
        napi_create_error(env, NULL, text, &error) != napi_ok ||
        napi_create_int32(env, return_code, &code) != napi_ok ||
        napi_set_named_property(env, error, "returnCode", code) != napi_ok) {
        return NULL;
    }
    return error;
}

static napi_value throw_error(napi_env env, const char *message, int return_code)
{
    napi_value error = make_error(env, message, return_code);
    if (error) {
        napi_throw(env, error);
    }
    else {
        napi_throw_error(env, NULL, message);
    }
    return NULL;
}

static char *copy_js_string(napi_env env, napi_value value)
{
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, NULL, 0, &length) != napi_ok)
        return NULL;
    char *copy = (char *)malloc(length + 1);
    if (!copy)
        return NULL;
    if (napi_get_value_string_utf8(env, value, copy, length + 1, &length) != napi_ok) {
        free(copy);
        return NULL;
    }
    if (strlen(copy) != length) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void free_open_task(OpenTask *task)
{
    if (!task)
        return;
    free(task->db_dir);
    if (task->parameters) {
        for (size_t i = 0; i < task->parameter_count; ++i)
            free(task->parameters[i]);
    }
    free(task->parameters);
    free(task);
}

static void finalize_instance(napi_env env, void *data, void *hint)
{
    (void)env;
    (void)hint;
    NativeInstance *instance = (NativeInstance *)data;
    if (instance->handle)
        seekdb_close(instance->handle);
    free(instance);
}

static void execute_open(napi_env env, void *data)
{
    (void)env;
    OpenTask *task = (OpenTask *)data;
    task->return_code = seekdb_open(task->db_dir, (const char **)task->parameters, &task->handle);
    if (task->return_code != SEEKDB_SUCCESS) {
        /* seekdb_last_open_error is thread-local; copy it on this worker. */
        snprintf(task->message, sizeof(task->message), "%s", seekdb_last_open_error());
    }
}

static void complete_open(napi_env env, napi_status status, void *data)
{
    OpenTask *task = (OpenTask *)data;
    napi_value value = NULL;
    if (status == napi_ok && task->return_code == SEEKDB_SUCCESS) {
        NativeInstance *instance = (NativeInstance *)calloc(1, sizeof(*instance));
        if (instance) {
            instance->handle = task->handle;
            if (napi_create_external(env, instance, finalize_instance, NULL, &value) == napi_ok) {
                task->handle = NULL;
                napi_resolve_deferred(env, task->deferred, value);
                napi_delete_async_work(env, task->work);
                free_open_task(task);
                return;
            }
            free(instance);
        }
        snprintf(task->message, sizeof(task->message), "Cannot allocate Node.js seekdb handle");
        task->return_code = SEEKDB_INTERNAL_ERROR;
    }
    else if (status != napi_ok) {
        snprintf(task->message, sizeof(task->message), "seekdb open work did not complete");
        task->return_code = SEEKDB_INTERNAL_ERROR;
    }
    if (task->handle)
        seekdb_close(task->handle);
    value =
        make_error(env, task->message[0] ? task->message : "seekdb_open failed", task->return_code);
    if (!value) {
        /* An error setter may throw; use that exception as the rejection reason. */
        bool pending = false;
        if (napi_is_exception_pending(env, &pending) == napi_ok && pending)
            napi_get_and_clear_last_exception(env, &value);
        if (!value)
            napi_get_undefined(env, &value);
    }
    if (value)
        napi_reject_deferred(env, task->deferred, value);
    napi_delete_async_work(env, task->work);
    free_open_task(task);
}

static napi_value native_open(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value argv[2];
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
        napi_throw_type_error(env, NULL, "open requires dbDir and parameter pairs");
        return NULL;
    }

    bool is_array = false;
    uint32_t count = 0;
    if (napi_is_array(env, argv[1], &is_array) != napi_ok || !is_array ||
        napi_get_array_length(env, argv[1], &count) != napi_ok || count % 2 != 0) {
        napi_throw_type_error(env, NULL, "parameters must be an array of key/value pairs");
        return NULL;
    }

    OpenTask *task = (OpenTask *)calloc(1, sizeof(*task));
    if (!task) {
        napi_throw_error(env, NULL, "Cannot allocate seekdb open task");
        return NULL;
    }
    task->db_dir = copy_js_string(env, argv[0]);
    task->parameter_count = count;
    task->parameters = (char **)calloc((size_t)count + 1, sizeof(char *));
    if (!task->db_dir || !task->parameters) {
        free_open_task(task);
        napi_throw_type_error(env, NULL, "dbDir must be a valid UTF-8 string");
        return NULL;
    }
    for (uint32_t i = 0; i < count; ++i) {
        napi_value item = NULL;
        if (napi_get_element(env, argv[1], i, &item) != napi_ok ||
            !(task->parameters[i] = copy_js_string(env, item))) {
            free_open_task(task);
            napi_throw_type_error(env, NULL, "parameters must contain UTF-8 strings without NUL");
            return NULL;
        }
    }

    napi_value promise = NULL;
    napi_value resource_name = NULL;
    if (napi_create_promise(env, &task->deferred, &promise) != napi_ok ||
        napi_create_string_utf8(env, "seekdb.open", NAPI_AUTO_LENGTH, &resource_name) != napi_ok ||
        napi_create_async_work(env, NULL, resource_name, execute_open, complete_open, task,
                               &task->work) != napi_ok ||
        napi_queue_async_work(env, task->work) != napi_ok) {
        if (task->work)
            napi_delete_async_work(env, task->work);
        free_open_task(task);
        napi_throw_error(env, NULL, "Cannot schedule seekdb open task");
        return NULL;
    }
    return promise;
}

static NativeInstance *get_instance(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    NativeInstance *instance = NULL;
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1 ||
        napi_get_value_external(env, argv[0], (void **)&instance) != napi_ok || !instance) {
        napi_throw_type_error(env, NULL, "Expected a seekdb native handle");
        return NULL;
    }
    return instance;
}

static bool set_string(napi_env env, napi_value object, const char *key, const char *value)
{
    napi_value text = NULL;
    return napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &text) == napi_ok &&
           napi_set_named_property(env, object, key, text) == napi_ok;
}

static napi_value native_connection_options(napi_env env, napi_callback_info info)
{
    NativeInstance *instance = get_instance(env, info);
    if (!instance)
        return NULL;
    if (!instance->handle)
        return throw_error(env, "seekdb is closed", SEEKDB_INVALID_ARGUMENT);

    SeekdbConnectionOptions options = {0};
    int rc = seekdb_connection_options(instance->handle, &options);
    if (rc != SEEKDB_SUCCESS)
        return throw_error(env, "seekdb_connection_options failed", rc);

    napi_value result = NULL;
    if (!options.transport || !options.user || napi_create_object(env, &result) != napi_ok ||
        !set_string(env, result, "transport", options.transport) ||
        !set_string(env, result, "user", options.user)) {
        return throw_error(env, "Cannot create connection options", SEEKDB_INTERNAL_ERROR);
    }

    if (strcmp(options.transport, SEEKDB_CONNECTION_TRANSPORT_TCP) == 0) {
        napi_value port = NULL;
        if (!options.host || options.port == 0 || !set_string(env, result, "host", options.host) ||
            napi_create_uint32(env, options.port, &port) != napi_ok ||
            napi_set_named_property(env, result, "port", port) != napi_ok) {
            return throw_error(env, "Invalid TCP connection options", SEEKDB_INTERNAL_ERROR);
        }
    }
    else if (strcmp(options.transport, SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET) == 0) {
        if (!options.unix_socket || !set_string(env, result, "unixSocket", options.unix_socket))
            return throw_error(env, "Invalid Unix socket options", SEEKDB_INTERNAL_ERROR);
    }
    else if (strcmp(options.transport, SEEKDB_CONNECTION_TRANSPORT_NAMED_PIPE) == 0) {
        if (!options.named_pipe || !set_string(env, result, "namedPipe", options.named_pipe))
            return throw_error(env, "Invalid named pipe options", SEEKDB_INTERNAL_ERROR);
    }
    else {
        return throw_error(env, "Unknown seekdb connection transport", SEEKDB_INTERNAL_ERROR);
    }
    return result;
}

static napi_value native_close(napi_env env, napi_callback_info info)
{
    NativeInstance *instance = get_instance(env, info);
    if (!instance)
        return NULL;
    if (instance->handle) {
        int rc = seekdb_close(instance->handle);
        if (rc != SEEKDB_SUCCESS)
            return throw_error(env, "seekdb_close failed", rc);
        instance->handle = NULL;
    }
    napi_value result = NULL;
    napi_get_undefined(env, &result);
    return result;
}

static napi_value init(napi_env env, napi_value exports)
{
    napi_property_descriptor properties[] = {
        {"open", NULL, native_open, NULL, NULL, NULL, napi_default, NULL},
        {"connectionOptions", NULL, native_connection_options, NULL, NULL, NULL, napi_default,
         NULL},
        {"close", NULL, native_close, NULL, NULL, NULL, napi_default, NULL},
    };
    if (napi_define_properties(env, exports, 3, properties) != napi_ok)
        return NULL;
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
