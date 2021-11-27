#include <map>
#include <uuid/uuid.h>
#include <map>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include "nv_object.h"
// #include "thread.h"
using namespace std;

class Configuration {
public:
  unsigned int threads;
  unsigned int operations;
  size_t valueSize;
  string workloadPrefix;
  bool useSavitar;
};

class Benchmark {
public:
    Benchmark(Configuration *config) {
      assert(instance == NULL);
      instance = this;
      cfg = config;
      memset(&t1, 0, sizeof(t1));
      memset(&t2, 0, sizeof(t2));
      sync = 0;

      // Populate value
      valueBuffer = (char *)malloc(cfg->valueSize);
      for (size_t i = 0; i < cfg->valueSize - 1; i++) {
          valueBuffer[i] = (rand() % 2 == 0 ? 'A' : 'a') + (i % 26);
      }
      valueBuffer[cfg->valueSize - 1] = 0;

      // Populate traces
      config->operations = 0;
      traces =
          (vector<string> **)malloc(sizeof(vector<string> *) * cfg->threads);
      for (unsigned int thread = 0; thread < cfg->threads; thread++) {
          traces[thread] = new vector<string>();
          string workloadPath = cfg->workloadPrefix;
  #ifdef ROCKSDB
          workloadPath += "-run-" + to_string(cfg->threads) + ".";
  #endif
          workloadPath += to_string(thread);

          string temp;
          ifstream infile(workloadPath);
          while (infile >> temp) {
              traces[thread]->push_back(temp);
              config->operations++;
          }
      }

      objects = (uintptr_t *)malloc(sizeof(uintptr_t) * cfg->threads);
    }
    ~Benchmark() {
      free(valueBuffer);
      for (unsigned int thread = 0; thread < cfg->threads; thread++) {
          traces[thread]->clear();
          delete traces[thread];
      }
      free(traces);
      free(objects);
      instance = NULL;
    }
    static Benchmark *singleton() {
        return instance;
    }

    virtual void init(unsigned int) = 0;
    virtual void cleanup(unsigned int) = 0;
    virtual unsigned int worker(unsigned int) = 0;

    void run();
    static void *worker(void *);
    uint64_t getLatency();
    uint64_t getThroughput();
    virtual void printReport();

protected:
    static Benchmark *instance;
    Configuration *cfg;
    struct timespec t1, t2;
    uint64_t sync;
    char *valueBuffer;
    vector<string> **traces;
    uintptr_t *objects;
};

class PersistentOrderedMap : PersistentObject {
public:
  typedef char* T;
  typedef map<T, T, less<T>> MapType;
  PersistentOrderedMap(uuid_t id) {
    v_map = new MapType;
  }
  PersistentOrderedMap() {}
  void insert(T key, T value) {
    // TODO: add logging thread
    v_map->insert(make_pair(key, value));
    // TODO: wait for logging thread to complete
  }
  void erase(T key) {
    v_map->erase(key);
  }

  static PersistentObject *BaseFactory(uuid_t id) {
      // ObjectAlloc *alloc = GlobalAlloc::getInstance()->newAllocator(id);
      // void *temp = alloc->alloc(sizeof(PersistentOrderedMap));
      // PersistentOrderedMap *obj = (PersistentOrderedMap *)temp;
      PersistentOrderedMap *object = new PersistentOrderedMap(id);
      return object;
  }
  static PersistentOrderedMap *Factory(uuid_t id) {
    // NVManager &manager = NVManager::getInstance();
    // manager.lock();
    PersistentOrderedMap *obj = nullptr;
    // PersistentOrderedMap *obj =
    //     (PersistentOrderedMap *)manager.findRecovered(id);
    if (obj == nullptr) {
        obj = static_cast<PersistentOrderedMap *>(BaseFactory(id));
        // manager.createNew(classID(), obj);
    }
    // manager.unlock();
    return obj;
  }

  uint64_t Log(uint64_t tag, uint64_t *args) {
      int vector_size = 0;
      ArgVector vector[4]; // Max arguments of the class

      switch (tag) {
          case InsertTag:
              {
              vector[0].addr = &tag;
              vector[0].len = sizeof(tag);
              vector[1].addr = (void *)args[0];
              vector[1].len = strlen((char *)args[0]) + 1;
              vector[2].addr = (void *)args[1];
              vector[2].len = strlen((char *)args[1]) + 1;
              vector_size = 3;
              }
              break;
          case EraseTag:
              {}
              break;
          default:
              assert(false);
              break;
      }

      return AppendLog(vector, vector_size);
  }

  size_t Play(uint64_t tag, uint64_t *args, bool dry) {
      size_t bytes_processed = 0;
      switch (tag) {
          case InsertTag:
              {
              char *key = (char *)args;
              char *value = (char *)args + strlen(key) + 1;
              if (!dry) insert(key, value);
              bytes_processed = strlen(key) + strlen(value) + 2;
              }
              break;
          case EraseTag:
              {}
              break;
          default:
              {
              // PRINT("Unknown tag: %zu\n", tag);
              assert(false);
              }
              break;
      }
      return bytes_processed;
  }

  static uint64_t classID() { return 2; }

private:
  MapType* v_map;
  enum MethodTags {
      InsertTag = 1,
      EraseTag = 2,
  };
};

class NvOrderedMap : public Benchmark {
public:
    NvOrderedMap(Configuration *cfg) : Benchmark(cfg) {  }

    virtual void init(unsigned int myID) {
        uuid_t uuid;
        uuid_generate(uuid);
        PersistentOrderedMap *m = PersistentOrderedMap::Factory(uuid);
        objects[myID] = (uintptr_t)m;
    }

    virtual void cleanup(unsigned int myID) {
        // Nothing to do
    }

    virtual unsigned int worker(unsigned int myID) {
        PersistentOrderedMap *m = (PersistentOrderedMap *)objects[myID];

        unsigned int i = 0;
        for (i = 0; i < traces[myID]->size(); i++) {

            // Create buffer for the value
            // char *value = (char *)alloc->alloc(cfg->valueSize);
            char* value = new char[cfg->valueSize];
            memcpy(value, valueBuffer, cfg->valueSize);

            // Create buffer for the key
            char *key = new char[traces[myID]->at(i).length()];
            strcpy(key, traces[myID]->at(i).c_str());

            m->insert(key, value);
        }
        return i;
    }

    void printReport() {
        cout << "persistent-ordered-map,";
        Benchmark::printReport();
    }
};

int main() {
  printf("Hello, pronto!\n");
  unsigned char id[3] = "2";
  PersistentOrderedMap* pom = PersistentOrderedMap::Factory(id);
  return 0;
};
