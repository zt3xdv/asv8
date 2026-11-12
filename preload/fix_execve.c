typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long usize;

extern char **environ;

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int streqn(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int endswith(const char *s, const char *suffix)
{
    int sl = slen(s);
    int suf = slen(suffix);
    if (sl < suf) return 0;
    return streq(s + sl - suf, suffix);
}

static char *g_real_argv0 = 0;

__attribute__((constructor)) static void init_real_argv0(void)
{
    for (int i = 0; environ[i]; i++) {
        if (streqn(environ[i], "ASV8_TARGET=", 12)) {
            g_real_argv0 = environ[i] + 12;
            break;
        }
    }
}

long execve(const char *path, char *const argv[], char *const envp[])
{
    if (g_real_argv0 && endswith(path, "ld-musl-aarch64.so.1")) {
        int argc = 0;
        while (argv[argc]) argc++;
        
        char *new_argv[64];
        new_argv[0] = g_real_argv0;
        for (int i = 1; i < argc && i < 63; i++) {
            new_argv[i] = argv[i];
        }
        new_argv[argc < 63 ? argc : 63] = 0;
        
        path = g_real_argv0;
        argv = new_argv;
    }
    register long x8 __asm__("x8") = 221;
    register long x0 __asm__("x0") = (long)path;
    register long x1 __asm__("x1") = (long)argv;
    register long x2 __asm__("x2") = (long)envp;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

long execveat(int fd, const char *path, char *const argv[], char *const envp[], int flags)
{
    if (g_real_argv0 && endswith(path, "ld-musl-aarch64.so.1")) {
        path = g_real_argv0;
    }
    register long x8 __asm__("x8") = 281;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = (long)argv;
    register long x3 __asm__("x3") = (long)envp;
    register long x4 __asm__("x4") = flags;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}
