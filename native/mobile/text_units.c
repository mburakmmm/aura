#include "text_units.h"

#include <stddef.h>

static int aura_text_space(const AuraTextCursor *cursor, int64_t index) {
    if (cursor == NULL || cursor->is_space == NULL || index < 0 || index >= cursor->length) return 0;
    return cursor->is_space(cursor->context, index) ? 1 : 0;
}

static int64_t aura_text_step(const AuraTextCursor *cursor, int64_t index, int forward) {
    int64_t next = AURA_TEXT_NONE;
    if (cursor == NULL || cursor->step == NULL) return AURA_TEXT_NONE;
    next = cursor->step(cursor->context, index, forward);
    if (forward) {
        if (next <= index || next > cursor->length) return AURA_TEXT_NONE;
    } else if (next < 0 || next >= index) {
        return AURA_TEXT_NONE;
    }
    return next;
}

static int64_t aura_text_run_end(const AuraTextCursor *cursor, int64_t index) {
    int space = 0;
    int guard = 0;
    if (index < 0 || index >= cursor->length) return AURA_TEXT_NONE;
    space = aura_text_space(cursor, index);
    while (index < cursor->length && aura_text_space(cursor, index) == space) {
        int64_t next = aura_text_step(cursor, index, 1);
        if (next == AURA_TEXT_NONE) return AURA_TEXT_NONE;
        index = next;
        guard += 1;
        if (guard > cursor->length) return AURA_TEXT_NONE;
    }
    return index;
}

static int64_t aura_text_run_start(const AuraTextCursor *cursor, int64_t end) {
    int space = 0;
    int guard = 0;
    int64_t index = AURA_TEXT_NONE;
    if (end <= 0 || end > cursor->length) return AURA_TEXT_NONE;
    index = aura_text_step(cursor, end, 0);
    if (index == AURA_TEXT_NONE) return AURA_TEXT_NONE;
    space = aura_text_space(cursor, index);
    while (index > 0) {
        int64_t previous = aura_text_step(cursor, index, 0);
        if (previous == AURA_TEXT_NONE || aura_text_space(cursor, previous) != space) break;
        index = previous;
        guard += 1;
        if (guard > cursor->length) return AURA_TEXT_NONE;
    }
    return index;
}

int aura_text_word_at(const AuraTextCursor *cursor, int64_t index, AuraTextSpan *span) {
    int64_t start = AURA_TEXT_NONE;
    int64_t end = AURA_TEXT_NONE;
    int guard = 0;
    if (cursor == NULL || span == NULL || cursor->length <= 0) return 0;
    if (index < 0) index = 0;
    if (index >= cursor->length) index = cursor->length - 1;
    if (!aura_text_space(cursor, index)) {
        end = aura_text_run_end(cursor, index);
        start = aura_text_run_start(cursor, end);
        if (start == AURA_TEXT_NONE || end == AURA_TEXT_NONE || start >= end) return 0;
        span->start = start;
        span->end = end;
        return 1;
    }
    start = index;
    while (start > 0 && aura_text_space(cursor, start - 1)) {
        int64_t previous = aura_text_step(cursor, start, 0);
        if (previous == AURA_TEXT_NONE) break;
        start = previous;
        guard += 1;
        if (guard > cursor->length) return 0;
    }
    if (start > 0) {
        int64_t word_end = start;
        int64_t word_start = aura_text_run_start(cursor, word_end);
        end = aura_text_run_end(cursor, index);
        if (word_start == AURA_TEXT_NONE || end == AURA_TEXT_NONE || word_start >= word_end) return 0;
        span->start = word_start;
        span->end = end;
        return 1;
    }
    end = aura_text_run_end(cursor, index);
    if (end == AURA_TEXT_NONE || end >= cursor->length) return 0;
    start = end;
    end = aura_text_run_end(cursor, start);
    if (end == AURA_TEXT_NONE || start >= end) return 0;
    span->start = start;
    span->end = end;
    return 1;
}

int64_t aura_text_next_word_start(const AuraTextCursor *cursor, int64_t index) {
    int64_t cursor_index = index;
    int guard = 0;
    if (cursor == NULL || index >= cursor->length) return AURA_TEXT_NONE;
    if (index < 0) index = 0;
    if (!aura_text_space(cursor, index)) {
        cursor_index = aura_text_run_end(cursor, index);
        if (cursor_index == AURA_TEXT_NONE) return AURA_TEXT_NONE;
    } else {
        cursor_index = index;
    }
    while (cursor_index < cursor->length && aura_text_space(cursor, cursor_index)) {
        int64_t next = aura_text_step(cursor, cursor_index, 1);
        if (next == AURA_TEXT_NONE) return AURA_TEXT_NONE;
        cursor_index = next;
        guard += 1;
        if (guard > cursor->length) return AURA_TEXT_NONE;
    }
    if (cursor_index >= cursor->length) return AURA_TEXT_NONE;
    return cursor_index;
}

int64_t aura_text_previous_word_start(const AuraTextCursor *cursor, int64_t index) {
    int64_t cursor_index = index;
    int guard = 0;
    int64_t back = AURA_TEXT_NONE;
    if (cursor == NULL || index <= 0) return AURA_TEXT_NONE;
    if (index > cursor->length) index = cursor->length;
    back = aura_text_step(cursor, index, 0);
    if (back == AURA_TEXT_NONE) return AURA_TEXT_NONE;
    cursor_index = back;
    if (aura_text_space(cursor, cursor_index)) {
        while (cursor_index > 0) {
            int64_t previous = aura_text_step(cursor, cursor_index, 0);
            if (previous == AURA_TEXT_NONE || !aura_text_space(cursor, previous)) break;
            cursor_index = previous;
            guard += 1;
            if (guard > cursor->length) return AURA_TEXT_NONE;
        }
        if (cursor_index == 0) return AURA_TEXT_NONE;
        back = aura_text_step(cursor, cursor_index, 0);
        if (back == AURA_TEXT_NONE) return AURA_TEXT_NONE;
        cursor_index = back;
    }
    while (cursor_index > 0) {
        int64_t previous = aura_text_step(cursor, cursor_index, 0);
        if (previous == AURA_TEXT_NONE || aura_text_space(cursor, previous)) break;
        cursor_index = previous;
        guard += 1;
        if (guard > cursor->length) return AURA_TEXT_NONE;
    }
    return cursor_index;
}

int aura_text_direction_forward(int granularity, int64_t direction, int left_to_right) {
    if (granularity == AURA_TEXT_GRANULARITY_LINE || granularity == AURA_TEXT_GRANULARITY_PARAGRAPH) {
        if (direction == 2 || direction == 5) return left_to_right ? 1 : 0;
        if (direction == 3 || direction == 4) return left_to_right ? 0 : 1;
    }
    return direction == 2 ? 0 : 1;
}

static int aura_text_same_line(double left, double right) {
    double delta = left - right;
    if (delta < 0.0) delta = -delta;
    return delta < 0.5;
}

int aura_text_visual_line(const AuraTextBoundary *items, int count, int64_t offset, AuraTextSpan *span) {
    int index = 0;
    int line = -1;
    int probe = 0;
    double y = 0.0;
    int64_t start = 0;
    int64_t end = 0;
    if (items == NULL || span == NULL || count <= 0) return 0;
    while (index < count) {
        if (items[index].offset <= offset) line = index;
        index += 1;
    }
    if (line < 0) line = 0;
    y = items[line].y;
    start = items[line].offset;
    probe = line;
    while (probe >= 0 && aura_text_same_line(items[probe].y, y)) {
        start = items[probe].offset;
        probe -= 1;
    }
    end = items[count - 1].offset;
    probe = line + 1;
    while (probe < count) {
        if (!aura_text_same_line(items[probe].y, y)) {
            end = items[probe].offset;
            break;
        }
        end = items[probe].offset;
        probe += 1;
    }
    if (start >= end) return 0;
    span->start = start;
    span->end = end;
    return 1;
}

static double aura_text_glyph_width(const AuraTextBoundary *items, int count, int index) {
    double width = 0.0;
    if (index + 1 < count && aura_text_same_line(items[index + 1].y, items[index].y) && items[index + 1].x > items[index].x) {
        width = items[index + 1].x - items[index].x;
    }
    if (width < 1.0) {
        width = items[index].height > 1.0 ? items[index].height * 0.25 : 1.0;
    }
    return width;
}

int aura_text_content_rect(const AuraTextBoundary *items, int count, double pad, double *x, double *y, double *w, double *h) {
    int index = 0;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    if (items == NULL || count <= 0 || x == NULL || y == NULL || w == NULL || h == NULL) return 0;
    left = items[0].x;
    top = items[0].y;
    right = items[0].x + aura_text_glyph_width(items, count, 0);
    bottom = items[0].y + (items[0].height > 1.0 ? items[0].height : 1.0);
    index = 1;
    while (index < count) {
        double edge = items[index].x + aura_text_glyph_width(items, count, index);
        double low = items[index].y + (items[index].height > 1.0 ? items[index].height : 1.0);
        if (items[index].x < left) left = items[index].x;
        if (items[index].y < top) top = items[index].y;
        if (edge > right) right = edge;
        if (low > bottom) bottom = low;
        index += 1;
    }
    if (pad < 0.0) pad = 0.0;
    left -= pad;
    top -= pad;
    right += pad;
    bottom += pad;
    if (right <= left || bottom <= top) return 0;
    *x = left;
    *y = top;
    *w = right - left;
    *h = bottom - top;
    return 1;
}

int aura_text_point_on_text(const AuraTextBoundary *items, int count, double x, double y, double slop) {
    int index = 0;
    int best = -1;
    double best_distance = 0.0;
    double line_y = 0.0;
    double left = 0.0;
    double right = 0.0;
    int found = 0;
    if (items == NULL || count <= 0) return 0;
    if (slop < 0.0) slop = 0.0;
    while (index < count) {
        double height = items[index].height > 1.0 ? items[index].height : 1.0;
        double middle = items[index].y + height * 0.5;
        double distance = middle - y;
        if (distance < 0.0) distance = -distance;
        if (distance <= height * 0.5 + slop && (best < 0 || distance < best_distance)) {
            best = index;
            best_distance = distance;
            line_y = items[index].y;
        }
        index += 1;
    }
    if (best < 0) return 0;
    index = 0;
    while (index < count) {
        double edge = 0.0;
        if (!aura_text_same_line(items[index].y, line_y)) {
            index += 1;
            continue;
        }
        edge = items[index].x + aura_text_glyph_width(items, count, index);
        if (!found || items[index].x < left) left = items[index].x;
        if (!found || edge > right) right = edge;
        found = 1;
        index += 1;
    }
    if (!found) return 0;
    return x >= left - slop && x <= right + slop;
}
