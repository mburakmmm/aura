#ifndef AURA_MOBILE_EVENT_QUEUE_H
#define AURA_MOBILE_EVENT_QUEUE_H

#include "events.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define AURA_EVENT_QUEUE_DEFAULT_CAPACITY 1024u

enum {
    AURA_QUEUE_PUSH_DROPPED = 0,
    AURA_QUEUE_PUSH_OK = 1,
    AURA_QUEUE_PUSH_COALESCED = 2,
    AURA_QUEUE_PUSH_INVALID = -1
};

typedef struct AuraEventQueue {
    pthread_mutex_t mutex;
    AuraPlatformEvent *pending;
    AuraPlatformEvent *snapshot;
    size_t capacity;
    size_t pending_count;
    size_t snapshot_count;
    size_t high_water_mark;
    uint64_t overflow_count;
    uint64_t dropped_count;
    uint64_t dropped_since_snapshot;
    uint64_t coalesced_count;
    uint64_t texts_allocated;
    uint64_t texts_freed;
    uint64_t last_sequence;
    uint64_t last_timestamp_ns;
    int initialized;
} AuraEventQueue;

int aura_event_queue_init(AuraEventQueue *queue, size_t capacity);
void aura_event_queue_destroy(AuraEventQueue *queue);
void aura_event_queue_reset(AuraEventQueue *queue);
int aura_event_queue_push(AuraEventQueue *queue, const AuraPlatformEvent *event);
size_t aura_event_queue_begin_snapshot(AuraEventQueue *queue);
void aura_event_queue_clear_snapshot(AuraEventQueue *queue);
size_t aura_event_queue_snapshot_count(AuraEventQueue *queue);
int aura_event_queue_snapshot_copy(AuraEventQueue *queue, size_t index, AuraPlatformEvent *out);
size_t aura_event_queue_snapshot_text_length(AuraEventQueue *queue, size_t index);
int aura_event_queue_snapshot_text_byte(AuraEventQueue *queue, size_t index, size_t offset);
uint64_t aura_event_queue_overflow_count(AuraEventQueue *queue);
uint64_t aura_event_queue_dropped_count(AuraEventQueue *queue);
uint64_t aura_event_queue_coalesced_count(AuraEventQueue *queue);
uint64_t aura_event_queue_owned_text_count(AuraEventQueue *queue);

#endif
