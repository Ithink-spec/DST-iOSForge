// Build (on a Mac):  see build.sh
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

static char g_root[1024];          // $HOME/Documents/DoNotStarveTogether/mods
static size_t g_root_len;
static char g_manifest_path[1200]; // $HOME/Documents/DoNotStarveTogether/.changmods/modsettings.lua

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

// Returns 1 and fills `out` when `path` points inside the game's mods folder.
// *is_root is set when `path` is the mods folder itself.
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

// ---- 文件夹筛选 -------------------------------------------------------------
static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

static int dir_exists(const char *name) {
    char p[1400]; struct stat st;
    snprintf(p, sizeof p, "%s/%s", g_root, name);
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

// "workshop-1365141672 2" -> "workshop-1365141672"；不是副本返回 0
static int dup_base(const char *name, char *base, size_t cap) {
    size_t n = strlen(name);
    size_t i = n;
    while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9') i--;
    if (i == n || i == 0) return 0;                 // 没有数字后缀 / 全是数字
    if (name[i - 1] != ' ') return 0;               // 数字前面不是空格
    size_t bl = i - 1;
    if (bl == 0 || bl >= cap) return 0;
    memcpy(base, name, bl);
    base[bl] = 0;
    return 1;
}

// 目录列表里这一项要不要露出来（重复副本、.off/dot 都不露）
static int list_ok(const char *name) {
    if (name[0] == '.') return 0;
    if (ends_with(name, ".off") || ends_with(name, ".disabled")) return 0;
    if (strchr(name, '"') || strchr(name, '\\') || strstr(name, "..")) return 0;
    char base[256];
    if (dup_base(name, base, sizeof base) && dir_exists(base)) return 0;
    return 1;
}

// ---- 虚拟清单：把 mods 目录里的 modsettings.lua / modsetting.lua 指到隐藏文件 ----
static int is_manifest_basename(const char *name) {
    return name && (!strcmp(name, "modsettings.lua") || !strcmp(name, "modsetting.lua"));
}

// 返回 path 在 "mods 根目录" 之后的部分（只接受根目录下的直接文件，不含子目录）
static const char *mods_root_child(const char *path) {
    if (!path) return NULL;
    for (size_t k = 0; k < sizeof MARKS / sizeof MARKS[0]; k++) {
        size_t ml = strlen(MARKS[k]);
        const char *m = strstr(path, MARKS[k]);
        if (!m) continue;
        if (m != path && m[-1] != '/') continue;
        const char *rest = m + ml;
        if (*rest != '/') continue;
        while (*rest == '/') rest++;
        if (*rest == 0) continue;
        if (strchr(rest, '/')) continue;          // 子目录里的文件，不管
        return rest;
    }
    return NULL;
}

// 若 path 指向 mods 根目录下的清单文件，返回 1 并把隐藏文件路径写进 out
static int map_manifest(const char *path, char *out, size_t cap) {
    if (!path || !g_manifest_path[0]) return 0;

    // 1) ../mods/modsettings.lua、scripts/mods/modsetting.lua
    const char *rest = mods_root_child(path);
    if (rest != NULL && is_manifest_basename(rest)) {
        snprintf(out, cap, "%s", g_manifest_path);
        return 1;
    }

    // 2) 绝对路径：.../mods/modsettings.lua
    const char *base = strrchr(path, '/');
    if (base != NULL && is_manifest_basename(base + 1)) {
        const char *p = strstr(path, "/mods/");
        if (p != NULL) {
            const char *r2 = p + 6;
            if (*r2 != 0 && strchr(r2, '/') == NULL) {
                snprintf(out, cap, "%s", g_manifest_path);
                return 1;
            }
        }
    }
    return 0;
}

// ---- replacements -------------------------------------------------------
static int my_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap); }
    char virt[1536];
    if (!(flags & (O_WRONLY | O_RDWR | O_CREAT)) && map_manifest(path, virt, sizeof virt) && doc_exists(virt))
        return open(virt, flags, mode);
    char buf[1536];
    if (!(flags & (O_WRONLY | O_RDWR | O_CREAT)) && redirect(path, buf, sizeof buf) && doc_exists(buf))
        return open(buf, flags, mode);
    return open(path, flags, mode);
}
static FILE *my_fopen(const char *path, const char *mode) {
    char virt[1536];
    if (mode && mode[0] == 'r' && !strchr(mode, '+') && map_manifest(path, virt, sizeof virt) && doc_exists(virt))
        return fopen(virt, mode);
    char buf[1536];
    if (mode && mode[0] == 'r' && !strchr(mode, '+') && redirect(path, buf, sizeof buf) && doc_exists(buf))
        return fopen(buf, mode);
    return fopen(path, mode);
}
static int my_stat(const char *path, struct stat *st) {
    char virt[1536];
    if (map_manifest(path, virt, sizeof virt) && doc_exists(virt)) return stat(virt, st);
    char buf[1536];
    if (redirect(path, buf, sizeof buf) && doc_exists(buf)) return stat(buf, st);
    return stat(path, st);
}
static int my_lstat(const char *path, struct stat *st) {
    char virt[1536];
    if (map_manifest(path, virt, sizeof virt) && doc_exists(virt)) return lstat(virt, st);
    char buf[1536];
    if (redirect(path, buf, sizeof buf) && doc_exists(buf)) return lstat(buf, st);
    return lstat(path, st);
}
static int my_access(const char *path, int mode) {
    char virt[1536];
    if (!(mode & W_OK) && map_manifest(path, virt, sizeof virt) && doc_exists(virt)) return access(virt, mode);
    char buf[1536];
    if (!(mode & W_OK) && redirect(path, buf, sizeof buf) && doc_exists(buf)) return access(buf, mode);
    return access(path, mode);
}
// The mods folder itself is listed as the union of the Documents folder and the
// original one, so mods shipped inside the app keep working next to user mods.
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
            if (!list_ok(e->d_name)) continue;              // 重复副本 / .off 不露出来
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

// ---- startup ------------------------------------------------------------
#define MAXMODS 256

static int file_exists_in(const char *dir, const char *name) {
    char p[1400];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    struct stat st;
    return stat(p, &st) == 0;
}

// 一个文件夹是否该写进清单
static int keep_mod(const char *name) {
    if (!list_ok(name)) return 0;                                   // ".": 隐藏 / .off / 重复副本 / 非法名

    char dir[1400];
    snprintf(dir, sizeof dir, "%s/%s", g_root, name);

    if (!file_exists_in(dir, "modinfo.lua")) return 0;              // 不是模组
    if (file_exists_in(dir, "disabled") || file_exists_in(dir, ".disabled")) return 0;

    return 1;
}

static int cmp_name(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// 写清单：写到 mods 目录【外面】的隐藏位置，mods 里不再出现任何 .lua
static void write_manifest(void) {
    const char *names[MAXMODS];
    int count = 0;
    char store[MAXMODS][256];

    DIR *d = opendir(g_root);
    struct dirent *e;
    while (d && (e = readdir(d)) && count < MAXMODS) {
        if (e->d_name[0] == '.') continue;
        char p[1400]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", g_root, e->d_name);
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (!keep_mod(e->d_name)) continue;
        snprintf(store[count], sizeof store[count], "%s", e->d_name);
        names[count] = store[count];
        count++;
    }
    if (d) closedir(d);

    if (count > 1) qsort(names, count, sizeof names[0], cmp_name);

    FILE *f = fopen(g_manifest_path, "w");
    if (!f) return;

    fprintf(f, "-- generated by ChangMods.dylib (not in the mods folder)\n");
    fprintf(f, "local Add = ForceEnableMod or Add\n");

    for (int i = 0; i < count; i++) {
        fprintf(f, "Add(\"%s\")\n", names[i]);
    }
    fclose(f);
}

__attribute__((constructor)) static void changmods_init(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    snprintf(g_root, sizeof g_root, "%s/Documents/DoNotStarveTogether/mods", home);
    g_root_len = strlen(g_root);
    mkdir_p(g_root);

    // 列表位
    char mdir[1200];
    snprintf(mdir, sizeof mdir, "%s/Documents/DoNotStarveTogether/.changmods", home);
    mkdir_p(mdir);
    snprintf(g_manifest_path, sizeof g_manifest_path, "%s/modsettings.lua", mdir);

    write_manifest();

    // chang外置自动写入列表隐藏
    char old1[1200], old2[1200];
    snprintf(old1, sizeof old1, "%s/modsettings.lua", g_root);
    snprintf(old2, sizeof old2, "%s/modsetting.lua", g_root);
    unlink(old1);
    unlink(old2);
}