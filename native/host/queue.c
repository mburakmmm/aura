#include "queue.h"

#include <stdlib.h>
#include <string.h>

static char *aura_dup(const char *text) {
    size_t length = 0;
    char *copy = NULL;
    if (text == NULL) {
        text = "";
    }
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

void aura_queue_init(AuraQueue *queue) {
    memset(queue, 0, sizeof(*queue));
}

void aura_queue_free(AuraQueue *queue) {
    aura_queue_clear(queue);
    free(queue->events);
    queue->events = NULL;
    queue->cap = 0;
}

void aura_queue_push(AuraQueue *queue, int kind, double x, double y, int64_t key, int64_t button) {
    AuraEvent *event = NULL;
    if (queue->count >= queue->cap) {
        int cap = queue->cap == 0 ? 32 : queue->cap * 2;
        AuraEvent *next = (AuraEvent *)realloc(queue->events, (size_t)cap * sizeof(AuraEvent));
        if (next == NULL) {
            return;
        }
        queue->events = next;
        queue->cap = cap;
    }
    event = &queue->events[queue->count];
    event->kind = kind;
    event->x = x;
    event->y = y;
    event->key = key;
    event->button = button;
    event->text = NULL;
    queue->count += 1;
}

void aura_queue_push_text(AuraQueue *queue, int kind, const char *text) {
    aura_queue_push(queue, kind, 0, 0, 0, 0);
    if (queue->count <= 0) {
        return;
    }
    queue->events[queue->count - 1].text = aura_dup(text);
}

void aura_queue_clear(AuraQueue *queue) {
    int i = 0;
    for (i = 0; i < queue->count; i++) {
        free(queue->events[i].text);
        queue->events[i].text = NULL;
    }
    queue->count = 0;
}

static void free_nodes(AuraNode *nodes, int count) {
    int i = 0;
    for (i = 0; i < count; i++) {
        free(nodes[i].role);
        free(nodes[i].label);
        free(nodes[i].value);
    }
    free(nodes);
}

void aura_ax_init(AuraAx *ax) {
    memset(ax, 0, sizeof(*ax));
}

void aura_ax_free(AuraAx *ax) {
    free_nodes(ax->nodes, ax->count);
    free_nodes(ax->pending, ax->pending_count);
    free(ax->query);
    memset(ax, 0, sizeof(*ax));
}

int64_t aura_ax_begin_nodes(AuraAx *ax) {
    free_nodes(ax->pending, ax->pending_count);
    ax->pending = NULL;
    ax->pending_count = 0;
    ax->pending_cap = 0;
    ax->building = 1;
    return 0;
}

int64_t aura_ax_add_node(AuraAx *ax, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press) {
    AuraNode *node = NULL;
    if (!ax->building) {
        return -1;
    }
    if (ax->pending_count >= ax->pending_cap) {
        int cap = ax->pending_cap == 0 ? 16 : ax->pending_cap * 2;
        AuraNode *next = (AuraNode *)realloc(ax->pending, (size_t)cap * sizeof(AuraNode));
        if (next == NULL) {
            return -1;
        }
        ax->pending = next;
        ax->pending_cap = cap;
    }
    node = &ax->pending[ax->pending_count];
    memset(node, 0, sizeof(*node));
    node->role = aura_dup(role);
    node->label = aura_dup(label);
    node->value = aura_dup(value);
    node->checked = checked != 0;
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;
    node->press = press;
    ax->pending_count += 1;
    return 0;
}

int64_t aura_ax_commit_nodes(AuraAx *ax) {
    free_nodes(ax->nodes, ax->count);
    ax->nodes = ax->pending;
    ax->count = ax->pending_count;
    ax->cap = ax->pending_cap;
    ax->pending = NULL;
    ax->pending_count = 0;
    ax->pending_cap = 0;
    ax->building = 0;
    return 0;
}

int64_t aura_ax_count_nodes(const AuraAx *ax) {
    return ax->count;
}

static const char *field_text(const AuraNode *node, int64_t field) {
    if (field == 1) {
        return node->label != NULL ? node->label : "";
    }
    if (field == 2) {
        return node->value != NULL ? node->value : "";
    }
    return node->role != NULL ? node->role : "";
}

int64_t aura_ax_text_len_node(AuraAx *ax, int64_t index, int64_t field) {
    const char *text = NULL;
    if (index < 0 || index >= ax->count) {
        return -1;
    }
    text = field_text(&ax->nodes[index], field);
    free(ax->query);
    ax->query = aura_dup(text);
    if (ax->query == NULL) {
        return 0;
    }
    return (int64_t)strlen(ax->query);
}

int64_t aura_ax_text_byte_node(const AuraAx *ax, int64_t offset) {
    if (ax->query == NULL || offset < 0 || offset >= (int64_t)strlen(ax->query)) {
        return 0;
    }
    return (unsigned char)ax->query[offset];
}

int64_t aura_ax_checked_node(const AuraAx *ax, int64_t index) {
    if (index < 0 || index >= ax->count) {
        return -1;
    }
    return ax->nodes[index].checked;
}

int64_t aura_ax_press_node(const AuraAx *ax, int64_t index) {
    if (index < 0 || index >= ax->count) {
        return -1;
    }
    return ax->nodes[index].press;
}

double aura_ax_bound(const AuraAx *ax, int64_t index, int field) {
    const AuraNode *node = NULL;
    if (index < 0 || index >= ax->count) {
        return -1.0;
    }
    node = &ax->nodes[index];
    if (field == 0) {
        return node->x;
    }
    if (field == 1) {
        return node->y;
    }
    if (field == 2) {
        return node->w;
    }
    return node->h;
}
