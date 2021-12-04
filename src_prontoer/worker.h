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
// (LogRemove and LogRemoveWait), 
// (LogInsert and LogInsertWait), 
// (LogRemove2 and LogRemoveWait2),
// are called on the same thread.
void LogInsert(uint64_t key, PersistentObject* object);
uint64_t LogInsertWait(PersistentObject* object, RedoLog* log);
void LogRemove(uint64_t slot_offset, PersistentObject* obj);
void LogRemoveWait(PersistentObject* object, RedoLog* log);
void LogRemove2(uint64_t slot_offset, PersistentObject* obj);
void LogRemoveWait2(PersistentObject* object, RedoLog* log);