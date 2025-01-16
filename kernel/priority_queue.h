struct PriorityQueue{
    struct proc *heap[NPROC];
    int size;
    struct spinlock lock;
};


void pq_init(struct PriorityQueue *pq) {
    initlock(&pq->lock, "pq_lock");
    pq->size = 0;
}


static void swap(struct proc **a, struct proc **b) {
    struct proc *temp = *a;
    *a = *b;
    *b = temp;
}

void pq_push(struct PriorityQueue *pq, struct proc *p) {
    if (pq->size >= NPROC) {
        panic("Priority queue overflow");
    }

    pq->heap[pq->size] = p;
    int i = pq->size;
    pq->size++;

    // Bubble up
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (pq->heap[parent]->usage_time->sum_of_ticks <= pq->heap[i]->usage_time->sum_of_ticks) {
            if (pq->heap[parent]->usage_time->sum_of_ticks == pq->heap[i]->usage_time->sum_of_ticks) {
                if (pq->heap[parent]->usage_time->deadline <= pq->heap[i]->usage_time->deadline) break;
            }
            else break;
        }
        swap(&pq->heap[parent], &pq->heap[i]);
        i = parent;
    }
}

struct proc *pq_pop(struct PriorityQueue *pq) {
    if (pq->size == 0) {
        return NULL;
    }

    struct proc *min = pq->heap[0];
    pq->heap[0] = pq->heap[pq->size - 1];
    pq->size--;

    // Bubble down
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < pq->size && pq->heap[left]->usage_time->sum_of_ticks <= pq->heap[smallest]->usage_time->sum_of_ticks) {
            if (pq->heap[left]->usage_time->sum_of_ticks == pq->heap[smallest]->usage_time->sum_of_ticks) {
                if (pq->heap[left]->usage_time->deadline < pq->heap[smallest]->usage_time->deadline) smallest = left;
            }
            else smallest = left;
        }
        if (right < pq->size && pq->heap[right]->usage_time->sum_of_ticks <= pq->heap[smallest]->usage_time->sum_of_ticks) {
            if (pq->heap[right]->usage_time->sum_of_ticks == pq->heap[smallest]->usage_time->sum_of_ticks) {
                if (pq->heap[right]->usage_time->deadline < pq->heap[smallest]->usage_time->deadline) smallest = right;
            }
            else smallest = right;
        }

        if (smallest == i) {
            break;
        }

        swap(&pq->heap[i], &pq->heap[smallest]);
        i = smallest;
    }
    acquire(&min->lock);
    min->state = CHOSEN_TO_RUN;
    release(&min->lock);
    return min;
}

int pq_empty(struct PriorityQueue *pq) {
    return pq->size == 0;
}

int pq_check_and_push(struct PriorityQueue *pq, struct proc *p) {
    if (p->state != RUNNABLE) {
        return 0;
    }
    for (int i = 0; i < pq->size; i++) {
        if (pq->heap[i] == p) {
            return 0;
        }
    }

    if (pq->size >= NPROC) {
        panic("Priority queue overflow");
    }

    pq->heap[pq->size] = p;
    int i = pq->size;
    pq->size++;

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (pq->heap[parent]->usage_time->sum_of_ticks == pq->heap[i]->usage_time->sum_of_ticks) {
            if (pq->heap[parent]->usage_time->deadline <= pq->heap[i]->usage_time->deadline) break;
        }
        else break;
        swap(&pq->heap[parent], &pq->heap[i]);
        i = parent;
    }
    return 1;
}
