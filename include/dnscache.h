#ifndef DNSCACHE_H
#define DNSCACHE_H

#include <event2/event.h>
#include <stdbool.h>

typedef void (*dnscache_add_cb)(const char *hostname, const char *value, void *ctx);
typedef void (*dnscache_expire_cb)(const char *hostname, const char *value, void *ctx);

bool dnscache_init(struct event_base *base);
void dnscache_cleanup(void);

bool dnscache_add(const char *name, dnscache_add_cb add_cb, dnscache_expire_cb expire_cb, void *ctx);

#endif
