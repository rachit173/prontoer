#pragma once

#include <uuid/uuid.h>
#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <optional>
#include <mutex>

#include "nv_log.h"
#include "constants.h"

/*
 * Objects demanding transactional durability must extend this class and
 * implement abstract methods. The state of the object is recovered using
 * its 'uuid', provided to the object constructor.
 */
class PersistentObject {
    public:
        PersistentObject(uuid_t id, size_t slot_size);
        ~PersistentObject();

        /*
         * Called by persister threads upon processing sync_buffers
         * Returns the offset of log entry
         */
        virtual uint64_t Log(uint64_t, uint64_t *) = 0;

        // inline uint64_t AppendLog(ArgVector *vector, int v_size) {
        //     return Savitar_log_append(this->log, vector, v_size);
        // }

        inline unsigned char *getUUID() const {
            return (unsigned char *)uuid;
        }

        // TODO support for permanent deletes
        void operator delete (void *ptr) {
            PersistentObject *obj = (PersistentObject *)ptr;
            if (obj->log == NULL) return;
            fprintf(stdout, "Deleting persistent object: %s\n",
                    obj->uuid_str);
        }

    private:
        void constructor(uuid_t id);

    public:
        RedoLog* getLog() { return log; }
        void AddFreeSlot(uint64_t slot_offset) {
            // TODO(rrt): Need to lock when adding free slot.
            std::lock_guard<std::mutex> guard(free_slots_mutex_);
            free_slots.push(slot_offset);
        }
        std::optional<uint64_t> GetFreeSlot() {
            std::lock_guard<std::mutex> guard(free_slots_mutex_);
            if (free_slots.empty()) return std::nullopt;
            uint64_t slot = free_slots.front();
            free_slots.pop();
            return slot;
        }

    protected:
        // Called by concrete persistent data structure
        // in the factory during the recovery process.
        void Recover();

        // Called by Recover.
        virtual size_t Play(uint64_t tag, uint64_t *args, uint64_t slot_index, bool dry) = 0;

        struct { // 16 + 38 + 1 + 1 + 8 = 64 bytes
            uuid_t uuid;
            char uuid_str[38];
            // object is assigned to a live variable?
            uint8_t assigned = 0;
            uint8_t recovering = 0;

            RedoLog *log = NULL;
        };
        // persistent state specific members.
        // TODO(rrt): Free slots storage and retrieval
        std::queue<uint64_t> free_slots;
        std::mutex free_slots_mutex_;
    public:
        // Size of a key slot on persistent log
        const size_t slot_size;
        // commit id of the last played log entry
        // TODO(rrt): Remove the last_played_commit_id
        // uint64_t last_played_commit_id;

        friend class Snapshot;
};
