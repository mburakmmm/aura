#include "event_queue.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static AuraPlatformEvent make_event(uint32_t kind, uint64_t sequence) {
    AuraPlatformEvent event;
    aura_platform_event_init(&event, kind, sequence, sequence * 1000u);
    return event;
}

static int expect_kind(AuraEventQueue *queue, size_t index, uint32_t kind, uint64_t sequence) {
    AuraPlatformEvent event;
    memset(&event, 0, sizeof(event));
    if (!aura_event_queue_snapshot_copy(queue, index, &event)) return 0;
    return event.kind == kind && event.sequence == sequence;
}

static int test_ordering_and_coalescing(void) {
    AuraEventQueue queue;
    AuraPlatformEvent event;
    char text[] = "aura";
    uint64_t coalesced_before = 0;
    if (!aura_event_queue_init(&queue, 16)) return 1;

    event = make_event(AURA_POINTER_DOWN, 1);
    event.device_id = 3;
    event.pointer_id = 7;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 2;
    event = make_event(AURA_POINTER_MOVE, 2);
    event.device_id = 3;
    event.pointer_id = 7;
    event.x = 10.0;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 3;
    event = make_event(AURA_POINTER_MOVE, 3);
    event.device_id = 3;
    event.pointer_id = 7;
    event.x = 20.0;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_COALESCED) return 4;
    event = make_event(AURA_TEXT, 4);
    event.text = text;
    event.text_length = 4;
    event.text_ownership = AURA_TEXT_BORROWED;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 5;
    text[0] = 'X';
    event = make_event(AURA_LIFECYCLE, 5);
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 6;
    event = make_event(AURA_POINTER_CANCEL, 6);
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 7;

    if (aura_event_queue_begin_snapshot(&queue) != 5) return 8;
    if (!expect_kind(&queue, 0, AURA_POINTER_DOWN, 1)) return 9;
    if (!expect_kind(&queue, 1, AURA_POINTER_MOVE, 3)) return 10;
    if (!expect_kind(&queue, 2, AURA_TEXT, 4)) return 11;
    if (!expect_kind(&queue, 3, AURA_LIFECYCLE, 5)) return 12;
    if (!expect_kind(&queue, 4, AURA_POINTER_CANCEL, 6)) return 13;
    if (aura_event_queue_snapshot_text_length(&queue, 2) != 4) return 14;
    if (aura_event_queue_snapshot_text_byte(&queue, 2, 0) != 'a') return 15;
    if (aura_event_queue_owned_text_count(&queue) != 1) return 16;
    coalesced_before = aura_event_queue_coalesced_count(&queue);
    if (coalesced_before != 1) return 17;
    aura_event_queue_clear_snapshot(&queue);
    if (aura_event_queue_owned_text_count(&queue) != 0) return 18;

    event = make_event(AURA_POINTER_DOWN, 10);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_DOWN, 11);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_UP, 12);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_UP, 13);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_CANCEL, 14);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_CANCEL, 15);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_TEXT, 16);
    event.text = "a";
    event.text_length = 1;
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_TEXT, 17);
    event.text = "b";
    event.text_length = 1;
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_LIFECYCLE, 18);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_LIFECYCLE, 19);
    aura_event_queue_push(&queue, &event);
    if (aura_event_queue_begin_snapshot(&queue) != 10) return 19;
    if (!expect_kind(&queue, 0, AURA_POINTER_DOWN, 10)) return 20;
    if (!expect_kind(&queue, 1, AURA_POINTER_DOWN, 11)) return 21;
    if (!expect_kind(&queue, 4, AURA_POINTER_CANCEL, 14)) return 22;
    if (!expect_kind(&queue, 5, AURA_POINTER_CANCEL, 15)) return 23;
    if (!expect_kind(&queue, 8, AURA_LIFECYCLE, 18)) return 24;
    if (!expect_kind(&queue, 9, AURA_LIFECYCLE, 19)) return 25;
    aura_event_queue_clear_snapshot(&queue);

    event = make_event(AURA_METRICS, 20);
    event.payload.metrics.logical_width = 100.0;
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_METRICS, 21);
    event.payload.metrics.logical_width = 200.0;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_COALESCED) return 26;
    if (aura_event_queue_begin_snapshot(&queue) != 1) return 27;
    if (!expect_kind(&queue, 0, AURA_METRICS, 21)) return 28;
    aura_event_queue_clear_snapshot(&queue);

    event = make_event(AURA_SYSTEM_BACK, 22);
    event.key_code = AURA_BACK_PHASE_START;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 29;
    event = make_event(AURA_SYSTEM_BACK, 23);
    event.key_code = AURA_BACK_PHASE_UPDATE;
    event.x = 0.25;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 30;
    event = make_event(AURA_SYSTEM_BACK, 24);
    event.key_code = AURA_BACK_PHASE_UPDATE;
    event.x = 0.75;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_COALESCED) return 31;
    event = make_event(AURA_SYSTEM_BACK, 25);
    event.key_code = AURA_BACK_PHASE_CANCEL;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 32;
    event = make_event(AURA_SYSTEM_BACK, 26);
    event.key_code = AURA_BACK_PHASE_COMMIT;
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 33;
    if (aura_event_queue_begin_snapshot(&queue) != 4) return 34;
    if (!expect_kind(&queue, 0, AURA_SYSTEM_BACK, 22)) return 35;
    if (!expect_kind(&queue, 1, AURA_SYSTEM_BACK, 24)) return 36;
    if (!expect_kind(&queue, 2, AURA_SYSTEM_BACK, 25)) return 37;
    if (!expect_kind(&queue, 3, AURA_SYSTEM_BACK, 26)) return 38;
    {
        AuraPlatformEvent progress;
        if (!aura_event_queue_snapshot_copy(&queue, 1, &progress)) return 39;
        if (progress.key_code != AURA_BACK_PHASE_UPDATE || progress.x != 0.75) return 40;
    }
    aura_event_queue_clear_snapshot(&queue);
    aura_event_queue_destroy(&queue);
    return 0;
}

static int test_frame_boundary(void) {
    AuraEventQueue queue;
    AuraPlatformEvent event;
    if (!aura_event_queue_init(&queue, 4)) return 1;
    event = make_event(AURA_POINTER_DOWN, 1);
    aura_event_queue_push(&queue, &event);
    if (aura_event_queue_begin_snapshot(&queue) != 1) return 2;
    event = make_event(AURA_POINTER_UP, 2);
    aura_event_queue_push(&queue, &event);
    if (aura_event_queue_snapshot_count(&queue) != 1) return 3;
    if (!expect_kind(&queue, 0, AURA_POINTER_DOWN, 1)) return 4;
    aura_event_queue_clear_snapshot(&queue);
    if (aura_event_queue_begin_snapshot(&queue) != 1) return 5;
    if (!expect_kind(&queue, 0, AURA_POINTER_UP, 2)) return 6;
    aura_event_queue_destroy(&queue);
    return 0;
}

static int test_overflow_diagnostic(void) {
    AuraEventQueue queue;
    AuraPlatformEvent event;
    AuraPlatformEvent diagnostic;
    char text[] = "z";
    if (!aura_event_queue_init(&queue, 3)) return 1;
    event = make_event(AURA_POINTER_MOVE, 1);
    event.pointer_id = 1;
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_POINTER_DOWN, 2);
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_TEXT, 3);
    event.text = text;
    event.text_length = 1;
    aura_event_queue_push(&queue, &event);
    event = make_event(AURA_LIFECYCLE, 4);
    if (aura_event_queue_push(&queue, &event) != AURA_QUEUE_PUSH_OK) return 2;
    if (aura_event_queue_begin_snapshot(&queue) != 4) return 3;
    if (!expect_kind(&queue, 0, AURA_POINTER_DOWN, 2)) return 4;
    if (!expect_kind(&queue, 1, AURA_TEXT, 3)) return 5;
    if (!expect_kind(&queue, 2, AURA_LIFECYCLE, 4)) return 6;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!aura_event_queue_snapshot_copy(&queue, 3, &diagnostic)) return 7;
    if (diagnostic.kind != AURA_QUEUE_OVERFLOW) return 8;
    if (diagnostic.payload.queue_overflow.dropped_count != 1) return 9;
    if (diagnostic.payload.queue_overflow.capacity != 3) return 10;
    if (aura_event_queue_overflow_count(&queue) != 1) return 11;
    if (aura_event_queue_dropped_count(&queue) != 1) return 12;
    aura_event_queue_clear_snapshot(&queue);
    if (aura_event_queue_owned_text_count(&queue) != 0) return 13;
    aura_event_queue_destroy(&queue);
    return 0;
}

typedef struct ProducerContext {
    AuraEventQueue *queue;
    uint64_t base;
    int count;
    _Atomic int *failures;
    _Atomic int *done;
} ProducerContext;

static void *producer_main(void *raw) {
    ProducerContext *context = (ProducerContext *)raw;
    int i = 0;
    static const char byte[] = "q";
    for (i = 0; i < context->count; i++) {
        AuraPlatformEvent event = make_event(AURA_TEXT, context->base + (uint64_t)i);
        event.text = byte;
        event.text_length = 1;
        if (aura_event_queue_push(context->queue, &event) != AURA_QUEUE_PUSH_OK) {
            atomic_fetch_add_explicit(context->failures, 1, memory_order_relaxed);
        }
    }
    atomic_fetch_add_explicit(context->done, 1, memory_order_release);
    return NULL;
}

static int test_concurrent_producers_and_reader(void) {
    enum { PRODUCERS = 4, PER_PRODUCER = 2500 };
    AuraEventQueue queue;
    pthread_t threads[PRODUCERS];
    ProducerContext contexts[PRODUCERS];
    _Atomic int failures = 0;
    _Atomic int done = 0;
    uint64_t consumed = 0;
    int i = 0;
    if (!aura_event_queue_init(&queue, PRODUCERS * PER_PRODUCER)) return 1;
    for (i = 0; i < PRODUCERS; i++) {
        contexts[i].queue = &queue;
        contexts[i].base = (uint64_t)i * 100000u;
        contexts[i].count = PER_PRODUCER;
        contexts[i].failures = &failures;
        contexts[i].done = &done;
        if (pthread_create(&threads[i], NULL, producer_main, &contexts[i]) != 0) return 2;
    }
    for (;;) {
        size_t count = aura_event_queue_begin_snapshot(&queue);
        consumed += count;
        aura_event_queue_clear_snapshot(&queue);
        if (atomic_load_explicit(&done, memory_order_acquire) == PRODUCERS) {
            count = aura_event_queue_begin_snapshot(&queue);
            consumed += count;
            aura_event_queue_clear_snapshot(&queue);
            if (count == 0) break;
        }
    }
    for (i = 0; i < PRODUCERS; i++) {
        pthread_join(threads[i], NULL);
    }
    if (atomic_load_explicit(&failures, memory_order_relaxed) != 0) return 3;
    if (consumed != PRODUCERS * PER_PRODUCER) return 4;
    if (aura_event_queue_owned_text_count(&queue) != 0) return 5;
    aura_event_queue_destroy(&queue);
    return 0;
}

static uint32_t fuzz_next(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int test_hundred_thousand_event_fuzz(void) {
    AuraEventQueue queue;
    uint64_t sequence = 1;
    uint32_t state = UINT32_C(0xA17A2026);
    int saw_overflow = 0;
    if (!aura_event_queue_init(&queue, 32)) return 1;
    while (sequence <= 100000) {
        uint32_t value = fuzz_next(&state);
        if (value % 64u == 0) {
            size_t count = aura_event_queue_begin_snapshot(&queue);
            size_t i = 0;
            while (i < count) {
                AuraPlatformEvent snapshot;
                if (!aura_event_queue_snapshot_copy(&queue, i, &snapshot)) return 2;
                if (snapshot.abi_version != AURA_PLATFORM_EVENT_ABI_VERSION) return 3;
                if (snapshot.kind == AURA_QUEUE_OVERFLOW) saw_overflow = 1;
                i += 1;
            }
            aura_event_queue_clear_snapshot(&queue);
        } else {
            static const char utf8[] = "a\xC3\xA7";
            uint32_t kinds[] = {
                AURA_POINTER_DOWN,
                AURA_POINTER_MOVE,
                AURA_POINTER_UP,
                AURA_POINTER_CANCEL,
                AURA_TEXT,
                AURA_LIFECYCLE,
                AURA_METRICS
            };
            AuraPlatformEvent event = make_event(kinds[value % 7u], sequence);
            event.device_id = (int64_t)((value >> 8) % 4u);
            event.pointer_id = (int64_t)((value >> 12) % 16u);
            event.x = (double)(value % 1000u);
            event.y = (double)((value >> 10) % 1000u);
            if (event.kind == AURA_TEXT) {
                event.text = utf8;
                event.text_length = 3;
            }
            if (aura_event_queue_push(&queue, &event) == AURA_QUEUE_PUSH_INVALID) return 4;
        }
        sequence += 1;
    }
    {
        size_t count = aura_event_queue_begin_snapshot(&queue);
        size_t i = 0;
        while (i < count) {
            AuraPlatformEvent snapshot;
            if (!aura_event_queue_snapshot_copy(&queue, i, &snapshot)) return 5;
            if (snapshot.kind == AURA_QUEUE_OVERFLOW) saw_overflow = 1;
            i += 1;
        }
    }
    aura_event_queue_clear_snapshot(&queue);
    if (aura_event_queue_overflow_count(&queue) == 0 || !saw_overflow) return 6;
    if (aura_event_queue_owned_text_count(&queue) != 0) return 7;
    aura_event_queue_destroy(&queue);
    return 0;
}

int main(void) {
    int result = test_ordering_and_coalescing();
    if (result != 0) {
        fprintf(stderr, "ordering/coalescing failed: %d\n", result);
        return 10 + result;
    }
    result = test_frame_boundary();
    if (result != 0) {
        fprintf(stderr, "frame boundary failed: %d\n", result);
        return 40 + result;
    }
    result = test_overflow_diagnostic();
    if (result != 0) {
        fprintf(stderr, "overflow failed: %d\n", result);
        return 60 + result;
    }
    result = test_concurrent_producers_and_reader();
    if (result != 0) {
        fprintf(stderr, "concurrency failed: %d\n", result);
        return 80 + result;
    }
    result = test_hundred_thousand_event_fuzz();
    if (result != 0) {
        fprintf(stderr, "100k failed: %d\n", result);
        return 100 + result;
    }
    return 0;
}
