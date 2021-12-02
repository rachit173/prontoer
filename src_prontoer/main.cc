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
#include "persistent_ordered_set.h"
using namespace std;

enum Op { Insert, Remove, Get };
struct Workload {
  typedef uint64_t KeyType;
  private:
    vector<pair<Op, KeyType>> ops;
    size_t i;
  public:
    Workload(): i(0) {}
    void addOperation(Op op, KeyType key) {
      ops.push_back({op, key});
    }
    void start() {
      i = 0;
    }
    bool hasNext() {
      return i < ops.size(); 
    }
    pair<Op, KeyType> next() {
      return ops[i++];
    }
};

void workerFunction(PersistentOrderedSet* obj, Workload workload) {
  function<void()> start_routine = [&obj, &workload](){
    workload.start();
    while (workload.hasNext()) {
      auto op = workload.next();
      auto key = op.second;
      switch (op.first)
      {
      case Insert:
        obj->insert(key);
        break;
      case Remove:
        obj->erase(key);
        break;
      case Get:
        {
          auto ret = obj->get(key);
          if (ret != nullopt) {
            cout << "Got: " << *ret << "\n";  
          } else {
            cout << "Did not find key\n";
          }
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
  PersistentOrderedSet* pom = PersistentOrderedSet::Factory(pom_id);
  int num_workers = 2;
  vector<thread> workers;
  Workload work[num_workers];
  // Generate workloads for all threads.
  for (int i = 0; i < num_workers; i++) {
    work[i].addOperation(Insert, 42);
    work[i].addOperation(Get, 42);
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