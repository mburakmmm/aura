#ifndef AURA_QUEUE_H
#define AURA_QUEUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuraEvent {
    int kind;
    double x;
    double y;
    int64_t key;
    int64_t button;
    char *text;
} AuraEvent;

typedef struct AuraQueue {
    AuraEvent *events;
    int count;
    int cap;
} AuraQueue;

void aura_queue_init(AuraQueue *queue);
void aura_queue_free(AuraQueue *queue);
void aura_queue_push(AuraQueue *queue, int kind, double x, double y, int64_t key, int64_t button);
void aura_queue_push_text(AuraQueue *queue, int kind, const char *text);
void aura_queue_clear(AuraQueue *queue);

typedef struct AuraNode {
    char *role;
    char *label;
    char *value;
    int checked;
    double x;
    double y;
    double w;
    double h;
    int64_t press;
} AuraNode;

typedef struct AuraAx {
    AuraNode *nodes;
    int count;
    int cap;
    int building;
    AuraNode *pending;
    int pending_count;
    int pending_cap;
    char *query;
} AuraAx;

void aura_ax_init(AuraAx *ax);
void aura_ax_free(AuraAx *ax);
int64_t aura_ax_begin_nodes(AuraAx *ax);
int64_t aura_ax_add_node(AuraAx *ax, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press);
int64_t aura_ax_commit_nodes(AuraAx *ax);
int64_t aura_ax_count_nodes(const AuraAx *ax);
int64_t aura_ax_text_len_node(AuraAx *ax, int64_t index, int64_t field);
int64_t aura_ax_text_byte_node(const AuraAx *ax, int64_t offset);
int64_t aura_ax_checked_node(const AuraAx *ax, int64_t index);
int64_t aura_ax_press_node(const AuraAx *ax, int64_t index);
double aura_ax_bound(const AuraAx *ax, int64_t index, int field);

#ifdef __cplusplus
}
#endif

#endif
