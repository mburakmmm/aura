#include "event_queue.h"

#include <stdlib.h>
#include <string.h>

static int event_valid(const AuraPlatformEvent *event) {
    if (event == NULL) {
        return 0;
    }
    if (event->abi_version != AURA_PLATFORM_EVENT_ABI_VERSION) {
        return 0;
    }
    if (event->struct_size < sizeof(AuraPlatformEvent)) {
        return 0;
    }
    return 1;
}

static int event_coalesces(const AuraPlatformEvent *previous, const AuraPlatformEvent *next) {
    if (previous->kind == AURA_POINTER_MOVE && next->kind == AURA_POINTER_MOVE) {
        return previous->device_id == next->device_id && previous->pointer_id == next->pointer_id;
    }
    if (previous->kind == AURA_METRICS && next->kind == AURA_METRICS) {
        return 1;
    }
    if (previous->kind == AURA_SYSTEM_BACK && next->kind == AURA_SYSTEM_BACK &&
        previous->key_code == AURA_BACK_PHASE_UPDATE && next->key_code == AURA_BACK_PHASE_UPDATE) {
        return 1;
    }
    return 0;
}

static int event_is_evictable(const AuraPlatformEvent *event) {
    return event->kind == AURA_POINTER_MOVE || event->kind == AURA_METRICS ||
           (event->kind == AURA_SYSTEM_BACK && event->key_code == AURA_BACK_PHASE_UPDATE);
}

static void event_release(AuraEventQueue *queue, AuraPlatformEvent *event) {
    if (event->text != NULL && event->text_ownership == AURA_TEXT_OWNED) {
        free((void *)event->text);
        queue->texts_freed += 1;
    }
    memset(event, 0, sizeof(*event));
}

static int event_copy_owned(AuraEventQueue *queue, AuraPlatformEvent *target, const AuraPlatformEvent *source) {
    char *text = NULL;
    memcpy(target, source, sizeof(*target));
    target->struct_size = (uint32_t)sizeof(*target);
    target->abi_version = AURA_PLATFORM_EVENT_ABI_VERSION;
    target->text = NULL;
    target->text_ownership = AURA_TEXT_BORROWED;
    if (source->text != NULL && source->text_length > 0) {
        text = (char *)malloc((size_t)source->text_length + 1u);
        if (text == NULL) {
            memset(target, 0, sizeof(*target));
            return 0;
        }
        memcpy(text, source->text, source->text_length);
        text[source->text_length] = 0;
        target->text = text;
        target->text_ownership = AURA_TEXT_OWNED;
        queue->texts_allocated += 1;
    } else {
        target->text_length = 0;
    }
    return 1;
}

static void clear_buffer(AuraEventQueue *queue, AuraPlatformEvent *buffer, size_t count) {
    size_t i = 0;
    while (i < count) {
        event_release(queue, &buffer[i]);
        i += 1;
    }
}

static void note_drop(AuraEventQueue *queue) {
    queue->overflow_count += 1;
    queue->dropped_count += 1;
    queue->dropped_since_snapshot += 1;
}

int aura_event_queue_init(AuraEventQueue *queue, size_t capacity) {
    if (queue == NULL || capacity == 0 || capacity > UINT32_MAX) {
        return 0;
    }
    memset(queue, 0, sizeof(*queue));
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return 0;
    }
    queue->pending = (AuraPlatformEvent *)calloc(capacity + 1u, sizeof(AuraPlatformEvent));
    queue->snapshot = (AuraPlatformEvent *)calloc(capacity + 1u, sizeof(AuraPlatformEvent));
    if (queue->pending == NULL || queue->snapshot == NULL) {
        free(queue->pending);
        free(queue->snapshot);
        queue->pending = NULL;
        queue->snapshot = NULL;
        pthread_mutex_destroy(&queue->mutex);
        return 0;
    }
    queue->capacity = capacity;
    queue->initialized = 1;
    return 1;
}

void aura_event_queue_destroy(AuraEventQueue *queue) {
    if (queue == NULL || !queue->initialized) {
        return;
    }
    pthread_mutex_lock(&queue->mutex);
    clear_buffer(queue, queue->pending, queue->pending_count);
    clear_buffer(queue, queue->snapshot, queue->snapshot_count);
    free(queue->pending);
    free(queue->snapshot);
    queue->pending = NULL;
    queue->snapshot = NULL;
    queue->pending_count = 0;
    queue->snapshot_count = 0;
    queue->initialized = 0;
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
}

void aura_event_queue_reset(AuraEventQueue *queue) {
    if (queue == NULL || !queue->initialized) {
        return;
    }
    pthread_mutex_lock(&queue->mutex);
    clear_buffer(queue, queue->pending, queue->pending_count);
    clear_buffer(queue, queue->snapshot, queue->snapshot_count);
    queue->pending_count = 0;
    queue->snapshot_count = 0;
    queue->high_water_mark = 0;
    queue->overflow_count = 0;
    queue->dropped_count = 0;
    queue->dropped_since_snapshot = 0;
    queue->coalesced_count = 0;
    queue->texts_allocated = 0;
    queue->texts_freed = 0;
    queue->last_sequence = 0;
    queue->last_timestamp_ns = 0;
    pthread_mutex_unlock(&queue->mutex);
}

int aura_event_queue_push(AuraEventQueue *queue, const AuraPlatformEvent *event) {
    AuraPlatformEvent owned;
    size_t i = 0;
    int result = AURA_QUEUE_PUSH_OK;
    if (queue == NULL || !queue->initialized || !event_valid(event)) {
        return AURA_QUEUE_PUSH_INVALID;
    }
    memset(&owned, 0, sizeof(owned));
    pthread_mutex_lock(&queue->mutex);
    queue->last_sequence = event->sequence;
    queue->last_timestamp_ns = event->timestamp_ns;
    if (queue->pending_count > 0 && event_coalesces(&queue->pending[queue->pending_count - 1u], event)) {
        if (!event_copy_owned(queue, &owned, event)) {
            note_drop(queue);
            pthread_mutex_unlock(&queue->mutex);
            return AURA_QUEUE_PUSH_DROPPED;
        }
        event_release(queue, &queue->pending[queue->pending_count - 1u]);
        queue->pending[queue->pending_count - 1u] = owned;
        queue->coalesced_count += 1;
        pthread_mutex_unlock(&queue->mutex);
        return AURA_QUEUE_PUSH_COALESCED;
    }
    if (queue->pending_count >= queue->capacity) {
        while (i < queue->pending_count && !event_is_evictable(&queue->pending[i])) {
            i += 1;
        }
        if (i < queue->pending_count && !event_is_evictable(event)) {
            event_release(queue, &queue->pending[i]);
            if (i + 1u < queue->pending_count) {
                memmove(&queue->pending[i], &queue->pending[i + 1u], (queue->pending_count - i - 1u) * sizeof(AuraPlatformEvent));
            }
            memset(&queue->pending[queue->pending_count - 1u], 0, sizeof(AuraPlatformEvent));
            queue->pending_count -= 1;
            note_drop(queue);
        } else {
            note_drop(queue);
            pthread_mutex_unlock(&queue->mutex);
            return AURA_QUEUE_PUSH_DROPPED;
        }
    }
    if (!event_copy_owned(queue, &owned, event)) {
        note_drop(queue);
        pthread_mutex_unlock(&queue->mutex);
        return AURA_QUEUE_PUSH_DROPPED;
    }
    queue->pending[queue->pending_count] = owned;
    queue->pending_count += 1;
    if (queue->pending_count > queue->high_water_mark) {
        queue->high_water_mark = queue->pending_count;
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

size_t aura_event_queue_begin_snapshot(AuraEventQueue *queue) {
    AuraPlatformEvent *swap = NULL;
    if (queue == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    clear_buffer(queue, queue->snapshot, queue->snapshot_count);
    queue->snapshot_count = 0;
    swap = queue->snapshot;
    queue->snapshot = queue->pending;
    queue->pending = swap;
    queue->snapshot_count = queue->pending_count;
    queue->pending_count = 0;
    if (queue->dropped_since_snapshot > 0) {
        AuraPlatformEvent diagnostic;
        aura_platform_event_init(&diagnostic, AURA_QUEUE_OVERFLOW, queue->last_sequence, queue->last_timestamp_ns);
        diagnostic.payload.queue_overflow.dropped_count = queue->dropped_since_snapshot;
        diagnostic.payload.queue_overflow.overflow_count = queue->overflow_count;
        diagnostic.payload.queue_overflow.capacity = (uint32_t)queue->capacity;
        diagnostic.payload.queue_overflow.high_water_mark = (uint32_t)queue->high_water_mark;
        queue->snapshot[queue->snapshot_count] = diagnostic;
        queue->snapshot_count += 1;
        queue->dropped_since_snapshot = 0;
    }
    pthread_mutex_unlock(&queue->mutex);
    return queue->snapshot_count;
}

void aura_event_queue_clear_snapshot(AuraEventQueue *queue) {
    if (queue == NULL || !queue->initialized) {
        return;
    }
    pthread_mutex_lock(&queue->mutex);
    clear_buffer(queue, queue->snapshot, queue->snapshot_count);
    queue->snapshot_count = 0;
    pthread_mutex_unlock(&queue->mutex);
}

size_t aura_event_queue_snapshot_count(AuraEventQueue *queue) {
    size_t count = 0;
    if (queue == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    count = queue->snapshot_count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

int aura_event_queue_snapshot_copy(AuraEventQueue *queue, size_t index, AuraPlatformEvent *out) {
    int found = 0;
    if (queue == NULL || out == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    if (index < queue->snapshot_count) {
        memcpy(out, &queue->snapshot[index], sizeof(*out));
        found = 1;
    }
    pthread_mutex_unlock(&queue->mutex);
    return found;
}

size_t aura_event_queue_snapshot_text_length(AuraEventQueue *queue, size_t index) {
    size_t length = 0;
    if (queue == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    if (index < queue->snapshot_count) {
        length = queue->snapshot[index].text_length;
    }
    pthread_mutex_unlock(&queue->mutex);
    return length;
}

int aura_event_queue_snapshot_text_byte(AuraEventQueue *queue, size_t index, size_t offset) {
    int value = 0;
    if (queue == NULL || !queue->initialized) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    if (index < queue->snapshot_count && queue->snapshot[index].text != NULL && offset < queue->snapshot[index].text_length) {
        value = (unsigned char)queue->snapshot[index].text[offset];
    }
    pthread_mutex_unlock(&queue->mutex);
    return value;
}

uint64_t aura_event_queue_overflow_count(AuraEventQueue *queue) {
    uint64_t value = 0;
    if (queue == NULL || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->mutex);
    value = queue->overflow_count;
    pthread_mutex_unlock(&queue->mutex);
    return value;
}

uint64_t aura_event_queue_dropped_count(AuraEventQueue *queue) {
    uint64_t value = 0;
    if (queue == NULL || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->mutex);
    value = queue->dropped_count;
    pthread_mutex_unlock(&queue->mutex);
    return value;
}

uint64_t aura_event_queue_coalesced_count(AuraEventQueue *queue) {
    uint64_t value = 0;
    if (queue == NULL || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->mutex);
    value = queue->coalesced_count;
    pthread_mutex_unlock(&queue->mutex);
    return value;
}

uint64_t aura_event_queue_owned_text_count(AuraEventQueue *queue) {
    uint64_t value = 0;
    if (queue == NULL || !queue->initialized) return 0;
    pthread_mutex_lock(&queue->mutex);
    value = queue->texts_allocated - queue->texts_freed;
    pthread_mutex_unlock(&queue->mutex);
    return value;
}
