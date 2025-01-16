struct Queue{
    struct proc *procs[NPROC];
    int front;
    int rear;
    int size;
    struct spinlock lock;
};

void initializeQueue(struct Queue* q)
{
    initlock(&q->lock, "queue_lock");
    q->front = 0;
    q->rear = 0;
    q->size = 0;
}

int isEmpty(struct Queue* q) { return (q->size == 0); }

int isFull(struct Queue* q) { return (q->size == NPROC); }

//1 success, 0 fail
int enqueue(struct Queue *q, struct proc *p)
{
    if (isFull(q)) {
        return 0;
    }
    q->procs[q->rear] = p;
    q->rear = (q->rear + 1) % NPROC;
    q->size++;
    return 1;
}

struct proc* pop(struct Queue* q)
{
    if (isEmpty(q)) {
        return NULL;
    }
    struct proc* p = q->procs[q->front];
    q->front = (q->front + 1) % NPROC;
    q->size--;
    return p;
}
