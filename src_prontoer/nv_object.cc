#include <stdint.h>
#include <stdio.h>
#include <cstring>
#include <queue>
#include "nv_object.h"
#include "nv_log.h"

/*
 * Constructor is only called for new objects:
 * * The first time a persistent object is constructed
 * * Every time a persistent object is solely constructed from logs
 * * Constructor is never called when snapshots are used
 */
PersistentObject::PersistentObject(uuid_t id, size_t slot_size): slot_size(slot_size) {
    constructor(id);
}

void PersistentObject::constructor(uuid_t id) {
    if (id == NULL) {
        uuid_t tid;
        uuid_generate(tid);
        id = tid;
    }
    // alloc = GlobalAlloc::getInstance()->findAllocator(id);
    // assert(alloc != NULL);
    uuid_copy(uuid, id);
    uuid_unparse(id, uuid_str);

    if (Savitar_log_exists(uuid)) {
        log = Savitar_log_open(uuid);
    }
    else {
        log = Savitar_log_create(uuid, LOG_SIZE);
    }
}

PersistentObject::~PersistentObject() {
    if (log == NULL) return; // handle dummy objects
    // TODO handle re-assignment of objects (remap semantic log)
    Savitar_log_close(log);
}

/*
 * [General rules]
 * NVM manager is responsible for recovering all persistent objects through calling their Recover()
 * method at startup. Each object is assigned to a recy thread. Look for the constructor method of
 * NVManager for more details.over
 * Also, all allocations are handled by the NVM manager object, which either finds the object or creates
 * an new persistent object using the object's factory method.
 * ----------------------------------------------------------------------------------------------------
 * [Single object recovery]
 * If there are no dependencies (nested transactions), the object will go through the log and plays
 * log entries one by one based on the commit order.
 * ----------------------------------------------------------------------------------------------------
 * [Normal recovery]
 * In presence of dependant objects, here is how the synchronization between persistent objects works to
 * ensure the right replay order for log entries:
 * > Parent: there is no change in the recovery code except the code snippet which child objects run to
 *   make sure the method on child object is called (and executed) at the right order with respect to
 *   other operations. In other words, the child object will stall the calls until it's ready.
 * > Child (non-parent): once a child object reads a log entry that belongs to a nested transaction,
 *   it will wait for the parent object to pass the point specified in the nested transaction log entry.
 *   For example, if the parent log shows commit order 12, the child object waits for the parent to finish
 *   executing the corresponding log entry and update 'last_played_commit_order' to 12.
 * ----------------------------------------------------------------------------------------------------
 * [Partial commits]
 * These are committed log entries for nested transactions where the system fails before marking the
 * outer-most transaction as committed. For example, assume A calls B and the program terminates after
 * B's log entry is marked as committed but before A's entry is marked as committed. In this situation,
 * B must avoid waiting for A and should end the recovery process.
 * If there are other objects trying to play a log entry that involves the child or parent object, the
 * recovery process for those objects should stop as well.
 * If an unclean shutdown is detected, the NVM manager will initiate a process to fix the log so that
 * uncommitted transactions are not played.
 * ----------------------------------------------------------------------------------------------------
 */
void PersistentObject::Recover() {
    assert(log != nullptr);
    assert(sizeof(uint64_t) == 8); // We assume 2 * sizeof(uint64_t) == 16
    const uint64_t log_head = log->head;
    char* ptr = (char*)log + log_head;
    const char* limit = (char*)log + log->tail; 

    char uuid_str[64], uuid_prefix[9];
    uuid_unparse(uuid, uuid_str);
    memcpy(uuid_prefix, uuid_str, 8);
    uuid_prefix[8] = '\0';
    printf("[%s] Started recoverying %s\n", uuid_prefix, uuid_str);
    printf("[%s] Log head: %zu\n", uuid_prefix, log->head);
    printf("[%s] New head: %zu\n", uuid_prefix, log_head);
    printf("[%s] Log tail: %zu\n", uuid_prefix, log->tail);

    const uint64_t magic_offset = sizeof(uint64_t);
    const uint64_t data_offset = magic_offset + sizeof(uint64_t);
    // Creating data-structures to handle out-of-order entries
    uint64_t slot_offset = log_head;
    while (ptr < limit) {
        // 1. Read commit id and method tag from persistent log
        uint64_t commit_id = *((uint64_t *)ptr);
        uint64_t magic = *((uint64_t *)(ptr + magic_offset));
        if (magic != REDO_LOG_MAGIC) { // free slot
            AddFreeSlot(slot_offset);
        } else { // filled slot.
            uint64_t* args = ((uint64_t*)(ptr + data_offset));
            uint64_t op_tag = 1; // Insert operation
            this->Play(op_tag, args, slot_offset, false);            
        }
        ptr += slot_size;
        slot_offset += slot_size;
    }
    printf("[%s] Finished recovering %s\n", uuid_prefix, uuid_str);
}
