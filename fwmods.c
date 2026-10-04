// FWMods.dylib - exposes Documents/DoNotStarveTogether/mods to the game.
//  * creates the folder on launch
//  * writes <mods>/mods.lua (AddMods("<folder>") per sub folder) for the framework loader
//  * redirects file access under "scripts/mods/" to the Documents folder when the file exists there
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

#define MODS_MARK "scripts/mods"

static char g_root[1024];      // $HOME/Documents/DoNotStarveTogether/mods
static size_t g_root_len;

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

// Returns 1 and fills out when `path` points inside scripts/mods and the same
// relative path exists below g_root. is_dir_root is set when path is the mods root itself.
static int redirect(const char *path, char *out, size_t cap) {
    if (!path || !g_root_len) return 0;
    const char *m = strstr(path, MODS_MARK);
    if (!m) return 0;
    if (m != path && m[-1] != '/') return 0;
    const char *rest = m + sizeof(MODS_MARK) - 1;
    if (*rest != '/' && *rest != 0) return 0;
    while (*rest == '/') rest++;
    if (strstr(rest, "..")) return 0;                      // never allow escaping the folder
    if (*rest) snprintf(out, cap, "%s/%s", g_root, rest);
    else       snprintf(out, cap, "%s", g_root);
    return 1;
}

static int doc_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
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
static DIR *my_opendir(const char *path) {
    char buf[1536];
    if (redirect(path, buf, sizeof buf) && doc_exists(buf)) return opendir(buf);
    return opendir(path);
}

#define INTERPOSE(rep, orig) \
    __attribute__((used)) static struct { const void *r; const void *o; } _ip_##orig \
    __attribute__((section("__DATA,__interpose"))) = { (const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig }

INTERPOSE(my_open,    open);
INTERPOSE(my_fopen,   fopen);
INTERPOSE(my_stat,    stat);
INTERPOSE(my_lstat,   lstat);
INTERPOSE(my_access,  access);
INTERPOSE(my_opendir, opendir);

// ---- startup ------------------------------------------------------------
static void write_manifest(void) {
    char mf[1100];
    snprintf(mf, sizeof mf, "%s/mods.lua", g_root);
    FILE *f = fopen(mf, "w");
    if (!f) return;
    DIR *d = opendir(g_root);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[1300]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", g_root, e->d_name);
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (strchr(e->d_name, '"') || strchr(e->d_name, '\\') || strstr(e->d_name, "..")) continue;
        fprintf(f, "AddMods(\"%s\")\n", e->d_name);
    }
    if (d) closedir(d);
    fclose(f);
}

__attribute__((constructor)) static void fwmods_init(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(g_root, sizeof g_root, "%s/Documents/DoNotStarveTogether/mods", home);
    g_root_len = strlen(g_root);
    mkdir_p(g_root);
    write_manifest();
}
