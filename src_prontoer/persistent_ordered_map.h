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
#include <stdint.h>
#include <stdio.h>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <cassert>

#include "nv_object.h"
#include "worker.h"

using namespace std;

class PersistentOrderedMap : PersistentObject {
public:
  typedef uint64_t KeyType;
  typedef const char* ValueType;
  typedef uint32_t ValueSizeType;
  typedef uint64_t SlotOffsetType;
  typedef map<KeyType, SlotOffsetType> MapType;
  void insert(KeyType key, ValueType value, uint32_t value_size) {
    assert(value_size <= max_value_size_);
    // At this program point v_set may or may not contain the key.
    // Thus, in the following code, a new insert is started
    // but if the v_set (volatile data structure) 
    // already contains the key and a offset,
    // the current log insertion is aborted.
    // For key value data structure, instead of uncommitting the
    // current slot, the previous slot should be uncommitted.
    // This kind of contention occurs when multiple threads
    // perform insert on the same key and the key is absent 
    // in v_set. 
    // In a real workload, this type of contention is expected to be 
    // infrequent, and LogRemove should be called in few cases.
    {
      LogInsertWithPayload(key, value, value_size, this);
      std::unique_lock lock(mutex_);
      auto out = v_map_.try_emplace(key, 0);
      uint64_t slot_offset = LogInsertWait(this, log);
      // TODO(rrt): Study the frequency of code paths taken in actual workloads. 
      if (out.second) {
        // kv pair with previously unused key.
        out.first->second = slot_offset;
      } else {
        // TODO(rrt): Investigate if LogRemove or LogRemove2 is a better alternative.
        uint64_t prev_slot_offset = out.first->second;
        LogRemove2(prev_slot_offset, this);
        out.first->second = slot_offset;
        LogRemoveWait2(this, log);
      }
      // lock released
    }
  }

  optional<std::pair<ValueType, size_t>> get(KeyType key) const {
    std::shared_lock lock(mutex_);
    auto it = v_map_.find(key);
    if (it == v_map_.end()) return nullopt;
    else {
      uint64_t offset =  it->second;
      ValueType val = (char*)log + offset + 3*sizeof(uint64_t);
      size_t len = *(size_t*)((char*)log + offset);
      return std::make_pair(val, len);
    }
  }

  void erase(KeyType key) {
    uint64_t slot_offset;
    {
      std::shared_lock lock(mutex_);
      auto it = v_map_.find(key);
      if (it == v_map_.end()) return;
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
      v_map_.erase(key);
      LogRemoveWait(this, log);
      // lock released
    }
  }


  static PersistentOrderedMap *Factory(uuid_t id, size_t max_value_size) {
    PersistentOrderedMap *obj = nullptr;
    if (obj == nullptr) {
        size_t slot_size = 3*sizeof(uint64_t); // ValueLen (8B), Magic (8B), Key (8B).
        slot_size += max_value_size;
        if (slot_size % CACHE_LINE_WIDTH != 0) {
          slot_size += (CACHE_LINE_WIDTH - slot_size%CACHE_LINE_WIDTH);
        }
        obj = new PersistentOrderedMap(id, max_value_size, slot_size);
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
          case RemoveTag:
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
  PersistentOrderedMap(uuid_t id, size_t max_value_size, size_t slot_size) : PersistentObject(id, slot_size) {
    max_value_size_ = max_value_size;
  }
  void volatile_insert(KeyType key, SlotOffsetType slot_index) {
    std::unique_lock lock(mutex_);
    v_map_.insert({key, {slot_index}});
  }
  void volatile_remove(KeyType key) {
    std::unique_lock lock(mutex_);
    v_map_.erase(key);
  }
  mutable std::shared_mutex mutex_;
  MapType v_map_;
  uint32_t max_value_size_;
  enum MethodTags {
      InsertTag = 1,
      RemoveTag = 2,
      Remove2Tag = 3,
      InsertWithPayloadTag = 4,
  };
};