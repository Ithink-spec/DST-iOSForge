#import <Foundation/Foundation.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <algorithm>

static bool isDirectory(const char *path) {
    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string docsPath() {
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    return docs ? std::string(docs.UTF8String) : std::string();
}

static void writeManifest() {
    std::string root = docsPath();
    if (root.empty()) return;
    root += "/DoNotStarveTogether/mods";

    mkdir((docsPath()+"/DoNotStarveTogether").c_str(), 0755);
    mkdir(root.c_str(), 0755);

    DIR *dir = opendir(root.c_str());
    if (!dir) return;

    std::vector<std::string> mods;
    dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (ent->d_type != DT_DIR && ent->d_type != DT_UNKNOWN) continue;

        std::string name = ent->d_name;
        std::string modinfo = root + "/" + name + "/modinfo.lua";
        if (access(modinfo.c_str(), R_OK) == 0)
            mods.push_back(name);
    }
    closedir(dir);

    std::sort(mods.begin(), mods.end());

    std::string manifest = root + "/external_mods.lua";
    FILE *fp = fopen(manifest.c_str(), "wb");
    if (!fp) return;

    fputs("return {\\n", fp);
    for (const auto &name : mods) {
        fputs("    [", fp);
        fputc('\'', fp);
        for (char c : name) {
            if (c == '\\'') fputc('\\', fp);
            fputc(c, fp);
        }
        fputs("] = true,\\n", fp);
    }
    fputs("}\\n", fp);
    fclose(fp);
}

__attribute__((constructor))
static void DSTModsInit() {
    @autoreleasepool {
        writeManifest();
    }
}
