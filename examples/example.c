#include <hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void example_argv_command(redisContext *c, size_t n) {
  char **argv, tmp[42];
  size_t *argvlen;
  redisReply *reply;

  /* We're allocating two additional elements for command and key */
  argv = calloc(2 + n, sizeof(*argv));
  argvlen = calloc(2 + n, sizeof(*argvlen));
  if (argv == nullptr || argvlen == nullptr) {
    fprintf(stderr, "Error: out of memory\n");
    free(argv);
    free(argvlen);
    exit(1);
  }

  /* First the command */
  argv[0] = (char *)"RPUSH";
  argvlen[0] = sizeof("RPUSH") - 1;

  /* Now our key */
  argv[1] = (char *)"argvlist";
  argvlen[1] = sizeof("argvlist") - 1;

  /* Now add the entries we wish to add to the list */
  for (size_t i = 2; i < (n + 2); i++) {
    argvlen[i] = snprintf(tmp, sizeof(tmp), "argv-element-%zu", i - 2);
    argv[i] = strdup(tmp);
    if (argv[i] == nullptr) {
      fprintf(stderr, "Error: out of memory\n");
      for (size_t k = 2; k < i; k++) {
        free(argv[k]);
      }
      free(argv);
      free(argvlen);
      exit(1);
    }
  }

  /* Execute the command using redisCommandArgv.  We're sending the arguments
   * with two explicit arrays.  One for each argument's string, and the other
   * for its length. */
  reply = redisCommandArgv(c, n + 2, (const char **)argv, (const size_t *)argvlen);

  if (reply == nullptr || c->err) {
    fprintf(stderr, "Error:  Couldn't execute redisCommandArgv\n");
    exit(1);
  }

  if (reply->type == REDIS_REPLY_INTEGER) {
    printf("%s reply: %lld\n", argv[0], reply->integer);
  }

  freeReplyObject(reply);

  /* Clean up */
  for (size_t i = 2; i < (n + 2); i++) {
    free(argv[i]);
  }

  free(argv);
  free(argvlen);
}

int main(int argc, char **argv) {
  unsigned int j;
  bool isunix = false;
  redisContext *c;
  redisReply *reply;
  const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";

  if (argc > 2) {
    if (*argv[2] == 'u' || *argv[2] == 'U') {
      isunix = true;
      /* in this case, host is the path to the unix socket */
      printf("Will connect to unix socket @%s\n", hostname);
    }
  }

  int port = (argc > 2) ? atoi(argv[2]) : 6379;

  struct timeval timeout = {.tv_sec = 1, .tv_usec = 500'000}; // 1.5 seconds
  if (isunix) {
    c = redisConnectUnixWithTimeout(hostname, timeout);
  } else {
    c = redisConnectWithTimeout(hostname, port, timeout);
  }
  if (c == nullptr || c->err) {
    if (c) {
      printf("Connection error: %s\n", c->errstr);
      redisFree(c);
    } else {
      printf("Connection error: can't allocate redis context\n");
    }
    exit(1);
  }

  /* PING server */
  reply = redisCommand(c, "PING");
  printf("PING: %s\n", reply->str);
  freeReplyObject(reply);

  /* Set a key */
  reply = redisCommand(c, "SET %s %s", "foo", "hello world");
  printf("SET: %s\n", reply->str);
  freeReplyObject(reply);

  /* Set a key using binary safe API */
  reply = redisCommand(c, "SET %b %b", "bar", (size_t)3, "hello", (size_t)5);
  printf("SET (binary API): %s\n", reply->str);
  freeReplyObject(reply);

  /* Try a GET and two INCR */
  reply = redisCommand(c, "GET foo");
  printf("GET foo: %s\n", reply->str);
  freeReplyObject(reply);

  reply = redisCommand(c, "INCR counter");
  printf("INCR counter: %lld\n", reply->integer);
  freeReplyObject(reply);
  /* again ... */
  reply = redisCommand(c, "INCR counter");
  printf("INCR counter: %lld\n", reply->integer);
  freeReplyObject(reply);

  /* Create a list of numbers, from 0 to 9 */
  reply = redisCommand(c, "DEL mylist");
  freeReplyObject(reply);
  for (j = 0; j < 10; j++) {
    char buf[64];

    snprintf(buf, 64, "%u", j);
    reply = redisCommand(c, "LPUSH mylist element-%s", buf);
    freeReplyObject(reply);
  }

  /* Let's check what we have inside the list */
  reply = redisCommand(c, "LRANGE mylist 0 -1");
  if (reply->type == REDIS_REPLY_ARRAY) {
    for (j = 0; j < reply->elements; j++) {
      printf("%u) %s\n", j, reply->element[j]->str);
    }
  }
  freeReplyObject(reply);

  /* See function for an example of redisCommandArgv */
  example_argv_command(c, 10);

  /* Disconnects and frees the context */
  redisFree(c);

  return 0;
}
