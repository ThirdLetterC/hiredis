#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <adapters/poll.h>
#include <async.h>

/* Put in the global scope, so that loop can be explicitly stopped */
static int exit_loop = 0;

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
  redisReply *reply = r;
  if (reply == nullptr)
    return;
  printf("argv[%s]: %s\n", (char *)privdata, reply->str);

  /* Disconnect after receiving the reply to GET */
  redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
  if (status != REDIS_OK) {
    printf("Error: %s\n", c->errstr);
    exit_loop = 1;
    return;
  }

  printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
  exit_loop = 1;
  if (status != REDIS_OK) {
    printf("Error: %s\n", c->errstr);
    return;
  }

  printf("Disconnected...\n");
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);

  redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
  if (c->err) {
    /* Let *c leak for now... */
    printf("Error: %s\n", c->errstr);
    return 1;
  }

  redisPollAttach(c);
  redisAsyncSetConnectCallback(c, connectCallback);
  redisAsyncSetDisconnectCallback(c, disconnectCallback);
  redisAsyncCommand(c, nullptr, nullptr, "SET key %b", argv[argc - 1], strlen(argv[argc - 1]));
  redisAsyncCommand(c, getCallback, (char *)"end-1", "GET key");
  while (!exit_loop) {
    redisPollTick(c, 0.1);
  }
  return 0;
}
