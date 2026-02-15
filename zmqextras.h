#ifndef __ZMQEXTRAS_H_
#define __ZMQEXTRAS_H_

typedef void czmq_thread_proc(void*, void*, void*);

void *czmq_thread_fork(void *ctx, czmq_thread_proc *proc, void *arg);
bool czmq_poll(void *self, int msecs);
void czmq_signal(void *self);
void czmq_wait(void *self);

#endif //__ZMQEXTRAS_H_
