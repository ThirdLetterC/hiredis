
#ifndef HIREDIS_POLL_H
#define HIREDIS_POLL_H

#include <alloc.h>
#include <async.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/time.h>

/* Values to return from redisPollTick */
[[maybe_unused]] static constexpr int REDIS_POLL_HANDLED_READ = 0b0001;
[[maybe_unused]] static constexpr int REDIS_POLL_HANDLED_WRITE = 0b0010;
[[maybe_unused]] static constexpr int REDIS_POLL_HANDLED_TIMEOUT = 0b0100;

/* An adapter to allow manual polling of the async context by checking the state
 * of the underlying file descriptor.  Useful in cases where there is no formal
 * IO event loop but regular ticking can be used, such as in game engines. */

typedef struct redisPollEvents {
  redisAsyncContext *context;
  redisFD fd;
  bool reading;
  bool writing;
  bool in_tick;
  bool deleted;
  double deadline;
} redisPollEvents;

static double redisPollTimevalToDouble(const struct timeval *tv) {
  if (tv == nullptr)
    return 0.0;
  return (double)tv->tv_sec + (double)tv->tv_usec / 1'000'000.0;
}

static double redisPollGetNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return redisPollTimevalToDouble(&tv);
}

/* Poll for io, handling any pending callbacks.  The timeout argument can be
 * positive to wait for a maximum given time for IO, zero to poll, or negative
 * to wait forever */
static int redisPollTick(redisAsyncContext *ac, double timeout) {
  bool reading, writing;
  struct pollfd pfd;
  int handled;
  int ns;
  int itimeout;

  auto *e = (redisPollEvents *)ac->ev.data;
  if (e == nullptr)
    return 0;

  /* local flags, won't get changed during callbacks */
  reading = e->reading;
  writing = e->writing;
  if (!reading && !writing)
    return 0;

  pfd.fd = e->fd;
  pfd.events = 0;
  if (reading)
    pfd.events = POLLIN;
  if (writing)
    pfd.events |= POLLOUT;

  if (timeout >= 0.0) {
    itimeout = (int)(timeout * 1000.0);
  } else {
    itimeout = -1;
  }

  ns = poll(&pfd, 1, itimeout);
  if (ns < 0) {
    /* ignore the EINTR error */
    if (errno != EINTR)
      return ns;
    ns = 0;
  }

  handled = 0;
  e->in_tick = true;
  if (ns) {
    if (reading && (pfd.revents & POLLIN)) {
      redisAsyncHandleRead(ac);
      handled |= REDIS_POLL_HANDLED_READ;
    }
    /* Connection failure is indicated with an error event, handle it like
     * writable. */
    if (writing && (pfd.revents & (POLLOUT | POLLERR))) {
      /* context Read callback may have caused context to be deleted, e.g.
         by doing an redisAsyncDisconnect() */
      if (!e->deleted) {
        redisAsyncHandleWrite(ac);
        handled |= REDIS_POLL_HANDLED_WRITE;
      }
    }
  }

  /* perform timeouts */
  if (!e->deleted && e->deadline != 0.0) {
    double now = redisPollGetNow();
    if (now >= e->deadline) {
      /* deadline has passed.  disable timeout and perform callback */
      e->deadline = 0.0;
      redisAsyncHandleTimeout(ac);
      handled |= REDIS_POLL_HANDLED_TIMEOUT;
    }
  }

  /* do a delayed cleanup if required */
  if (e->deleted)
    hi_free(e);
  else
    e->in_tick = false;

  return handled;
}

static void redisPollAddRead(void *data) {
  auto *e = (redisPollEvents *)data;
  e->reading = true;
}

static void redisPollDelRead(void *data) {
  auto *e = (redisPollEvents *)data;
  e->reading = false;
}

static void redisPollAddWrite(void *data) {
  auto *e = (redisPollEvents *)data;
  e->writing = true;
}

static void redisPollDelWrite(void *data) {
  auto *e = (redisPollEvents *)data;
  e->writing = false;
}

static void redisPollCleanup(void *data) {
  auto *e = (redisPollEvents *)data;

  /* if we are currently processing a tick, postpone deletion */
  if (e->in_tick)
    e->deleted = true;
  else
    hi_free(e);
}

static void redisPollScheduleTimer(void *data, struct timeval tv) {
  auto *e = (redisPollEvents *)data;
  double now = redisPollGetNow();
  e->deadline = now + redisPollTimevalToDouble(&tv);
}

static int redisPollAttach(redisAsyncContext *ac) {
  auto *c = &(ac->c);
  redisPollEvents *e;

  /* Nothing should be attached when something is already attached */
  if (ac->ev.data != nullptr)
    return REDIS_ERR;

  /* Create container for context and r/w events */
  e = (redisPollEvents *)hi_calloc(1, sizeof(*e));
  if (e == nullptr)
    return REDIS_ERR;

  e->context = ac;
  e->fd = c->fd;
  e->reading = e->writing = false;
  e->in_tick = e->deleted = false;
  e->deadline = 0.0;

  /* Register functions to start/stop listening for events */
  ac->ev.addRead = redisPollAddRead;
  ac->ev.delRead = redisPollDelRead;
  ac->ev.addWrite = redisPollAddWrite;
  ac->ev.delWrite = redisPollDelWrite;
  ac->ev.scheduleTimer = redisPollScheduleTimer;
  ac->ev.cleanup = redisPollCleanup;
  ac->ev.data = e;

  return REDIS_OK;
}
#endif /* HIREDIS_POLL_H */
