#include <libpmem.h>
#include <uuid/uuid.h>
#include <fstream>
#include <string.h>
#include "nv_log.h"
#include "constants.h"

#define CHECKSUM(log) ((&log->checksum)[1] ^ (&log->checksum)[2] ^ (&log->checksum)[3])

static const uint64_t LogMagic = REDO_LOG_MAGIC;

void Savitar_log_path(uuid_t uuid, char *path) {
    assert(uuid_is_null(uuid) == 0);

    char uuid_str[64];
    uuid_unparse(uuid, uuid_str);

    strcpy(path, PMEM_PATH);
    strcat(path, uuid_str);
    strcat(path, ".log");
}

bool Savitar_log_exists(uuid_t uuid) {
    char path[255];
    Savitar_log_path(uuid, path);
    std::ifstream f(path);
    return f.good();
}

SavitarLog *Savitar_log_open(uuid_t id) {
    char path[255];
    size_t mapped_len;
    Savitar_log_path(id, path);

    PRINT("Opening existing log at %s\n", path);
    SavitarLog *log = (SavitarLog *)pmem_map_file(path, 0, 0, 0,
            &mapped_len, NULL);
    assert(log == NULL || log->size == mapped_len);
    // assert(log == NULL || log->checksum == CHECKSUM(log));
    if (log != NULL) log->snapshot_lock = 0;
    return log;
}

SavitarLog *Savitar_log_create(uuid_t id, size_t size) {
    char path[255];
    size_t mapped_len;
    Savitar_log_path(id, path);

    SavitarLog *log = (SavitarLog *)pmem_map_file(path, size,
            PMEM_FILE_CREATE | PMEM_FILE_EXCL, 0666, &mapped_len, NULL);
    if (log != NULL) {
        assert(mapped_len == size);
        log->size = size;
        uuid_copy(log->object_id, id);
        assert(sizeof(struct RedoLog) == CACHE_LINE_WIDTH);
        log->tail = sizeof(struct RedoLog);
        log->head = log->tail;
        log->last_commit = 0;
        log->snapshot_lock = 0;
        log->checksum = CHECKSUM(log);
        pmem_persist(log, sizeof(struct RedoLog));
        PRINT("Created new semantic log at %s\n", path);
    }
    else {
      PRINT("Failed to create semantic log at %s\n", path);
    }
    return log;
}

void Savitar_log_close(SavitarLog *log) {
    char uuid[64];
    uuid_unparse(log->object_id, uuid);
    pmem_unmap(log, log->size);
    PRINT("Closed semantic log: %s\n", uuid);
}

void Savitar_log_commit(SavitarLog *log, uint64_t entry_offset) {
    uint64_t commit_id = __sync_add_and_fetch(&log->last_commit, 1);
    assert(commit_id < UINT64_MAX);
    uint64_t *ptr = (uint64_t *)((char *)log + entry_offset);
    *ptr = commit_id;
    pmem_persist(ptr, sizeof(commit_id));
    PRINT("[%d] Marked log entry (%zu) as committed with id = %zu\n",
            (int)pthread_self(), entry_offset, commit_id);
}

