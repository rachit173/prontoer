#include "libpmem.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <functional>
#include <optional>
#include <thread>
#include <cassert>

#include "constants.h"
#include "nv_object.h"
#define SLOT_SIZE 64
static const uint64_t LogMagic = REDO_LOG_MAGIC;
static const uint64_t NoLogMagic = 0;
static int available_cores = 0; // including Hyper-Threaded cores
static uint16_t core_tenants[MAX_CORES / 2];
static uint8_t core_ht_map[MAX_CORES / 2][2];
static pthread_mutex_t core_tenants_lock;

#define MAX_SOCKETS 2



bool coreInfoCompare(std::pair<long,long> a, std::pair<long,long> b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second < b.second;
}

void getCPUInfo(uint8_t *core_map, int *map_size) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[1024];
    char tag[128];
    char value[1024];

    long processor; // core id (with HT)
    long core_id; // physical core id (no HT)
    long physical_id; // socket id
    long cpu_cores; // physical cores per socket

    *map_size = 0;
    std::vector<std::pair<long,long>> sockets[MAX_SOCKETS];

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strlen(line) == 1) continue;
        sscanf(line, "%[^\t:] : %[^\t\n]", tag, value);

        if (strcmp(tag, "core id") == 0) {
            core_id = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "physical id") == 0) {
            physical_id = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "processor") == 0) {
            processor = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "cpu cores") == 0) {
            cpu_cores = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "flags") == 0) {
            assert(physical_id < MAX_SOCKETS);
            sockets[physical_id].push_back(std::pair<long,long>(core_id, processor));
            *map_size = *map_size + 1;
        }
    }

    fclose(f);

    for (int i = 0; i < MAX_SOCKETS; i++) {
        assert(i < 7); // 3 bits
        std::sort(sockets[i].begin(), sockets[i].end(), coreInfoCompare);

        // <first, second> = <core_id, processor>
        for (auto core = sockets[i].begin(); core != sockets[i].end(); core++) {
            assert(core->first < 32); // 5 bits
            core_map[core->second] = core->first | (i << 5);
        }

        sockets[i].clear();
    }
}

void coreInit() {
    uint8_t core_info[MAX_CORES];
    getCPUInfo(core_info, &available_cores);
    assert(available_cores <= MAX_CORES);
    printf("Found a total of %d active cores.\n", available_cores);
    assert(available_cores % 2 == 0);

    printf("Zeroing core affinity data structures for %d physical cores.\n",
            available_cores / 2);
    for (int i = 0; i < (available_cores >> 1); i++) {
        core_tenants[i] = 0; // physical cores
    }
    assert(pthread_mutex_init(&core_tenants_lock, NULL) == 0);

    // Create HT to Physical mapping
    uint8_t cores_per_socket = 0; // assumes identical CPUs
    printf("Printing Hyper-Threading map\n");
    for (uint8_t c = 0; c < (available_cores >> 1); c++) {
        for (uint8_t ht = 0; ht < 2; ht++) { // number of hyper-threads
            int min_offset = 0;
            for (int i = 1; i < available_cores; i++) {
                if (core_info[i] < core_info[min_offset])
                    min_offset = i;
            }
            core_ht_map[c][ht] = min_offset; // processor id
            if (ht == 0 && core_info[min_offset] < 32) cores_per_socket++;
            core_info[min_offset] = 0xFF;
        }
        printf("Core %d = { %d, %d }\n", c, core_ht_map[c][0], core_ht_map[c][1]);
    }
    printf("Total of %d physical cores per socket\n", cores_per_socket);

#ifdef NO_HT_PINNING
    PRINT("HT-pinning is disabled, updating the HT-map\n");
    uint8_t sockets = (available_cores / 2) / cores_per_socket;
    for (uint8_t s = 0; s < sockets; s++) {
        PRINT("Socket %d\n", s);
        uint8_t b = s * cores_per_socket;
        for (uint8_t c = 0; c < cores_per_socket / 2; c++) {
            std::swap(core_ht_map[b + c][1],
                    core_ht_map[b + c + cores_per_socket / 2][0]);
        }
        for (uint8_t c = 0; c < cores_per_socket; c++) {
            PRINT("Core %d = { %d, %d }\n", c,
                    core_ht_map[b + c][0], core_ht_map[b + c][1]);
        }
    }
#endif
}

void coreFinalize() {
    for (int i = 0; i < (available_cores >> 1); i++) {
        while (core_tenants[i] != 0); // wait for all threads to terminate
    }
    assert(pthread_mutex_destroy(&core_tenants_lock) == 0);
}

/**
 * tx_buffer[0] is used to index sync_buffer
 */
static __thread NvMethodCall *sync_buffer;


/**
 * Contains offset of redo-log entries for active transactions
 * tx_buffer[0]: number of active transactions for current thread.
 * tx_buffer[1+]: redo-log offset 
 */
static __thread uint64_t *tx_buffer;

uint64_t logAppend(PersistentObject* object, uint64_t* arg_ptrs) {

    RedoLog* log = object->getLog();
    size_t slot_size = 2 * sizeof(uint64_t); // Hole for commit_id and log magic
    uint64_t key = arg_ptrs[0];
    slot_size += sizeof(key);
    if (slot_size % CACHE_LINE_WIDTH != 0) {
        slot_size += CACHE_LINE_WIDTH - (slot_size % CACHE_LINE_WIDTH);
    }
    auto slot_index = object->GetFreeSlot();
    uint64_t offset;
    if (slot_index.has_value()) {
        offset = log->head + (*slot_index) * slot_size; 
    } else {
        offset = __sync_fetch_and_add(&log->tail, slot_size);        
        assert(offset + slot_size <= log->size);
    }
    char* dst = (char*)log + offset + sizeof(uint64_t); // Hole for commit_id

    pmem_memcpy_nodrain(dst, &LogMagic, sizeof(LogMagic));
    dst += sizeof(uint64_t);
    pmem_memcpy_nodrain(dst, &key, sizeof(key));
    pmem_drain();
    pmem_persist(&log->tail, sizeof(log->tail));
    return offset;
}

void logRemove(PersistentObject* object, uint64_t* arg_ptrs) {
    RedoLog* log = object->getLog();
    uint64_t offset = arg_ptrs[0];

    uint64_t entry_size = 3 * sizeof(uint64_t);
    assert(offset + entry_size <= log->size);
    char* dst = (char*)log + offset + sizeof(uint64_t); // Hole for commit_id
    pmem_memcpy_nodrain(dst, &NoLogMagic, sizeof(NoLogMagic));
    pmem_drain();
    object->AddFreeSlot(offset);
}

void loggerRoutine(PersistentObject* object) {
  uint64_t active_tx_id = 0;

  while (true) {
    // Wait for main thread to setup buffer
    while (sync_buffer[active_tx_id].method_tag == 0) {
      if (tx_buffer[0] == 0) {
        active_tx_id = 0;
        continue;
      }
      if (active_tx_id > tx_buffer[0]-1) active_tx_id--;
      else if (active_tx_id < tx_buffer[0]-1) active_tx_id++;
    }

    // Check for TERM signal from main thread
    if (sync_buffer[active_tx_id].method_tag == UINT64_MAX) {
      printf("Received TERM signal from the main thread\n");
      break;
    }
    PersistentObject* nv_object = (PersistentObject*)sync_buffer[active_tx_id].obj_ptr;
    assert(nv_object == object);
    uint64_t log_offset;
    if (active_tx_id > 0) { // dependant (nested) transaction
        // Currently not supporting 
        printf("Currently not supporting nested transaction for prontoer");
        fflush(stdout);
        exit(0);
    } else { // outer-most transaction
        // Delegate log creation to the logger creation.
        if (sync_buffer[active_tx_id].method_tag == 1) {
            log_offset = logAppend(nv_object, sync_buffer[active_tx_id].arg_ptrs);
        } else if (sync_buffer[active_tx_id].method_tag == 2) {
            logRemove(nv_object, sync_buffer[active_tx_id].arg_ptrs);
        } else {
            printf("Unsupported method_tag: %ld", sync_buffer[active_tx_id].method_tag);
            fflush(stdout);
            exit(0);            
        }
    }
    tx_buffer[active_tx_id + 1] = log_offset;
    sync_buffer[active_tx_id].offset = log_offset;
    // Notify main thread
    asm volatile("mfence" : : : "memory");
    sync_buffer[active_tx_id].method_tag = 0;
    
  }
}

void coreAlloc(int *core_ids) {
    int least_occupied = 0;
    assert(pthread_mutex_lock(&core_tenants_lock) == 0);
    for (int i = 0; i < (available_cores >> 1); i++) {
        if (core_tenants[i] < core_tenants[least_occupied]) {
            least_occupied = i;
        }
    }
    core_tenants[least_occupied]++;
    assert(pthread_mutex_unlock(&core_tenants_lock) == 0);
    core_ids[0] = core_ht_map[least_occupied][0];
    core_ids[1] = core_ht_map[least_occupied][1];
    printf("Adding new tenants to cores %d and %d.\n",
        core_ids[0], core_ids[1]);
}

// Must only be called once by either the logger or main thread
void coreFree(int core_id) {
    assert(core_id >= 0);
    assert(core_id < available_cores);
    assert(pthread_mutex_lock(&core_tenants_lock) == 0);

    uint8_t physical_core_id = UINT8_MAX;
    for (uint8_t c = 0; c < (available_cores >> 1); c++) {
        if (core_id == core_ht_map[c][0] || core_id == core_ht_map[c][1]) {
            physical_core_id = c;
            break;
        }
    }
    assert(physical_core_id != UINT8_MAX);

    core_tenants[physical_core_id]--;
    assert(pthread_mutex_unlock(&core_tenants_lock) == 0);
    printf("Removing tenants from cores %d and %d.\n",
        core_ht_map[physical_core_id][0], core_ht_map[physical_core_id][1]);
}

void initializeWorker(std::function<void()>& start_routine, PersistentObject* obj) {
    // Allocate shared buffer
    NvMethodCall *buffer_sync = (NvMethodCall *)calloc(MAX_ACTIVE_TXS, sizeof(NvMethodCall));
    assert(buffer_sync != NULL);
    memset(buffer_sync, 0, sizeof(NvMethodCall) * MAX_ACTIVE_TXS);

    // Allocate transaction buffer
    uint64_t *buffer_tx = (uint64_t *)calloc(MAX_ACTIVE_TXS + 1, sizeof(uint64_t));
    assert(buffer_tx != NULL);
    memset(buffer_tx, 0, sizeof(uint64_t) * (MAX_ACTIVE_TXS + 1));
    // Get cores which host main and logger threads
    int core_ids[2];
    coreAlloc(core_ids);  
    auto logger_thread = std::thread([&obj, &buffer_sync, &buffer_tx](int core_id){
        sync_buffer = buffer_sync;
        tx_buffer = buffer_tx;
        // Set thread core affinity
        pthread_t thread = pthread_self();
    #ifndef SYNC_SL
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        assert(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
    #endif // SYNC_SL
        loggerRoutine(obj);
        // clean up
        printf("[%d] Logger thread is now terminating\n", (int)thread);
    #ifndef SYNC_SL
        coreFree(core_id);
    #endif // SYNC_SL
    }, core_ids[0]);

    auto main_thread = std::thread([&start_routine, &buffer_sync, &buffer_tx](int core_id) {
        sync_buffer = buffer_sync;
        tx_buffer = buffer_tx;
        // Set thread core affinity
        pthread_t thread = pthread_self();
    #ifndef SYNC_SL
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        assert(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
    #endif // SYNC_SL
        start_routine();
        assert(tx_buffer[0] == 0);  // No active transactions
        sync_buffer[0].method_tag = UINT64_MAX;  // Signals logger thread to terminate
        printf("[%d] Worker thread is now terminating\n", (int)thread);
    }, core_ids[1]);

    logger_thread.join();
    main_thread.join();
}

void LogInsert(uint64_t key, PersistentObject* object) {
  // Add to tx buffer
  // Similar functionality to Savitar_thread_notify
  sync_buffer[tx_buffer[0]].obj_ptr = (uint64_t)object;
  // Add payload to store
  sync_buffer[tx_buffer[0]].arg_ptrs[0] = key;

  // Increment tx buffer store index
  tx_buffer[0]++;
  tx_buffer[tx_buffer[0]] = 0;
#ifndef SYNC_SL
  asm volatile("sfence" : : : "memory");
#endif // SYNC_SL

  sync_buffer[tx_buffer[0]-1].method_tag = 1;
#ifdef SYNC_SL
    Savitar_persister_log(tx_buffer[0] - 1);
    PRINT("[%d] Finished creating synchronous semantic log\n", (int)pthread_self());
#endif // SYNC_SL
}
void logCommit(RedoLog* log, uint64_t offset) {
  uint64_t commit_id = __sync_add_and_fetch(&log->last_commit, 1);
  assert(commit_id < UINT64_MAX);
  uint64_t *ptr = (uint64_t*)((char*)log + offset);
  *ptr = commit_id;
  pmem_persist(ptr, sizeof(commit_id));
  printf("[%d] Marked log entry (%zu) as committed with id = %zu\n",
            (int)pthread_self(), offset, commit_id);
}
uint64_t LogInsertWait(PersistentObject* object, RedoLog* log) {
  // Wait for tx buffer to complete the work
#ifndef SYNC_SL
    // TODO (Abhinav): Optimize spin lock
    while (sync_buffer[tx_buffer[0] - 1].method_tag != 0) { } // spin lock
#endif // SYNC_SL
  assert(tx_buffer[0] > 0);
  uint64_t log_offset = sync_buffer[tx_buffer[0] - 1].offset;
  logCommit(log, tx_buffer[tx_buffer[0]--]);
  return (log_offset-log->head)/SLOT_SIZE;
}
void LogRemove(uint64_t offset, PersistentObject* object) {
  // Add to tx buffer
  // Similar functionality to Savitar_thread_notify
  sync_buffer[tx_buffer[0]].obj_ptr = (uint64_t)object;
  // Add payload to store
  sync_buffer[tx_buffer[0]].arg_ptrs[0] = offset;

  // Increment tx buffer store index
  tx_buffer[0]++;
  tx_buffer[tx_buffer[0]] = 0;
#ifndef SYNC_SL
  asm volatile("sfence" : : : "memory");
#endif // SYNC_SL

  sync_buffer[tx_buffer[0]-1].method_tag = 2;
#ifdef SYNC_SL
    Savitar_persister_log(tx_buffer[0] - 1);
    PRINT("[%d] Finished creating synchronous semantic log\n", (int)pthread_self());
#endif // SYNC_SL
}
void logUncommit(RedoLog* log, uint64_t offset) {
    // Nothing to do since the log magic has already been emptied.
//   uint64_t commit_id = __sync_add_and_fetch(&log->last_commit, 1);
//   assert(commit_id < UINT64_MAX);
//   uint64_t *ptr = (uint64_t*)((char*)log + offset);
//   *ptr = 0;
//   pmem_persist(ptr, sizeof(commit_id));
//   printf("[%d] Marked log entry (%zu) as committed with id = %zu\n",
//             (int)pthread_self(), offset, commit_id);
}
void LogRemoveWait(PersistentObject* object, RedoLog* log) {
    // Wait for the tx buffer to complete the work
#ifndef SYNC_SL
    while (sync_buffer[tx_buffer[0] - 1].method_tag != 0) { 
        // TODO(Abhinav): Asm volatile pause.
    } // spin lock
#endif // SYNC_SL
    assert(tx_buffer[0] > 0);
    uint64_t log_offset = sync_buffer[tx_buffer[0]-1].offset;    
    logUncommit(log, tx_buffer[tx_buffer[0]--]);
}