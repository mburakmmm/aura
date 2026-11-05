#ifndef AURA_TEXT_UNITS_H
#define AURA_TEXT_UNITS_H

#include <stdint.h>

#define AURA_TEXT_NONE ((int64_t)-1)

enum {
    AURA_TEXT_GRANULARITY_CHARACTER = 0,
    AURA_TEXT_GRANULARITY_WORD = 1,
    AURA_TEXT_GRANULARITY_SENTENCE = 2,
    AURA_TEXT_GRANULARITY_PARAGRAPH = 3,
    AURA_TEXT_GRANULARITY_LINE = 4,
    AURA_TEXT_GRANULARITY_DOCUMENT = 5
};

typedef struct AuraTextCursor {
    void *context;
    int64_t length;
    int (*is_space)(void *context, int64_t index);
    int64_t (*step)(void *context, int64_t index, int forward);
} AuraTextCursor;

typedef struct AuraTextSpan {
    int64_t start;
    int64_t end;
} AuraTextSpan;

typedef struct AuraTextBoundary {
    int64_t offset;
    double x;
    double y;
    double height;
} AuraTextBoundary;

/* The word a finger at index should read.
   A space stays with the word before it. The span is never the rest of the text. */
int aura_text_word_at(const AuraTextCursor *cursor, int64_t index, AuraTextSpan *span);
int64_t aura_text_next_word_start(const AuraTextCursor *cursor, int64_t index);
int64_t aura_text_previous_word_start(const AuraTextCursor *cursor, int64_t index);

/* Storage direction 2 is backward. Layout direction 2 is right.
   Word movement uses the storage meaning. Line movement uses right and down. */
int aura_text_direction_forward(int granularity, int64_t direction, int left_to_right);

int aura_text_visual_line(const AuraTextBoundary *items, int count, int64_t offset, AuraTextSpan *span);
int aura_text_content_rect(const AuraTextBoundary *items, int count, double pad, double *x, double *y, double *w, double *h);
int aura_text_point_on_text(const AuraTextBoundary *items, int count, double x, double y, double slop);

#endif
