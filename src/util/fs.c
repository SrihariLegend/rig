#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>

char *fs_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);

    buf[n] = '\0';
    if (len) *len = n;
    return buf;
}

int fs_write_file(const char *path, const char *data, size_t len) {
    size_t path_len = strlen(path);
    char *tmp = malloc(path_len + 8);
    if (!tmp) return -1;
    snprintf(tmp, path_len + 8, "%s.XXXXXX", path);

    int fd = mkstemp(tmp);
    if (fd < 0) { free(tmp); return -1; }

    FILE *f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(tmp); free(tmp); return -1; }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) { unlink(tmp); free(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); free(tmp); return -1; }

    free(tmp);
    return 0;
}

int fs_append_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

bool fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool fs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool fs_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int fs_mkdir_p(const char *path) {
    char *tmp = strdup(path);
    if (!tmp) return -1;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    int rc = mkdir(tmp, 0755);
    free(tmp);
    return (rc == 0 || errno == EEXIST) ? 0 : -1;
}

int64_t fs_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_mtime;
}

char *fs_join(const char *base, const char *path) {
    if (!base || !path) return NULL;
    size_t blen = strlen(base);
    size_t plen = strlen(path);

    while (blen > 0 && base[blen - 1] == '/') blen--;
    while (plen > 0 && path[0] == '/') { path++; plen--; }

    char *result = malloc(blen + 1 + plen + 1);
    if (!result) return NULL;
    memcpy(result, base, blen);
    result[blen] = '/';
    memcpy(result + blen + 1, path, plen);
    result[blen + 1 + plen] = '\0';
    return result;
}

const char *fs_homedir(void) {
    const char *home = getenv("HOME");
    if (home) return home;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

char *fs_expand_home(const char *path) {
    if (!path || path[0] != '~') return strdup(path ? path : "");
    const char *home = fs_homedir();
    size_t hlen = strlen(home);
    size_t plen = strlen(path + 1);
    char *result = malloc(hlen + plen + 1);
    if (!result) return NULL;
    memcpy(result, home, hlen);
    memcpy(result + hlen, path + 1, plen + 1);
    return result;
}

int fs_readdir(const char *path, void (*cb)(const char *dir, const char *name, bool is_dir, void *ctx), void *ctx) {
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) continue;

        bool is_dir_entry = false;
        if (ent->d_type == DT_DIR) {
            is_dir_entry = true;
        } else if (ent->d_type == DT_UNKNOWN) {
            char *full = fs_join(path, ent->d_name);
            is_dir_entry = fs_is_dir(full);
            free(full);
        }
        cb(path, ent->d_name, is_dir_entry, ctx);
    }
    closedir(d);
    return 0;
}
