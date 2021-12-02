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

void workerFunction(PersistentOrderedSet* obj) {
  function<void()> start_routine = [&obj](){
    // TODO: replace with benchmark code.
    obj->insert(42);
    auto ret = obj->get(42); 
    if (ret != nullopt) {
      cout << "Got: " << *ret << endl; 
    } else {
      cout << "Did not find key" << endl;
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
  for (int i = 0; i < num_workers-1; i++) {
    workers.push_back(thread(workerFunction, pom));
  }
  workerFunction(pom);

  for (auto& worker: workers) {
    worker.join();
  }
  coreFinalize();
}