#ifdef LAPISCLIENT
#include "lapisclient_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
/* Reject invalid UTF-8, embedded NUL, overlong encodings and surrogate scalars.
 */
bool lc_utf8(const unsigned char *s, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned c = s[i++], min = 0, k = 0;
    if (c < 128) {
      if (!c)
        return false;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      k = 1;
      min = 0x80;
      c &= 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      k = 2;
      min = 0x800;
      c &= 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      k = 3;
      min = 0x10000;
      c &= 7;
    } else
      return false;
    if (k > n - i)
      return false;
    while (k--) {
      unsigned b = s[i++];
      if ((b & 192) != 128)
        return false;
      c = (c << 6) | (b & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
  }
  return true;
}
static void ws(const char **p, const char *end) {
  while (*p < end && strchr(" \t\r\n", **p))
    ++*p;
}
static int hex(const char **p, const char *end) {
  if (end - *p < 4)
    return -1;
  int v = 0;
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)*(*p)++;
    int d = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                   : -1;
    if (d < 0)
      return -1;
    v = v * 16 + d;
  }
  return v;
}
static bool string(const char **p, const char *end, char *out,
                   size_t capacity) {
  if (*p == end || *(*p)++ != '"')
    return false;
  size_t n = 0;
  while (*p < end && **p != '"') {
    unsigned c = (unsigned char)*(*p)++;
    if (c < 32)
      return false;
    if (c == '\\') {
      if (*p == end)
        return false;
      c = (unsigned char)*(*p)++;
      if (c == 'u') {
        int h = hex(p, end);
        if (h <= 0)
          return false;
        c = (unsigned)h;
        if (c >= 0xd800 && c <= 0xdbff) {
          if (end - *p < 6 || (*p)[0] != '\\' || (*p)[1] != 'u')
            return false;
          *p += 2;
          h = hex(p, end);
          if (h < 0xdc00 || h > 0xdfff)
            return false;
          c = 0x10000 + ((c - 0xd800) << 10) + (unsigned)(h - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff)
          return false;
        if (n + 4 >= capacity)
          return false;
        if (c < 128)
          out[n++] = (char)c;
        else if (c < 2048) {
          out[n++] = (char)(192 | (c >> 6));
          out[n++] = (char)(128 | (c & 63));
        } else if (c < 65536) {
          out[n++] = (char)(224 | (c >> 12));
          out[n++] = (char)(128 | ((c >> 6) & 63));
          out[n++] = (char)(128 | (c & 63));
        } else {
          out[n++] = (char)(240 | (c >> 18));
          out[n++] = (char)(128 | ((c >> 12) & 63));
          out[n++] = (char)(128 | ((c >> 6) & 63));
          out[n++] = (char)(128 | (c & 63));
        }
        continue;
      }
      switch (c) {
      case '"':
      case '\\':
      case '/':
        break;
      case 'b':
        c = 8;
        break;
      case 'f':
        c = 12;
        break;
      case 'n':
        c = 10;
        break;
      case 'r':
        c = 13;
        break;
      case 't':
        c = 9;
        break;
      default:
        return false;
      }
    }
    if (n + 1 >= capacity)
      return false;
    out[n++] = (char)c;
  }
  if (*p == end)
    return false;
  ++*p;
  out[n] = 0;
  return lc_utf8((const unsigned char *)out, n);
}
bool lc_parse(const char *data, size_t size, LcObject *out) {
  const char *p = data, *end = data + size;
  out->count = 0;
  if (!lc_utf8((const unsigned char *)data, size))
    return false;
  ws(&p, end);
  if (p == end || *p++ != '{')
    return false;
  ws(&p, end);
  if (p < end && *p == '}') {
    p++;
    ws(&p, end);
    return p == end;
  }
  while (p < end && out->count < 20) {
    LcField *f = &out->fields[out->count];
    memset(f, 0, sizeof(*f));
    if (!string(&p, end, f->key, sizeof(f->key)) || lc_field(out, f->key))
      return false;
    ws(&p, end);
    if (p == end || *p++ != ':')
      return false;
    ws(&p, end);
    if (p == end)
      return false;
    if (*p == '"') {
      f->kind = 's';
      if (!string(&p, end, f->text, sizeof(f->text)))
        return false;
    } else if (end - p >= 4 && !memcmp(p, "true", 4)) {
      f->kind = 'b';
      f->number = 1;
      p += 4;
    } else if (end - p >= 5 && !memcmp(p, "false", 5)) {
      f->kind = 'b';
      p += 5;
    } else {
      const char *start = p;
      if (*p == '-')
        p++;
      if (p == end || !isdigit((unsigned char)*p))
        return false;
      if (*p == '0')
        p++;
      else
        while (p < end && isdigit((unsigned char)*p))
          p++;
      if (p < end && *p == '.') {
        p++;
        if (p == end || !isdigit((unsigned char)*p))
          return false;
        while (p < end && isdigit((unsigned char)*p))
          p++;
      }
      if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-'))
          p++;
        if (p == end || !isdigit((unsigned char)*p))
          return false;
        while (p < end && isdigit((unsigned char)*p))
          p++;
      }
      size_t n = (size_t)(p - start);
      if (n >= 64)
        return false;
      char num[64];
      memcpy(num, start, n);
      num[n] = 0;
      f->number = strtod(num, NULL);
      f->kind = 'n';
      if (!isfinite(f->number))
        return false;
    }
    out->count++;
    ws(&p, end);
    if (p == end)
      return false;
    if (*p == '}') {
      p++;
      ws(&p, end);
      return p == end;
    }
    if (*p++ != ',')
      return false;
    ws(&p, end);
  }
  return false;
}
const LcField *lc_field(const LcObject *o, const char *key) {
  for (unsigned i = 0; i < o->count; i++)
    if (!strcmp(o->fields[i].key, key))
      return &o->fields[i];
  return NULL;
}
const char *lc_string(const LcObject *o, const char *key) {
  const LcField *f = lc_field(o, key);
  return f && f->kind == 's' ? f->text : NULL;
}
bool lc_number(const LcObject *o, const char *key, double min, double max,
               double *out) {
  const LcField *f = lc_field(o, key);
  if (!f || f->kind != 'n' || f->number < min || f->number > max)
    return false;
  *out = f->number;
  return true;
}
bool lc_integer(const LcObject *o, const char *key, int min, int max,
                int *out) {
  double n;
  if (!lc_number(o, key, min, max, &n) || floor(n) != n)
    return false;
  *out = (int)n;
  return true;
}
bool lc_boolean(const LcObject *o, const char *key, bool *out) {
  const LcField *f = lc_field(o, key);
  if (!f || f->kind != 'b')
    return false;
  *out = f->number != 0;
  return true;
}
#endif
