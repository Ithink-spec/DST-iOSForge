#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

static const char *MARKS[] = { "scripts/mods", "../mods" };

static char g_root[1024];          

static size_t g_root_len;

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static int redirect2(const char *path, char *out, size_t cap, int *is_root) {
    if (is_root) *is_root = 0;
    if (!path || !g_root_len) return 0;
    for (size_t k = 0; k < sizeof MARKS / sizeof MARKS[0]; k++) {
        size_t ml = strlen(MARKS[k]);
        const char *m = strstr(path, MARKS[k]);
        if (!m) continue;
        if (m != path && m[-1] != '/') continue;
        const char *rest = m + ml;
        if (*rest != '/' && *rest != 0) continue;
        while (*rest == '/') rest++;
        if (strstr(rest, "..")) return 0;                  // never allow escaping the folder
        if (*rest) snprintf(out, cap, "%s/%s", g_root, rest);
        else { snprintf(out, cap, "%s", g_root); if (is_root) *is_root = 1; }
        return 1;
    }
    return 0;
}
static int redirect(const char *path, char *out, size_t cap) { return redirect2(path, out, cap, NULL); }

static int doc_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

static int dir_exists(const char *name) {
    char p[1400]; struct stat st;
    snprintf(p, sizeof p, "%s/%s", g_root, name);
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int dup_base(const char *name, char *base, size_t cap) {
    size_t n = strlen(name);
    size_t i = n;
    while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9') i--;
    if (i == n || i == 0) return 0;             
    
    if (name[i - 1] != ' ') return 0;               
    size_t bl = i - 1;
    if (bl == 0 || bl >= cap) return 0;
    memcpy(base, name, bl);
    base[bl] = 0;
    return 1;
}

static int list_ok(const char *name) {
    if (name[0] == '.') return 0;
    if (ends_with(name, ".off") || ends_with(name, ".disabled")) return 0;
    if (strchr(name, '"') || strchr(name, '\\') || strstr(name, "..")) return 0;
    char base[256];
    if (dup_base(name, base, sizeof base) && dir_exists(base)) return 0;
    return 1;
}

// ---- replacements -------------------------------------------------------
static int my_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    char buf[1536];
    if (!(flags & (O_WRONLY | O_RDWR | O_CREAT)) && redirect(path, buf, sizeof buf) && doc_exists(buf))
        return open(buf, flags, mode);
    return open(path, flags, mode);
}
static FILE *my_fopen(const char *path, const char *mode) {
    char buf[1536];
    if (mode && mode[0] == 'r' && !strchr(mode, '+') && redirect(path, buf, sizeof buf) && doc_exists(buf))
        return fopen(buf, mode);
    return fopen(path, mode);
}
static int my_stat(const char *path, struct stat *st) {
    char buf[1536];
    if (redirect(path, buf, sizeof buf) && doc_exists(buf)) return stat(buf, st);
    return stat(path, st);
}
static int my_lstat(const char *path, struct stat *st) {
    char buf[1536];
    if (redirect(path, buf, sizeof buf) && doc_exists(buf)) return lstat(buf, st);
    return lstat(path, st);
}
static int my_access(const char *path, int mode) {
    char buf[1536];
    if (!(mode & W_OK) && redirect(path, buf, sizeof buf) && doc_exists(buf)) return access(buf, mode);
    return access(path, mode);
}

#define MAXM 8
static struct { DIR *a; DIR *b; int phase; } g_m[MAXM];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int find_slot(DIR *d) {
    for (int i = 0; i < MAXM; i++) if (g_m[i].a == d) return i;
    return -1;
}

static DIR *my_opendir(const char *path) {
    char buf[1536]; int root = 0;
    if (redirect2(path, buf, sizeof buf, &root) && doc_exists(buf)) {
        DIR *da = opendir(buf);
        if (!da) return opendir(path);
        if (!root) return da;
        DIR *db = opendir(path);
        if (!db) return da;
        int stored = 0;
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < MAXM; i++)
            if (!g_m[i].a) { g_m[i].a = da; g_m[i].b = db; g_m[i].phase = 0; stored = 1; break; }
        pthread_mutex_unlock(&g_lock);
        if (!stored) closedir(db);
        return da;
    }
    return opendir(path);
}

static struct dirent *my_readdir(DIR *d) {
    pthread_mutex_lock(&g_lock);
    int i = find_slot(d);
    DIR *b = NULL; int phase = 0;
    if (i >= 0) { b = g_m[i].b; phase = g_m[i].phase; }
    pthread_mutex_unlock(&g_lock);
    if (i < 0) return readdir(d);
    if (phase == 0) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) return e;
            if (!list_ok(e->d_name)) continue;  
            
            return e;
        }
        pthread_mutex_lock(&g_lock); g_m[i].phase = 1; pthread_mutex_unlock(&g_lock);
    }
    struct dirent *e;
    while ((e = readdir(b)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!list_ok(e->d_name)) continue;
        char p[1300];                                       // skip names the Documents folder already has
        snprintf(p, sizeof p, "%s/%s", g_root, e->d_name);
        if (doc_exists(p)) continue;
        return e;
    }
    return NULL;
}

static int my_closedir(DIR *d) {
    pthread_mutex_lock(&g_lock);
    int i = find_slot(d);
    DIR *b = NULL;
    if (i >= 0) { b = g_m[i].b; g_m[i].a = NULL; g_m[i].b = NULL; }
    pthread_mutex_unlock(&g_lock);
    if (b) closedir(b);
    return closedir(d);
}

static void my_rewinddir(DIR *d) {
    pthread_mutex_lock(&g_lock);
    int i = find_slot(d);
    DIR *b = (i >= 0) ? g_m[i].b : NULL;
    if (i >= 0) g_m[i].phase = 0;
    pthread_mutex_unlock(&g_lock);
    if (b) rewinddir(b);
    rewinddir(d);
}

#ifdef __APPLE__
#define INTERPOSE(rep, orig) \
    __attribute__((used)) static struct { const void *r; const void *o; } _ip_##orig \
    __attribute__((section("__DATA,__interpose"))) = { (const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig }
#else
#define INTERPOSE(rep, orig)   /* test build on non-Apple hosts */
#endif

INTERPOSE(my_open,    open);
INTERPOSE(my_fopen,   fopen);
INTERPOSE(my_stat,    stat);
INTERPOSE(my_lstat,   lstat);
INTERPOSE(my_access,  access);
INTERPOSE(my_opendir,   opendir);
INTERPOSE(my_readdir,   readdir);
INTERPOSE(my_closedir,  closedir);
INTERPOSE(my_rewinddir, rewinddir);

static void cleanup_legacy_manifest(void) {
    char p[1200];

    snprintf(p, sizeof p, "%s/modsettings.lua", g_root);
    unlink(p);
    snprintf(p, sizeof p, "%s/modsetting.lua", g_root);
    unlink(p);

    const char *home = getenv("HOME");
    if (home) {
        snprintf(p, sizeof p, "%s/Documents/DoNotStarveTogether/.changmods/modsettings.lua", home);
        unlink(p);
    }
}

__attribute__((constructor)) static void changmods_init(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    snprintf(g_root, sizeof g_root, "%s/Documents/DoNotStarveTogether/mods", home);
    g_root_len = strlen(g_root);
    mkdir_p(g_root);

    cleanup_legacy_manifest();
}