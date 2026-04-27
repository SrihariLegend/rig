#ifndef RIG_FS_H
#define RIG_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

char *fs_read_file(const char *path, size_t *len);
int fs_write_file(const char *path, const char *data, size_t len);
int fs_append_file(const char *path, const char *data, size_t len);
bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
bool fs_is_file(const char *path);
int fs_mkdir_p(const char *path);
int64_t fs_mtime(const char *path);
char *fs_join(const char *base, const char *path);
const char *fs_homedir(void);
char *fs_expand_home(const char *path);
int fs_readdir(const char *path, void (*cb)(const char *dir, const char *name, bool is_dir, void *ctx), void *ctx);

#endif
