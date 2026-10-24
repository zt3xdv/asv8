typedef unsigned char u8;
typedef unsigned int u32;

extern const u8 _binary_bundle_bin_start[];
extern const u8 _binary_bundle_bin_end[];

#define bundle_start _binary_bundle_bin_start

#define SYS_EXIT 1
#define SYS_WRITE 4
#define SYS_CLOSE 6
#define SYS_EXECVE 11
#define SYS_OPENAT 322
#define SYS_FCHMODAT 333
#define SYS_FACCESSAT 334
#define SYS_MKDIRAT 323
#define SYS_SYMLINKAT 331

#define AT_FDCWD -100L

static int g_argc;
static char **g_argv;
static char **g_envp;
static char *g_xargv[4200];

__attribute__((naked)) void _start(void)
{
    __asm__ volatile(
        "mov r0, sp\n"
        "b c_entry\n"
    );
}

static long sys1(long n, long a)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys2(long n, long a, long b)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long sys3(long n, long a, long b, long c)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long sys4(long n, long a, long b, long c, long d)
{
    register long r7 __asm__("r7") = n;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory");
    return r0;
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static char *scpy(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

static char *scat(char *d, const char *s)
{
    return scpy(d + slen(d), s);
}

static int streqn(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void msg(const char *s)
{
    sys3(SYS_WRITE, 2, (long)s, slen(s));
}

static void die(const char *s, const char *name, int code)
{
    msg("asv8: ");
    msg(s);
    if (name) msg(name);
    msg("\n");
    sys1(SYS_EXIT, code);
    __builtin_unreachable();
}

static char *envget(const char *name)
{
    int n = slen(name);
    for (int i = 0; g_envp[i]; i++) {
        char *e = g_envp[i];
        if (streqn(e, name, n) && e[n] == '=') return e + n + 1;
    }
    return 0;
}

static void mkdirs(char *path)
{
    for (int i = 1; path[i]; i++) {
        if (path[i] == '/') {
            path[i] = 0;
            sys3(SYS_MKDIRAT, AT_FDCWD, (long)path, 0777);
            path[i] = '/';
        }
    }
}

static int writeall(long fd, const u8 *d, u32 n)
{
    while (n) {
        long r = sys3(SYS_WRITE, fd, (long)d, n);
        if (r <= 0) return 0;
        d += r;
        n -= (u32)r;
    }
    return 1;
}

static int extract(const char *cache)
{
    static char path[4096];
    scpy(path, cache);
    scat(path, "/.ok");
    if (sys3(SYS_FACCESSAT, AT_FDCWD, (long)path, 0) == 0) return 1;

    u32 count = le32(bundle_start + 12);
    const u8 *p = bundle_start + 16;
    for (u32 i = 0; i < count; i++) {
        u32 flags = le32(p); p += 4;
        u32 plen = le32(p); p += 4;
        u32 dlen = le32(p); p += 4;
        const char *rel = (const char *)p; p += plen;
        const u8 *data = p; p += dlen;

        scpy(path, cache);
        char *d = path + slen(path);
        *d++ = '/';
        for (u32 j = 0; j < plen; j++) d[j] = rel[j];
        d[plen] = 0;
        mkdirs(path);

        if (flags == 1) {
            long r = sys3(SYS_SYMLINKAT, (long)data, AT_FDCWD, (long)path);
            if (r < 0 && r != -17) return 0;
        } else {
            long fd = sys4(SYS_OPENAT, AT_FDCWD, (long)path, 1 | 64 | 512, 0777);
            if (fd < 0) return 0;
            int ok = writeall(fd, data, dlen);
            sys1(SYS_CLOSE, fd);
            if (!ok) return 0;
            sys3(SYS_FCHMODAT, AT_FDCWD, (long)path, 0755);
        }
    }

    scpy(path, cache);
    scat(path, "/.ok");
    long fd = sys4(SYS_OPENAT, AT_FDCWD, (long)path, 1 | 64 | 512, 0644);
    if (fd < 0) return 0;
    sys1(SYS_CLOSE, fd);
    return 1;
}

static int resolve(char *out, const char *name)
{
    int i;
    for (i = 0; name[i]; i++)
        if (name[i] == '/') break;
    if (name[i] == '/') {
        scpy(out, name);
        return sys3(SYS_FACCESSAT, AT_FDCWD, (long)out, 1) == 0;
    }

    static char pbuf[8192];
    char *path = envget("PATH");
    if (!path) return 0;
    scpy(pbuf, path);

    int pos = 0;
    while (pbuf[pos]) {
        int end = pos;
        while (pbuf[end] && pbuf[end] != ':') end++;
        int dirlen = end - pos;
        if (dirlen) {
            for (i = 0; i < dirlen; i++) out[i] = pbuf[pos + i];
            out[dirlen] = '/';
            scpy(out + dirlen + 1, name);
            if (sys3(SYS_FACCESSAT, AT_FDCWD, (long)out, 1) == 0) return 1;
        }
        pos = end;
        if (pbuf[pos]) pos++;
    }
    return 0;
}

static void hex32(char *d, u32 v)
{
    for (int i = 0; i < 8; i++) {
        int nib = (v >> (28 - 4 * i)) & 15;
        d[i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    d[8] = 0;
}

void c_entry(unsigned long *sp)
{
    g_argc = (int)sp[0];
    g_argv = (char **)(sp + 1);
    g_envp = g_argv + g_argc + 1;

    if (g_argc < 2)
        die("usage: asv8 <binary> [args...]", 0, 2);
    if (g_argc > 4000)
        die("too many arguments", 0, 2);

    if (le32(bundle_start) != 0x38565341u)
        die("bundle is corrupted, build this again?", 0, 1);
    if (le32(bundle_start + 4) != 1)
        die("unsupported bundle version", 0, 1);

    char *home = envget("HOME");
    if (!home)
        die("HOME env variable not set, this is needed for cache: ~/.cache/asv8/...", 0, 1);

    static char cache[4096];
    char *d = scpy(cache, home);
    d = scat(d, "/.cache/asv8/");
    hex32(d, le32(bundle_start + 8));

    mkdirs(cache);
    if (!extract(cache))
        die("failed to extract bundle into ", cache, 1);

    static char loader[4096], preload[4096], libpath[4096], target[4096];
    scpy(loader, cache);
    scat(loader, "/lib/ld-musl-aarch64.so.1");
    if (sys3(SYS_FACCESSAT, AT_FDCWD, (long)loader, 1) != 0)
        die("missing lib/ld-musl-aarch64.so.1 in bundle\n", 0, 1);

    scpy(preload, cache);
    scat(preload, "/lib/preload64.so");
    scpy(libpath, cache);
    scat(libpath, "/lib:");
    scat(libpath, cache);
    scat(libpath, "/usr/lib");

    if (!resolve(target, g_argv[1]))
        die("not found: ", g_argv[1], 127);

    g_xargv[0] = loader;
    g_xargv[1] = "--preload";
    g_xargv[2] = preload;
    g_xargv[3] = "--library-path";
    g_xargv[4] = libpath;
    g_xargv[5] = target;
    for (int i = 2; i < g_argc; i++) g_xargv[4 + i] = g_argv[i];
    g_xargv[g_argc + 4] = 0;

    long r = sys3(SYS_EXECVE, (long)loader, (long)g_xargv, (long)g_envp);
    if (r < 0)
        die("execve failed: ", target, 126);
    sys1(SYS_EXIT, 0);
}
