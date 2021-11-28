#pragma once

#include <pthread.h>
#include <stdarg.h>
#include "constants.h"
#include "nv_log.h"
#include "nv_object.h"
#include "persistent_ordered_set.h"
#include <functional>


void Savitar_core_init();
void Savitar_core_finalize();

typedef int (*MainFunction)(int, char **);

int Savitar_main(MainFunction, int, char **);

int Savitar_thread_create(pthread_t *, const pthread_attr_t *,
    void *(*start_routine)(void *), void *);

/*
 * The main thread communicates with the logger thread through
 * the thread_notify function. Here is the list and order of
 * arguments:
 * thread_notify(logger_func, log, arg_1, ..., arg_n)
 */
void Savitar_thread_notify(int, ...);

/*
 * The main thread waits for the logger thread to finish logging
 * through calling this function.
 */
void Savitar_thread_wait(PersistentObject *, SavitarLog *log);
