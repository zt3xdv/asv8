typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long usize;

struct sa4 { u16 family; u16 port; u32 addr; u8 zero[8]; };
struct addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    u32 ai_addrlen;
    struct sa4 *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
struct pollfd { int fd; short events, revents; };
struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

extern void *malloc(usize);
extern void free(void *);
extern void *memset(void *, int, usize);
extern void *memcpy(void *, const void *, usize);
extern int socket(int, int, int);
extern int close(int);
extern long sendto(int, const void *, usize, int, const void *, u32);
extern long recvfrom(int, void *, usize, int, void *, u32 *);
extern int poll(struct pollfd *, u32, int);
extern char *getenv(const char *);
extern int open(const char *, int, ...);
extern long read(int, void *, usize);
extern int *__h_errno_location(void);

static int strcopy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
    return i;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int parse_ipv4(const char *s, u32 *out)
{
    u32 parts[4];
    int idx = 0, digits = 0;
    u32 v = 0;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            v = v * 10 + (u32)(c - '0');
            if (++digits > 3) return 0;
        } else if (c == '.' || c == 0) {
            if (!digits || v > 255 || idx > 3) return 0;
            parts[idx++] = v;
            v = 0;
            digits = 0;
            if (c == 0) break;
        } else return 0;
        s++;
    }
    if (idx != 4) return 0;
    *out = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
    return 1;
}

static u32 g_ns[4];
static int g_nscount;
static int g_nsloaded;

static void load_resolv_conf(void)
{
    if (g_nsloaded) return;
    g_nsloaded = 1;
    g_ns[0] = 0x08080808u;
    g_ns[1] = 0x01010101u;
    g_nscount = 2;
    const char *p = getenv("DNS64_RESOLV");
    if (!p) p = getenv("RESOLV_CONF");
    if (!p) p = "/etc/resolv.conf";
    int fd = open(p, 0);
    if (fd < 0) return;
    static char buf[4096];
    long n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    u32 found[4];
    int fc = 0;
    char *line = buf;
    while (line && *line && fc < 4) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        int isend = !*nl;
        *nl = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == 'n' && s[1] == 'a' && s[2] == 'm' && s[3] == 'e' &&
            s[4] == 's' && s[5] == 'e' && s[6] == 'r' && s[7] == 'v' &&
            (s[8] == ' ' || s[8] == '\t')) {
            char *v = s + 9;
            while (*v == ' ' || *v == '\t') v++;
            u32 ip;
            if (parse_ipv4(v, &ip)) found[fc++] = ip;
        }
        if (isend) break;
        line = nl + 1;
    }
    if (fc > 0) {
        for (int i = 0; i < fc; i++) g_ns[i] = found[i];
        g_nscount = fc;
    }
}

static int mkresult(u32 ip, u16 port, int socktype, int flags, struct addrinfo **res, const char *node)
{
    usize clen = 0;
    if (node && (flags & 2)) { while (node[clen]) clen++; }
    char *blk = malloc(88 + clen + 1);
    if (!blk) return -10;
    memset(blk, 0, 88 + clen + 1);
    struct addrinfo *ai = (struct addrinfo *)blk;
    struct sa4 *sa = (struct sa4 *)(blk + 0x30);
    sa->family = 2;
    sa->port = (u16)((port >> 8) | ((port & 0xff) << 8));
    sa->addr = ip;
    ai->ai_family = 2;
    ai->ai_socktype = socktype;
    ai->ai_addrlen = 16;
    ai->ai_addr = sa;
    ai->ai_flags = flags;
    if (clen) {
        memcpy(blk + 88, node, clen + 1);
        ai->ai_canonname = blk + 88;
    }
    *(int *)(blk + 0x4c) = 0;
    *(short *)(blk + 0x50) = 0;
    *(unsigned short *)(blk + 0x52) = 1;
    *res = ai;
    return 0;
}

static int build_query(u8 *p, const char *name)
{
    u8 *start = p;
    *p++ = 0xab; *p++ = 0xcd;
    *p++ = 0x01; *p++ = 0x00;
    *p++ = 0; *p++ = 1;
    *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
    const char *s = name;
    while (*s) {
        const char *dot = s;
        while (*dot && *dot != '.') dot++;
        int len = dot - s;
        if (len <= 0 || len > 63 || p - start > 270) return -2;
        *p++ = (u8)len;
        memcpy(p, s, len);
        p += len;
        s = dot;
        if (*s == '.') s++;
    }
    *p++ = 0;
    *p++ = 0; *p++ = 1;
    *p++ = 0; *p++ = 1;
    return (int)(p - start);
}

static int dns_query(const char *name, u8 *ans)
{
    u8 pkt[300];
    int qlen = build_query(pkt, name);
    if (qlen < 0) return qlen;
    load_resolv_conf();
    int fd = socket(2, 2, 0);
    if (fd < 0) return -11;
    for (int i = 0; i < g_nscount; i++) {
        struct sa4 dst;
        memset(&dst, 0, sizeof dst);
        dst.family = 2;
        dst.port = 0x3500;
        dst.addr = g_ns[i];
        sendto(fd, pkt, qlen, 0, &dst, sizeof dst);
    }
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = 1;
    if (poll(&pfd, 1, 3000) <= 0) {
        close(fd);
        return -3;
    }
    long n = recvfrom(fd, ans, 1024, 0, 0, 0);
    close(fd);
    if (n < 12) return -3;
    return (int)n;
}

static int dns_parse(u8 *a, int n, u32 *ip)
{
    if ((a[3] & 0x0f) == 3) return -2;
    if ((a[3] & 0x0f) != 0) return -4;
    int i = 12;
    while (i < n && a[i]) {
        if ((a[i] & 0xc0) == 0xc0) { i += 2; break; }
        i += a[i] + 1;
    }
    if (a[i - 1]) i++;
    i += 4;
    int ancount = (a[6] << 8) | a[7];
    for (int k = 0; k < ancount && i + 12 <= n; k++) {
        while (i < n) {
            if ((a[i] & 0xc0) == 0xc0) { i += 2; break; }
            if (!a[i]) { i++; break; }
            i += a[i] + 1;
        }
        if (i + 10 > n) break;
        int type = (a[i] << 8) | a[i + 1];
        int rclass = (a[i + 2] << 8) | a[i + 3];
        int rdlen = (a[i + 8] << 8) | a[i + 9];
        i += 10;
        if (i + rdlen > n) break;
        if (type == 1 && rclass == 1 && rdlen == 4) {
            *ip = a[i] | (a[i + 1] << 8) | (a[i + 2] << 16) | ((u32)a[i + 3] << 24);
            return 0;
        }
        i += rdlen;
    }
    return -2;
}

static int resolve_name(const char *name, u32 *ip)
{
    if (parse_ipv4(name, ip)) return 0;
    if (streq(name, "localhost")) {
        *ip = 0x0000007fu;
        return 0;
    }
    u8 ans[1024];
    int n = dns_query(name, ans);
    if (n < 0) return n;
    int r = dns_parse(ans, n, ip);
    if (r) return r;
    return 0;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    *res = 0;
    int family = 0, socktype = 0, flags = 0;
    if (hints) {
        family = hints->ai_family;
        socktype = hints->ai_socktype;
        flags = hints->ai_flags;
    }
    if (family != 0 && family != 2) return -6;
    u16 port = 0;
    if (service && *service) {
        u32 pv = 0;
        int d = 0;
        for (const char *s = service; *s; s++) {
            if (*s < '0' || *s > '9') return -8;
            pv = pv * 10 + (u32)(*s - '0');
            if (++d > 5) return -8;
        }
        if (pv > 65535) return -8;
        port = (u16)pv;
    }
    if (!node)
        return mkresult((flags & 1) ? 0 : 0x0000007fu, port, socktype, flags, res, 0);
    u32 ip;
    if (flags & 4) {
        if (!parse_ipv4(node, &ip)) return -2;
    } else if (resolve_name(node, &ip)) {
        return -2;
    }
    return mkresult(ip, port, socktype, flags, res, node);
}

static char g_hn[256];
static u32 g_hip;
static char *g_hlist[2];
static struct hostent g_hent;

static struct hostent *host_result(const char *name, u32 ip)
{
    strcopy(g_hn, name, (int)sizeof g_hn);
    g_hip = ip;
    g_hlist[0] = (char *)&g_hip;
    g_hlist[1] = 0;
    g_hent.h_name = g_hn;
    g_hent.h_aliases = 0;
    g_hent.h_addrtype = 2;
    g_hent.h_length = 4;
    g_hent.h_addr_list = g_hlist;
    return &g_hent;
}

struct hostent *gethostbyname(const char *name)
{
    u32 ip;
    if (!resolve_name(name, &ip)) return host_result(name, ip);
    *__h_errno_location() = 2;
    return 0;
}

struct hostent *gethostbyname2(const char *name, int af)
{
    if (af != 2 && af != 0) {
        *__h_errno_location() = 3;
        return 0;
    }
    u32 ip;
    if (!resolve_name(name, &ip)) return host_result(name, ip);
    *__h_errno_location() = 2;
    return 0;
}
