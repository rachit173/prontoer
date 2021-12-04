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
  typedef uint64_t KeyType;
  typedef uint64_t SlotOffsetType;
  typedef map<KeyType, SlotOffsetType> SetType;
  void insert(KeyType key) {
    // The following presence check may not be required for certain workloads.
    {
      std::shared_lock lock(mutex_);
      if (v_set.find(key) != v_set.end()) return; // Read before to check if key already logged.
    }
    // At this program point v_set may or may not contain the key.
    // Thus, in the following code, a new insert is started
    // but if the v_set (volatile data structure) 
    // already contains the key and a offset,
    // the current log insertion is aborted.
    // For key value data structure, instead of uncommitting the
    // current slot, the previous slot should be uncommitted.
    // i.e.
     /**
      * map<KeyType, pair<SlotOffsetType, ValueType>> MapType;
      if (out.second) {
        // kv pair with previously unused key.        
        out.first->second = {slot_offset, Value};
      } else {
        // kv pair with same key is already present.
        uint64_t prev_slot_offset = out.first->second.first;
        LogRemove2(prev_slot_offset, this);
        out.first->second = {slot_offset, Value};
        LogRemoveWait2(this, log);
      }
      
     */
    // This kind of contention occurs when multiple threads
    // perform insert on the same key and the key is absent 
    // in v_set. 
    // In a real workload, this type of contention is expected to be 
    // infrequent, and LogRemove should be called in few cases.
    {
      LogInsert(key, this);
      std::unique_lock lock(mutex_);
      // auto out =  v_set.insert({key, 0});
      auto out = v_set.try_emplace(key, 0);
      uint64_t slot_offset = LogInsertWait(this, log);
      // TODO(rrt): Study the frequency of code paths taken in actual workloads. 
      if (out.second) {
        out.first->second = slot_offset;
      } else {
        // TODO(rrt): Investigate if LogRemove or LogRemove2 is a better alternative.
        LogRemove2(slot_offset, this);
        LogRemoveWait2(this, log);
      }
      // lock released
    }
  }
  optional<KeyType> get(KeyType key) const {
    std::shared_lock lock(mutex_);
    auto it = v_set.find(key);
    if (it == v_set.end()) return nullopt;
    else return it->first;
  }

  void erase(KeyType key) {
    uint64_t slot_offset;
    {
      std::shared_lock lock(mutex_);
      auto it = v_set.find(key);
      if (it == v_set.end()) return;
      slot_offset = it->second;
    }
    // Multiple threads erasing
    // the same key which is present in v_set
    // will invalidate the same slot which
    // is not a correctness issue.
    {
      // TODO(rrt): Check if Remove2, RemoveWait2 perform better.
      LogRemove(slot_offset, this);
      std::unique_lock lock(mutex_);
      v_set.erase(key);
      LogRemoveWait(this, log);
      // lock released
    }
  }


  static PersistentOrderedSet *Factory(uuid_t id) {
    PersistentOrderedSet *obj = nullptr;
    if (obj == nullptr) {
      size_t slot_size = 64;
      obj = new PersistentOrderedSet(id, slot_size);
    }
    obj->Recover();
    return obj;
  }

  uint64_t Log(uint64_t tag, uint64_t *args) {
      printf("Currently not implemented in prontoer");
      assert(false);
  }
  size_t Play(uint64_t tag, uint64_t *args, SlotOffsetType slot_offset, bool dry) override {
      size_t bytes_processed = 0;
      switch (tag) {
          case InsertTag:
              {
                KeyType key = *((KeyType*)args);
                if (!dry) {
                  volatile_insert(key, slot_offset);
                }
                bytes_processed = sizeof(KeyType);
              }
              break;
          case EraseTag:
              {
                printf("EraseTag will not be used for recovery in Prontoer");
                assert(false); // EraseTag should not be present in protoer.
              }
              break;
          default:
              {
                printf("Unknown tag: %zu\n", tag);
                assert(false);
              }
              break;
      }
      return bytes_processed;
  }

  static uint64_t classID() { return 2; }

private:
  PersistentOrderedSet(uuid_t id, size_t slot_size) : PersistentObject(id, slot_size) {}
  void volatile_insert(KeyType key, SlotOffsetType slot_index) {
    std::unique_lock lock(mutex_);
    v_set.insert({key, slot_index});
  }
  void volatile_remove(KeyType key) {
    std::unique_lock lock(mutex_);
    v_set.erase(key);
  }
  mutable std::shared_mutex mutex_;
  SetType v_set;
  enum MethodTags {
      InsertTag = 1,
      EraseTag = 2,
  };
};