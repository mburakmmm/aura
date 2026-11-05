#include "text_units.h"

#include <stdio.h>
#include <string.h>

static const char *kSentence = "The harbor is quiet after the rain, and the notebook stayed open on the desk while the light moved across the page.";

static int ascii_space(void *context, int64_t index) {
    const char *text = (const char *)context;
    char value = text[index];
    return value == ' ' || value == '\n' || value == '\t' || value == '\r';
}

static int64_t ascii_step(void *context, int64_t index, int forward) {
    (void)context;
    return forward ? index + 1 : index - 1;
}

static AuraTextCursor sentence_cursor(void) {
    AuraTextCursor cursor;
    cursor.context = (void *)kSentence;
    cursor.length = (int64_t)strlen(kSentence);
    cursor.is_space = ascii_space;
    cursor.step = ascii_step;
    return cursor;
}

static int fail(int code) {
    fprintf(stderr, "text units failed: %d\n", code);
    return code;
}

static int test_space_reads_one_word(void) {
    AuraTextCursor cursor = sentence_cursor();
    AuraTextSpan span;
    const char *expected = "The ";
    if (!aura_text_word_at(&cursor, 3, &span)) return 1;
    if (span.start != 0 || span.end != 4) return 2;
    if (span.end - span.start >= cursor.length / 2) return 3;
    if (memcmp(kSentence + span.start, expected, (size_t)(span.end - span.start)) != 0) return 4;
    if (!aura_text_word_at(&cursor, 4, &span)) return 5;
    if (span.start != 4 || memcmp(kSentence + span.start, "harbor", 6) != 0 || span.end != 10) return 6;
    return 0;
}

static int test_word_walk_covers_sentence_once(void) {
    AuraTextCursor cursor = sentence_cursor();
    int64_t index = 0;
    int words = 0;
    int64_t previous = -1;
    while (index != AURA_TEXT_NONE) {
        AuraTextSpan span;
        int64_t next = 0;
        if (!aura_text_word_at(&cursor, index, &span)) return 10;
        if (span.start != index || span.end <= span.start || span.end - span.start >= cursor.length) return 11;
        if (span.start <= previous) return 12;
        previous = span.start;
        words += 1;
        next = aura_text_next_word_start(&cursor, index);
        if (next != AURA_TEXT_NONE && next <= index) return 13;
        index = next;
        if (words > 64) return 14;
    }
    if (words != 22) return 15;
    if (aura_text_previous_word_start(&cursor, 4) != 0) return 16;
    if (aura_text_previous_word_start(&cursor, 0) != AURA_TEXT_NONE) return 17;
    if (aura_text_next_word_start(&cursor, cursor.length - 1) != AURA_TEXT_NONE) return 18;
    return 0;
}

static int test_direction_collision(void) {
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_WORD, 2, 1) != 0) return 20;
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_WORD, 1, 1) != 1) return 21;
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_LINE, 2, 1) != 1) return 22;
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_LINE, 3, 1) != 0) return 23;
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_LINE, 5, 1) != 1) return 24;
    if (aura_text_direction_forward(AURA_TEXT_GRANULARITY_PARAGRAPH, 2, 1) != 1) return 25;
    return 0;
}

static int test_wrapped_sentence_is_not_one_line(void) {
    AuraTextBoundary items[6];
    AuraTextSpan line;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    int index = 0;
    items[0] = (AuraTextBoundary){0, 8.0, 8.0, 20.0};
    items[1] = (AuraTextBoundary){4, 40.0, 8.0, 20.0};
    items[2] = (AuraTextBoundary){11, 96.0, 8.0, 20.0};
    items[3] = (AuraTextBoundary){14, 8.0, 28.0, 20.0};
    items[4] = (AuraTextBoundary){20, 48.0, 28.0, 20.0};
    items[5] = (AuraTextBoundary){28, 8.0, 48.0, 20.0};
    if (!aura_text_visual_line(items, 6, 4, &line)) return 30;
    if (line.start != 0 || line.end != 14) return 31;
    if (!aura_text_visual_line(items, 6, 20, &line)) return 32;
    if (line.start != 14 || line.end != 28) return 33;
    if (line.end - line.start >= (int64_t)strlen(kSentence)) return 34;
    if (!aura_text_point_on_text(items, 6, 36.0, 18.0, 12.0)) return 35;
    if (aura_text_point_on_text(items, 6, 36.0, 400.0, 12.0)) return 36;
    if (!aura_text_content_rect(items, 6, 12.0, &x, &y, &w, &h)) return 37;
    if (h >= 200.0 || w >= 400.0) return 38;
    while (index < 6) {
        if (items[index].y + items[index].height > y + h) return 39;
        index += 1;
    }
    return 0;
}

int main(void) {
    int result = test_space_reads_one_word();
    if (result != 0) return fail(result);
    result = test_word_walk_covers_sentence_once();
    if (result != 0) return fail(result);
    result = test_direction_collision();
    if (result != 0) return fail(result);
    result = test_wrapped_sentence_is_not_one_line();
    if (result != 0) return fail(result);
    printf("text units ok\n");
    return 0;
}
