#define DEBUG
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
#include <functional>

#include "worker.h"
#include "nv_log.h"
#include "nv_object.h"
#include "persistent_ordered_map.h"
using namespace std;

enum Op { Insert, Remove, Get };
struct Workload {
  typedef uint64_t KeyType;
  typedef string ValueType;
  typedef uint32_t ValueSizeType;
  typedef pair<Op, pair<KeyType, ValueType>> EntryType;
  private:
    vector<EntryType> ops;
    size_t i;
  public:
    Workload(): i(0) {}
    void addOperation(Op op, KeyType key, ValueType value) {
      ops.push_back({op, {key, value}});
    }
    void start() {
      i = 0;
    }
    bool hasNext() {
      return i < ops.size(); 
    }
    EntryType next() {
      return ops[i++];
    }
};

void workerFunction(PersistentOrderedMap* obj, Workload workload) {
  function<void()> start_routine = [&obj, &workload](){
    workload.start();
    while (workload.hasNext()) {
      auto op = workload.next();
      uint64_t key = op.second.first;
      const string& value = op.second.second;
      switch (op.first)
      {
      case Insert:
        {
          obj->insert(key, value.c_str(), value.size()+1); // Include ending null char in value len.
          std::cout << "Inserted: " << key << "\n";
          break;
        }
      case Remove:
        {
          obj->erase(key);
          std::cout << "Removed: " << key << "\n";
          break;
        }
      case Get:
        {
          auto ret = obj->get(key);
          if (ret != nullopt) {
            cout << "Got: " << ret->first << "\n";  
          } else {
            cout << "Did not find key\n";
          }
          break;
        }
      default:
        /* op not found */
        break;
      }
    }
  };
  initializeWorker(start_routine, (PersistentObject*)obj);
}



int main(int argc, char* argv[]) {
  coreInit();
  cout << "savitar core finalize" << endl;
  uuid_t pom_id = "persistent_map";
  PersistentOrderedMap* pom = PersistentOrderedMap::Factory(pom_id, 100);
  int num_workers = 2;
  vector<thread> workers;
  Workload work[num_workers];
  // Generate workloads for all threads.
  for (int i = 0; i < num_workers; i++) {
    work[i].addOperation(Get, 42, "");
    work[i].addOperation(Insert, 42, "DATA1");
    work[i].addOperation(Get, 42, "");
    work[i].addOperation(Remove, 42, "");
    work[i].addOperation(Get, 42, "");
    work[i].addOperation(Insert, 42, "DATA2");
    work[i].addOperation(Get, 42, "");
  }

  // Run workload on the different threads with
  // the common data structure `pom`.
  for (int i = 0; i < num_workers-1; i++) {
    workers.push_back(thread(workerFunction, pom, work[i]));
  }
  workerFunction(pom, work[num_workers-1]);

  for (auto& worker: workers) {
    worker.join();
  }
  coreFinalize();
}