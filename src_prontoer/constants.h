#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#define MAX_THREADS                 64
#define CACHE_LINE_WIDTH            64
#define BUFFER_SIZE                 8
#define MAX_CORES                   40
#define MAX_ACTIVE_TXS              15
#define CATALOG_FILE_NAME           "savitar.cat"
#define CATALOG_FILE_SIZE           ((size_t)8 << 20) // 8 MB
#define CATALOG_HEADER_SIZE         ((size_t)2 << 20) // 2 MB
#define PMEM_PATH                   "/home/Abhinav/data"
#ifndef LOG_SIZE
#define LOG_SIZE                    ((off_t)1 << 30) // 1 GB
#endif
#define NESTED_TX_TAG               0x8000000000000000
#define REDO_LOG_MAGIC              0x5265646F4C6F6745 // RedoLogE

#ifdef DEBUG
#define PRINT(format, ...)          fprintf(stdout, format, ## __VA_ARGS__)
#else
#define PRINT(format, ...)          {}
#endif