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
  typedef uint64_t SlotIndexType;
  typedef map<KeyType, SlotIndexType> SetType;
  void insert(KeyType key) {
    if (get(key).has_value()) return; // Read before to check if key already logged.
    LogInsert(key, this);
    std::unique_lock lock(mutex_);
    auto it =  v_set.insert({key, 0}).first;
    uint64_t slot_index = LogInsertWait(this, log);
    it->second = slot_index;
    // lock released
  }
  optional<KeyType> get(KeyType key) {
    std::shared_lock lock(mutex_);
    auto it = v_set.find(key);
    if (it == v_set.end()) return nullopt;
    else return it->first;
  }
  optional<SlotIndexType> getSlotIndex(KeyType key) {
    std::shared_lock lock(mutex_);
    auto it = v_set.find(key);
    if (it == v_set.end()) return nullopt;
    else return it->second;
  }
  void erase(KeyType key) {
    auto slot_index = getSlotIndex(key);
    if (!slot_index.has_value()) return;
    LogRemove(*slot_index, this);
    std::unique_lock lock(mutex_);
    v_set.erase(key);
    LogRemoveWait(this, log);
    // lock released
  }


  static PersistentOrderedSet *Factory(uuid_t id) {
    PersistentOrderedSet *obj = nullptr;
    if (obj == nullptr) {
        obj = static_cast<PersistentOrderedSet *>(BaseFactory(id));
    }
    obj->Recover();
    return obj;
  }

  uint64_t Log(uint64_t tag, uint64_t *args) {
      printf("Currently not implemented in prontoer");
      assert(false);
  }
  size_t Play(uint64_t tag, uint64_t *args, SlotIndexType slot_index, bool dry) override {
      size_t bytes_processed = 0;
      switch (tag) {
          case InsertTag:
              {
                KeyType key = *((KeyType*)args);
                if (!dry) {
                  volatile_insert(key, slot_index);
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
  PersistentOrderedSet(uuid_t id) : PersistentObject(id) {}
  static PersistentObject *BaseFactory(uuid_t id) {
      PersistentOrderedSet *object = new PersistentOrderedSet(id);
      return object;
  }
  void volatile_insert(KeyType key, SlotIndexType slot_index) {
    std::unique_lock lock(mutex_);
    v_set.insert({key, slot_index});
  }
  void volatile_remove(KeyType key) {
    std::unique_lock lock(mutex_);
    v_set.erase(key);
  }
  std::shared_mutex mutex_;
  SetType v_set;
  enum MethodTags {
      InsertTag = 1,
      EraseTag = 2,
  };
};