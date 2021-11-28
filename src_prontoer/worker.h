#pragma once

#include <pthread.h>
#include <stdarg.h>
#include "constants.h"
#include "nv_log.h"
#include "nv_object.h"

#include <functional>
#include <vector>

void coreInit();
void coreFinalize();
void initializeWorker(std::function<void()>& start_routine, PersistentObject* object);
void LogInsert(uint64_t key, PersistentObject* object);
uint64_t LogInsertWait(PersistentObject* object, RedoLog* log);
void LogRemove(uint64_t offset, PersistentObject* log);
void LogRemoveWait(PersistentObject* object, RedoLog* log);