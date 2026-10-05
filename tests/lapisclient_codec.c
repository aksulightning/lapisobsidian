#include "lapisclient_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool parse(const char *s) {
  LcObject o;
  return lc_parse(s, strlen(s), &o);
}
int main(void) {
  LcObject o;
  const char *valid =
      "{\"type\":\"player_move\",\"x\":-0.125e2,\"onGround\":true}";
  assert(lc_parse(valid, strlen(valid), &o));
  double n;
  bool b;
  int i;
  assert(lc_number(&o, "x", -20, 20, &n) && n == -12.5);
  assert(!lc_integer(&o, "x", -20, 20, &i));
  assert(lc_boolean(&o, "onGround", &b) && b);
  const char *bad[] = {"",
                       "null",
                       "[]",
                       "{\"type\":null}",
                       "{\"x\":NaN}",
                       "{\"x\":1e309}",
                       "{\"x\":01}",
                       "{\"x\":1.}",
                       "{\"x\":1,}",
                       "{\"x\":1,\"x\":2}",
                       "{\"x\":{}}",
                       "{}garbage",
                       "{\"s\":\"\\u0000\"}",
                       "{\"s\":\"\\ud800\"}",
                       "{\"s\":\"\\udc00\"}",
                       "{\"s\":\"\xc0\xaf\"}"};
  for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); j++)
    assert(!parse(bad[j]));
  assert(parse("{\"text\":\"Hello \\ud83e\\udea8\\n\\\"\\\\\"}"));
  assert(parse(" { \"type\" : \"ping\" } \n"));
  /* Every truncated valid message must fail (except whitespace suffix). */
  for (size_t j = 0; j < strlen(valid); j++)
    assert(!lc_parse(valid, j, &o));
  puts("lapisclient codec: schemas, numeric grammar, UTF-8, escapes, duplicate "
       "keys and truncation passed");
}
