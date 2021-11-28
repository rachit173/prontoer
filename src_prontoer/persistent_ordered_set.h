#pragma once
#include <map>
#include <set>
#include <uuid/uuid.h>
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
#include <execinfo.h>
#include <signal.h>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include "nv_object.h"
#include "worker.h"

using namespace std;

class PersistentOrderedSet : PersistentObject {
public:
  typedef uint64_t T;
  typedef set<T, less<T>> SetType;
  PersistentOrderedSet(uuid_t id) : PersistentObject(id) {}
  void insert(T key) {
    LogInsert(key, this);
    std::unique_lock lock(mutex_);
    v_set.insert(key);
    LogInsertWait(this, log);
    // lock released
  }
  optional<T> get(T key) {
    std::shared_lock lock(mutex_);
    auto it = v_set.find(key);
    if (it == v_set.end()) return nullopt;
    else return *it;
  }
  void erase(T key) {
    uint64_t offset = 0;
    LogRemove(offset, this);
    std::unique_lock lock(mutex_);
    v_set.erase(key);
    LogRemoveWait(this, log);
    // lock released
  }

  static PersistentObject *BaseFactory(uuid_t id) {
      // ObjectAlloc *alloc = GlobalAlloc::getInstance()->newAllocator(id);
      // void *temp = alloc->alloc(sizeof(PersistentOrderedMap));
      // PersistentOrderedMap *obj = (PersistentOrderedMap *)temp;
      PersistentOrderedSet *object = new PersistentOrderedSet(id);
      return object;
  }
  static PersistentOrderedSet *Factory(uuid_t id) {
    // NVManager &manager = NVManager::getInstance();
    // manager.lock();
    PersistentOrderedSet *obj = nullptr;
    // PersistentOrderedMap *obj =
    //     (PersistentOrderedMap *)manager.findRecovered(id);
    if (obj == nullptr) {
        obj = static_cast<PersistentOrderedSet *>(BaseFactory(id));
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
              {
                vector[0].addr = &tag;
                vector[0].len = sizeof(tag);
                vector[1].addr = (void*)args[0];
                vector[1].len = strlen((char*)args[0])+1;
                vector_size = 2;
              }
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
                T key = *((T*)args);
                if (!dry) insert(key);
                bytes_processed = sizeof(T);
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
  std::shared_mutex mutex_;
  SetType v_set;
  enum MethodTags {
      InsertTag = 1,
      EraseTag = 2,
  };
};