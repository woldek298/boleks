#include "zmqextras.h"
#include <zmq.h>
#include <stdlib.h>
#include <thread>
#include "loguru.hpp"

struct czmq_thread_ctx {
  czmq_thread_proc *proc;
  void *arg;
  void *ctx;
  void *pipe;
};

static void *czmq_thread_entry_point(void *arg)
{
  czmq_thread_ctx *thctx = (czmq_thread_ctx *) arg;
  thctx->proc(thctx->arg, thctx->ctx, thctx->pipe);
  zmq_close(thctx->pipe);
  free(thctx);
  return nullptr;
}

void *czmq_thread_fork(void *ctx, czmq_thread_proc *proc, void *arg)
{
  char endpoint[256];
  void *pipe = zmq_socket(ctx, ZMQ_PAIR);
  snprintf(endpoint, sizeof(endpoint), "inproc://zctx-pipe-%p", pipe);
  if (zmq_bind(pipe, endpoint) == -1) {
    LOG_F(ERROR, "czmq_thread_fork: can't create pipe");
  }
  
  czmq_thread_ctx *thctx = (czmq_thread_ctx*)malloc(sizeof(czmq_thread_ctx));
  thctx->ctx = ctx;
  thctx->arg = arg;
  thctx->proc = proc;
  thctx->pipe = zmq_socket(ctx, ZMQ_PAIR);
  if (zmq_connect(thctx->pipe, endpoint) == -1) {
    LOG_F(ERROR, "czmq_thread_fork: can't connect to pipe");
  }

  std::thread thread(czmq_thread_entry_point, thctx);
  thread.detach();
  return pipe;
}

bool czmq_poll(void *self, int msecs)
{
  zmq_pollitem_t items [] = { { self, 0, ZMQ_POLLIN, 0 } };
  int rc = zmq_poll (items, 1, msecs);
  return rc != -1 &&
         (items [0].revents & ZMQ_POLLIN) != 0;
}

void czmq_signal(void *self)
{
  if (zmq_send(self, "", 0, 0) == -1) {
    LOG_F(ERROR, "czmq_signal failed");
  }
}

void czmq_wait(void *self)
{
  zmq_msg_t msg;
  zmq_msg_init(&msg);
  zmq_recvmsg(self, &msg, 0);  
  zmq_msg_close(&msg);    
}
