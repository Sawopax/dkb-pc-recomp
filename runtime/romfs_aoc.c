// Read-only add-on content (DLC) volume: a second packed RomFS the game mounts with nn::fs::MountAddOnContent.
//
// TOTK_DLC (DKB_DLC, dlc in the settings file) names a directory holding the add-on's romfs.bin, written by
// tools/dkb_pack_romfs.py from the player's own dump of the add-on's RomFS, and its romfs-index.bin, written by
// tools/romfs_index.py --romfs <dir> --out <dir>. Unset or "0": the host has no add-on content and
// nn::aoc::CountAddOnContent answers 0, as before. The index is checked when it is loaded (format, sorted paths,
// bounds inside romfs.bin), not against compiled constants, so one executable runs with or without the add-on.
//
// Donkey Kong Bananza update 458752 has one add-on, index 1, which it mounts as "EmeraldRush" (main+0x5bf678) and
// reads through a projection device after the game's own RomFS. Handles live in their own range (AOC_HANDLE_BASE),
// between the game RomFS's and the host files', so fs_host.c tells the three apart by number.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "fs.h"
#include "host.h"
#include "romfs_path.h"
#include "platform/platform.h"

#define AOC_INDEX 1                          // the add-on content index the dump belongs to
#define NH 16384                             // handle slots
#define READ_CHUNK (64u << 20)

typedef struct { uint64_t offset, size; uint32_t name, len; } Entry;
_Static_assert(sizeof(Entry) == 24, "index entry layout");

static uint8_t *index_bytes;
static size_t index_size;
static uint64_t file_count, strings_offset, image_size;
static PlatFile *image;
static int ready;                            // 1: the add-on loaded and passed its checks
static char mount_name[32];
static PlatMutex lock = PLAT_MUTEX_INIT;
static uint64_t handles[NH];                 // file index + 1; 0 is free

static Entry entry(uint64_t i) {
    Entry e;
    memcpy(&e, index_bytes + 24 + i * 24, 24);
    return e;
}
static const char *entry_name(Entry e) { return (const char *)index_bytes + strings_offset + e.name; }

// A broken add-on is reported and left out: the game then runs without it, as on a console that lacks it.
static int bad(const char *dir, const char *why) {
    rt_log("aoc: add-on content in %s left out: %s", dir, why);
    free(index_bytes);
    index_bytes = NULL;
    if (image) plat_file_close(image);
    image = NULL;
    return 0;
}

static int load(const char *dir) {
    char path[2100];
    if (snprintf(path, sizeof path, "%s/romfs-index.bin", dir) >= (int)sizeof path) return bad(dir, "path too long");
    uint64_t sz = 0;
    index_bytes = plat_file_read(path, &sz);
    if (!index_bytes) return bad(dir, "no romfs-index.bin (tools/romfs_index.py --romfs <dir> --out <dir>)");
    if (sz < 24 || sz >= (64u << 20) || memcmp(index_bytes, "TOTKRFS1", 8)) return bad(dir, "romfs-index.bin format");
    index_size = (size_t)sz;
    memcpy(&file_count, index_bytes + 8, 8);
    memcpy(&strings_offset, index_bytes + 16, 8);
    if (!file_count || file_count > 1000000 || strings_offset != 24 + file_count * 24 || strings_offset >= index_size)
        return bad(dir, "romfs-index.bin layout");
    snprintf(path, sizeof path, "%s/romfs.bin", dir);
    image = plat_file_open(path);
    if (!image) return bad(dir, "no romfs.bin (tools/dkb_pack_romfs.py)");
    PlatFileId id;
    if (!plat_file_id(image, &id)) return bad(dir, "romfs.bin size");
    image_size = id.size;
    const char *previous = NULL;
    for (uint64_t i = 0; i < file_count; i++) {
        Entry e = entry(i);
        if (e.offset > image_size || e.size > image_size - e.offset) return bad(dir, "a file lies past romfs.bin");
        if (e.name >= index_size - strings_offset || e.len < 2 || e.len > index_size - strings_offset - e.name)
            return bad(dir, "a name lies past the index");
        const char *name = entry_name(e);
        if (name[e.len - 1] || strlen(name) != e.len - 1 || (previous && strcmp(previous, name) >= 0))
            return bad(dir, "paths not sorted");
        previous = name;
    }
    rt_log("aoc: add-on content %d in %s: %llu files, %llu bytes", AOC_INDEX, dir, (unsigned long long)file_count,
           (unsigned long long)image_size);
    return 1;
}

static void init_once(void *param) {
    (void)param;
    const char *dir = cfg_get("TOTK_DLC");
    if (!dir || !*dir || !strcmp(dir, "0")) return;
    ready = load(dir);
}

static void aoc_init(void) {
    static PlatOnce once = PLAT_ONCE_INIT;
    plat_once(&once, init_once, NULL);
}

// ---- lookup
static uint64_t lower(const char *name) {
    uint64_t lo = 0, hi = file_count;
    while (lo < hi) {
        uint64_t m = lo + (hi - lo) / 2;
        if (strcmp(entry_name(entry(m)), name) < 0) lo = m + 1;
        else hi = m;
    }
    return lo;
}
static uint64_t file_index(const char *p) {
    uint64_t i = lower(p);
    return i < file_count && !strcmp(p, entry_name(entry(i))) ? i : UINT64_MAX;
}

// ---- interface (fs.h)
int aoc_count(void) {
    aoc_init();
    return ready;
}

int aoc_list(int *out, int offset, int count) {
    if (!aoc_count() || !out || offset != 0 || count < 1) return 0;
    out[0] = AOC_INDEX;
    return 1;
}

uint32_t aoc_mount(const char *name, int index) {
    if (!aoc_count() || index != AOC_INDEX) return FS_R_TARGET_NOT_FOUND;
    if (!name || !romfs_path_valid_relative(name) || strlen(name) >= sizeof mount_name || strchr(name, '/') ||
        strchr(name, ':'))
        return FS_R_INVALID;
    plat_mutex_lock(&lock);
    int clash = *mount_name && strcmp(name, mount_name);
    if (!clash) strcpy(mount_name, name);
    plat_mutex_unlock(&lock);
    if (clash) {
        rt_log("aoc: second add-on mount %s (%s is mounted)", name, mount_name);
        return FS_R_ALREADY_EXISTS;
    }
    rt_log("fs mount %s: -> add-on content %d", name, index);
    return 0;
}

void aoc_unmount(void) {
    plat_mutex_lock(&lock);
    *mount_name = 0;
    plat_mutex_unlock(&lock);
}

int aoc_owns(const char *path) {
    size_t n = strlen(mount_name);
    return n && path && !strncmp(path, mount_name, n) && path[n] == ':';
}

int aoc_handle(uint64_t h) { return h > AOC_HANDLE_BASE && h < AOC_HANDLE_BASE + NH; }

uint32_t aoc_open(uint64_t *out, const char *path, int mode) {
    fs_trace("aoc", path);
    if (!out || mode != FS_OPEN_READ) return FS_R_UNSUPPORTED_OPEN;
    const char *p = romfs_path_relative(mount_name, path);
    if (!p) return FS_R_PATH_NOT_FOUND;
    uint64_t i = file_index(p);
    if (i == UINT64_MAX) return FS_R_PATH_NOT_FOUND;
    static volatile int32_t first;
    if (!__atomic_exchange_n(&first, 1, __ATOMIC_RELAXED)) rt_log("aoc: first add-on file opened: %s", path);
    plat_mutex_lock(&lock);
    for (unsigned j = 1; j < NH; j++)
        if (!handles[j]) {
            handles[j] = i + 1;
            *out = AOC_HANDLE_BASE + j;
            plat_mutex_unlock(&lock);
            return 0;
        }
    plat_mutex_unlock(&lock);
    rt_fatal("aoc: file handle table full (%d)", NH);
}

static int handle_entry(uint64_t h, Entry *e) {
    if (!aoc_handle(h)) return 0;
    plat_mutex_lock_shared(&lock);
    uint64_t i = handles[h - AOC_HANDLE_BASE];
    plat_mutex_unlock_shared(&lock);
    if (!i) return 0;
    *e = entry(i - 1);
    return 1;
}

uint32_t aoc_read(uint64_t *out, uint64_t h, int64_t offset, void *buffer, uint64_t size) {
    Entry e;
    if (!out || offset < 0 || !handle_entry(h, &e)) return FS_R_INVALID;
    if ((uint64_t)offset > e.size || (!buffer && size)) return FS_R_INVALID;
    uint64_t n = size < e.size - (uint64_t)offset ? size : e.size - (uint64_t)offset, done = 0;
    while (done < n) {
        uint32_t chunk = n - done > READ_CHUNK ? READ_CHUNK : (uint32_t)(n - done);
        mem_thaw((uintptr_t)buffer + done, chunk);
        int64_t r = plat_file_pread(image, (uint8_t *)buffer + done, chunk, e.offset + (uint64_t)offset + done);
        if (r <= 0) rt_fatal("aoc: read failure at %#llx", (unsigned long long)(e.offset + offset + done));
        done += (uint64_t)r;
    }
    *out = done;
    return 0;
}

uint32_t aoc_size(int64_t *out, uint64_t h) {
    Entry e;
    if (!out || !handle_entry(h, &e)) return FS_R_INVALID;
    *out = (int64_t)e.size;
    return 0;
}

void aoc_close(uint64_t h) {
    Entry e;
    if (!handle_entry(h, &e)) rt_fatal("aoc: close of invalid file handle %#llx", (unsigned long long)h);
    plat_mutex_lock(&lock);
    handles[h - AOC_HANDLE_BASE] = 0;
    plat_mutex_unlock(&lock);
}

uint32_t aoc_entry_type(int *out, const char *path) {
    if (!out) return FS_R_INVALID;
    const char *p = romfs_path_relative(mount_name, path);
    if (!p) return FS_R_PATH_NOT_FOUND;
    if (file_index(p) != UINT64_MAX) {
        *out = FS_ENTRY_FILE;
        return 0;
    }
    char dir[4096];
    if (snprintf(dir, sizeof dir, "%s/", p) >= (int)sizeof dir) return FS_R_INVALID;
    uint64_t i = lower(dir);
    if (i < file_count && !strncmp(entry_name(entry(i)), dir, strlen(dir))) {
        *out = FS_ENTRY_DIRECTORY;
        return 0;
    }
    return FS_R_PATH_NOT_FOUND;
}
