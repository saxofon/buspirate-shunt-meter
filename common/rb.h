#ifndef __RB_H__
#define __RB_H__

struct s_rb {
        void *buf;
        int head;
        int tail;
        int nr_of_nodes;
};

extern int rb_head_n_tail_diff(struct s_rb *rb);
extern int rb_head_n_tail_update(struct s_rb *rb);
extern int rb_init(struct s_rb *rb, int nr_of_nodes, int node_sz);
extern int rb_free(struct s_rb *rb);
extern int rb_test(void);

#define RB_NODE(rb, type) ((type*)(rb->buf))[rb->head]
#define RB_NODE_IDX(rb, type, idx) ((type*)(rb->buf))[idx]


#define RB_HEAD(rb) (rb->head)
#define RB_HEAD_MINUS(rb, minus) (rb->head - minus > 0? \
	rb->head - minus: \
	rb->nr_of_nodes - abs(rb->head - minus))

#define RB_TAIL(rb) (rb->tail)

#endif
