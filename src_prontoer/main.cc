#define DEBUG
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
#include <optional>

#include "thread.h"
#include "nv_log.h"
#include "nv_object.h"
using namespace std;

class PersistentOrderedMap : PersistentObject {
public:
  typedef char* T;
  typedef map<T, T, less<T>> MapType;
  PersistentOrderedMap(uuid_t id) : PersistentObject(id) {
    v_map = new MapType;
  }
  PersistentOrderedMap() : PersistentObject(true) {}
  void insert(T key, T value) {
    // TODO: add logging thread
    v_map->insert(make_pair(key, value));
    // TODO: wait for logging thread to complete
  }
  optional<T> get(T key) {
    auto it = v_map->find(key);
    if (it == v_map->end()) return nullopt;
    else return it->second;
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

int main(int argc, char* argv[]) {
  Savitar_core_init();
  std::cout << "savitar core finalize" << std::endl;
  uuid_t id = "axaxaxaxaxaxaxa";
  SavitarLog* log;
  if (Savitar_log_exists(id)) {
    log = Savitar_log_open(id);
  } else {
    log = Savitar_log_create(id, LOG_SIZE);
  }
  uuid_t pom_id = "persistent_map";
  PersistentOrderedMap* pom = PersistentOrderedMap::Factory(pom_id);
  char key[] = "abcd";
  char value[] = "xyzw";
  pom->insert(key, value);
  auto val = pom->get(key);
  if (val.has_value()) {
    std::cout << *val << std::endl;
  }

  Savitar_core_finalize();
  return 0;
}