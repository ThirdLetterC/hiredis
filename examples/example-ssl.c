#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hiredis.h>
#include <hiredis_ssl.h>

int main(int argc, char **argv) {
  redisSSLContextError ssl_error = REDIS_SSL_CTX_NONE;
  redisContext *c = nullptr;
  redisReply *reply = nullptr;
  if (argc < 4) {
    printf("Usage: %s <host> <port> <cert> <key> [ca]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *hostname = (argc > 1) ? argv[1] : "127.0.0.1";
  auto port = atoi(argv[2]);
  const char *cert = argv[3];
  const char *key = argv[4];
  const char *ca = (argc > 4) ? argv[5] : nullptr;

  redisInitOpenSSL();
  auto ssl = redisCreateSSLContext(ca, nullptr, cert, key, nullptr, &ssl_error);
  if (!ssl || ssl_error != REDIS_SSL_CTX_NONE) {
    printf("SSL Context error: %s\n", redisSSLContextGetError(ssl_error));
    exit(EXIT_FAILURE);
  }

  constexpr struct timeval timeout = {.tv_sec = 1, .tv_usec = 500'000}; // 1.5 seconds
  redisOptions options = {0};
  REDIS_OPTIONS_SET_TCP(&options, hostname, port);
  options.connect_timeout = &timeout;
  c = redisConnectWithOptions(&options);

  if (c == nullptr || c->err) {
    if (c) {
      printf("Connection error: %s\n", c->errstr);
      redisFree(c);
    } else {
      printf("Connection error: can't allocate redis context\n");
    }
    exit(EXIT_FAILURE);
  }

  if (redisInitiateSSLWithContext(c, ssl) != REDIS_OK) {
    printf("Couldn't initialize SSL!\n");
    printf("Error: %s\n", c->errstr);
    redisFree(c);
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

  /* Disconnects and frees the context */
  redisFree(c);

  redisFreeSSLContext(ssl);

  return 0;
}
