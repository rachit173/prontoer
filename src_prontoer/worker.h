#pragma once

#include <pthread.h>
#include <stdarg.h>
#include "constants.h"
#include "nv_log.h"
#include "nv_object.h"
#include "persistent_ordered_set.h"

#include <functional>
#include <vector>

void coreInit();
void coreFinalize();
void initializeWorker(function<void()>& start_routine, PersistentObject* obj);