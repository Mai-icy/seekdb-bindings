#pragma once

#include "seekdb.h"

int sdb_load(const char *path, char *error, size_t error_size);
int sdb_set_binary_path(const char *path);
int sdb_open(const char *db_dir, const char **parameters, SeekdbHandle *handle,
             char *error, size_t error_size);
int sdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *options);
int sdb_close(SeekdbHandle handle);
