#define DEBUG
#include <iostream>
#include <vector>
#include <uuid/uuid.h>

#include "thread.h"
#include "nv_log.h"


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

  Savitar_core_finalize();
  return 0;
}