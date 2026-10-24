#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

typedef unsigned int u32;
typedef unsigned char u8;

#define MAXENT 8192

struct ent {
    char path[512];
    u8 *data;
    u32 len;
    int islink;
    char target[512];
};

static struct ent g_ents[MAXENT];
static int g_nents;
static u32 g_fnv = 2166136261u;

static void fnv_push(const u8 *d, u32 n)
{
    for (u32 i = 0; i < n; i++) {
        g_fnv ^= d[i];
        g_fnv *= 16777619u;
    }
}

static void fnv_push32(u32 v)
{
    u8 b[4] = { (u8)(v & 255), (u8)((v >> 8) & 255), (u8)((v >> 16) & 255), (u8)((v >> 24) & 255) };
    fnv_push(b, 4);
}

static void put32(FILE *f, u32 v)
{
    u8 b[4] = { (u8)(v & 255), (u8)((v >> 8) & 255), (u8)((v >> 16) & 255), (u8)((v >> 24) & 255) };
    fwrite(b, 1, 4, f);
}

static int wanted(const char *name)
{
    return strstr(name, ".so") != NULL || strncmp(name, "ld-musl", 7) == 0;
}

static int add_file(const char *src, const char *dst)
{
    FILE *f = fopen(src, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 256L * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    u8 *data = malloc((size_t)n);
    if (!data) {
        fclose(f);
        return 0;
    }
    if (n > 0 && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    struct ent *e = &g_ents[g_nents++];
    snprintf(e->path, sizeof e->path, "%s", dst);
    e->data = data;
    e->len = (u32)n;
    e->islink = 0;
    e->target[0] = 0;
    return 1;
}

static int add_link(const char *src, const char *dst)
{
    char target[512];
    ssize_t n = readlink(src, target, sizeof target - 1);
    if (n < 0) return 0;
    target[n] = 0;
    struct ent *e = &g_ents[g_nents++];
    snprintf(e->path, sizeof e->path, "%s", dst);
    e->data = 0;
    e->islink = 1;
    snprintf(e->target, sizeof e->target, "%s", target);
    e->len = (u32)n + 1;
    return 1;
}

static int full(void)
{
    if (g_nents >= MAXENT) {
        fprintf(stderr, "mkbundle: too many files (max %d)\n", MAXENT);
        return 1;
    }
    return 0;
}

static void walk_libs(const char *src, const char *dst)
{
    DIR *d = opendir(src);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && !full()) {
        if (de->d_name[0] == '.') continue;
        char spath[768], dpath[768];
        snprintf(spath, sizeof spath, "%s/%s", src, de->d_name);
        snprintf(dpath, sizeof dpath, "%s/%s", dst, de->d_name);
        struct stat st;
        if (lstat(spath, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_libs(spath, dpath);
        } else if (wanted(de->d_name)) {
            if (S_ISLNK(st.st_mode)) add_link(spath, dpath);
            else if (S_ISREG(st.st_mode)) add_file(spath, dpath);
        }
    }
    closedir(d);
}

static void walk_all(const char *src, const char *dst)
{
    DIR *d = opendir(src);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && !full()) {
        if (de->d_name[0] == '.') continue;
        char spath[768], dpath[768];
        snprintf(spath, sizeof spath, "%s/%s", src, de->d_name);
        snprintf(dpath, sizeof dpath, "%s/%s", dst, de->d_name);
        struct stat st;
        if (lstat(spath, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_all(spath, dpath);
        } else if (S_ISLNK(st.st_mode)) {
            add_link(spath, dpath);
        } else if (S_ISREG(st.st_mode)) {
            add_file(spath, dpath);
        }
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: mkbundle <out> <preload.so> <payload_dir> <rootfs>\n");
        return 2;
    }
    const char *out = argv[1];
    const char *preload = argv[2];
    const char *payload = argv[3];
    const char *rootfs = argv[4];

    char libdir[768], usrdir[768];
    snprintf(libdir, sizeof libdir, "%s/lib", rootfs);
    snprintf(usrdir, sizeof usrdir, "%s/usr/lib", rootfs);

    if (!add_file(preload, "lib/preload64.so")) {
        fprintf(stderr, "mkbundle: cannot read %s\n", preload);
        return 1;
    }
    walk_libs(libdir, "lib");
    walk_libs(usrdir, "usr/lib");
    walk_all(payload, "lib");

    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "mkbundle: cannot create %s\n", out);
        return 1;
    }
    put32(f, 0x38565341u);
    put32(f, 1);
    put32(f, 0);
    put32(f, (u32)g_nents);

    u32 total = 0;
    for (int i = 0; i < g_nents; i++) {
        struct ent *e = &g_ents[i];
        u32 flags = (u32)e->islink;
        u32 plen = (u32)strlen(e->path);
        const u8 *data;
        u32 dlen;
        if (e->islink) {
            data = (const u8 *)e->target;
            dlen = (u32)strlen(e->target) + 1;
        } else {
            data = e->data;
            dlen = e->len;
        }
        fnv_push32(flags);
        fnv_push32(plen);
        fnv_push32(dlen);
        fnv_push((const u8 *)e->path, plen);
        fnv_push(data, dlen);
        put32(f, flags);
        put32(f, plen);
        put32(f, dlen);
        fwrite(e->path, 1, plen, f);
        fwrite(data, 1, dlen, f);
        total += dlen;
    }
    fseek(f, 8, SEEK_SET);
    put32(f, g_fnv);
    fclose(f);
    printf("mkbundle: %d files, %.1f MiB, id %08x\n", g_nents, total / 1048576.0, g_fnv);
    return 0;
}
