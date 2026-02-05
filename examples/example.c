#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis.h"

static void example_argv_command(redisContext *c, size_t n) {
  constexpr size_t argv_fixed = 2;
  constexpr size_t tmp_size = 42;
  const size_t argv_count = argv_fixed + n;
  char tmp[tmp_size];
  char **argv = calloc(argv_count, sizeof(*argv));
  size_t *argvlen = calloc(argv_count, sizeof(*argvlen));
  redisReply *reply = nullptr;

  /* We're allocating two additional elements for command and key */
  if (argv == nullptr || argvlen == nullptr) {
    fprintf(stderr, "Error: out of memory\n");
    free(argv);
    free(argvlen);
    exit(EXIT_FAILURE);
  }

  /* First the command */
  argv[0] = (char *)"RPUSH";
  argvlen[0] = sizeof("RPUSH") - 1;

  /* Now our key */
  argv[1] = (char *)"argvlist";
  argvlen[1] = sizeof("argvlist") - 1;

  /* Now add the entries we wish to add to the list */
  for (size_t i = argv_fixed; i < argv_count; i++) {
    argvlen[i] = snprintf(tmp, sizeof(tmp), "argv-element-%zu", i - argv_fixed);
    argv[i] = strdup(tmp);
    if (argv[i] == nullptr) {
      fprintf(stderr, "Error: out of memory\n");
      for (size_t k = argv_fixed; k < i; k++) {
        free(argv[k]);
      }
      free(argv);
      free(argvlen);
      exit(EXIT_FAILURE);
    }
  }

  /* Execute the command using redisCommandArgv.  We're sending the arguments
   * with two explicit arrays.  One for each argument's string, and the other
   * for its length. */
  reply = redisCommandArgv(c, argv_count, (const char **)argv, (const size_t *)argvlen);

  if (reply == nullptr || c->err) {
    fprintf(stderr, "Error:  Couldn't execute redisCommandArgv\n");
    exit(EXIT_FAILURE);
  }

  if (reply->type == REDIS_REPLY_INTEGER) {
    printf("%s reply: %lld\n", argv[0], reply->integer);
  }

  freeReplyObject(reply);

  /* Clean up */
  for (size_t i = argv_fixed; i < argv_count; i++) {
    free(argv[i]);
  }

  free(argv);
  free(argvlen);
}

int main(int argc, char **argv) {
  bool isunix = false;
  redisContext *c = nullptr;
  redisReply *reply = nullptr;
  const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";

  if (argc > 2) {
    if (*argv[2] == 'u' || *argv[2] == 'U') {
      isunix = true;
      /* in this case, host is the path to the unix socket */
      printf("Will connect to unix socket @%s\n", hostname);
    }
  }

  constexpr int default_port = 6'379;
  auto port = (argc > 2) ? atoi(argv[2]) : default_port;

  constexpr struct timeval timeout = {.tv_sec = 1, .tv_usec = 500'000}; // 1.5 seconds
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
    exit(EXIT_FAILURE);
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
  constexpr size_t list_len = 10;
  constexpr size_t buf_size = 64;
  for (size_t j = 0; j < list_len; j++) {
    char buf[buf_size];

    snprintf(buf, sizeof(buf), "%zu", j);
    reply = redisCommand(c, "LPUSH mylist element-%s", buf);
    freeReplyObject(reply);
  }

  /* Let's check what we have inside the list */
  reply = redisCommand(c, "LRANGE mylist 0 -1");
  if (reply->type == REDIS_REPLY_ARRAY) {
    for (size_t j = 0; j < reply->elements; j++) {
      printf("%zu) %s\n", j, reply->element[j]->str);
    }
  }
  freeReplyObject(reply);

  /* See function for an example of redisCommandArgv */
  constexpr size_t argv_entries = 10;
  example_argv_command(c, argv_entries);

  /* Disconnects and frees the context */
  redisFree(c);

  return 0;
}
