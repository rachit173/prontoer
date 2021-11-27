#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <functional>
#include <thread>

#include "constants.h"
#include "nv_object.h"


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

void loggerRoutine(PersistentObject* nv_object) {
  uint64_t active_tx_id = 0;
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
    // NvMethodCall *buffer = (NvMethodCall *)calloc(MAX_ACTIVE_TXS, sizeof(NvMethodCall));
    // assert(buffer != NULL);
    // memset(buffer, 0, sizeof(NvMethodCall) * MAX_ACTIVE_TXS);

    // // Allocate transaction buffer
    // uint64_t *tx_buffer = (uint64_t *)calloc(MAX_ACTIVE_TXS + 1, sizeof(uint64_t));
    // assert(tx_buffer != NULL);
    // memset(tx_buffer, 0, sizeof(uint64_t) * (MAX_ACTIVE_TXS + 1));
    // Get cores which host main and logger threads
    int core_ids[2];
    coreAlloc(core_ids);  
    auto logger_thread = std::thread([&](int core_id){
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

    auto main_thread = std::thread([&start_routine](int core_id) {
        // Set thread core affinity
        pthread_t thread = pthread_self();
    #ifndef SYNC_SL
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        assert(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
    #endif // SYNC_SL
        start_routine();
        printf("[%d] Worker thread is now terminating\n", (int)thread);
    }, core_ids[1]);

    logger_thread.join();
    main_thread.join();
}