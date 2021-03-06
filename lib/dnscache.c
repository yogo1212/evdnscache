#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/dns.h>
#include <stdlib.h>
#include <uthash.h>

#include "evdnscache.h"
#include "log.h"
#include "str.h"

// TODO libevent does round-robin when there are multiple DNS servers

// TODO handle IPv6 as well
struct ipv4_entry;
typedef struct ipv4_entry ipv4_entry_t;

typedef void (*ipv4_entry_expire_func)(ipv4_entry_t *ipv4, void *ctx);
struct ipv4_entry {
	struct event *evt;
	struct in_addr ina;

	ipv4_entry_expire_func expire_func;
	void *ctx;

	UT_hash_handle hh;
};

#define MAX_HOSTNAME_LEN_WITH_ZT (253 + 1) // DNS limit

typedef struct {
	char hostname[MAX_HOSTNAME_LEN_WITH_ZT];

	ipv4_entry_t *ipv4;
	struct event *evt;
	struct evdns_request *active_request;

	dnscache_add_cb add_cb;
	dnscache_expire_cb expire_cb;
	void *ctx;

	UT_hash_handle hh;
} dnscache_entry_t;

// TODO try to reduce vars in global scope

struct {
	struct event_base *evb;
	struct evdns_base *edb;

	dnscache_entry_t *url_list;
} dnscache;

static void ipv4_time_out(evutil_socket_t fd, short what, void *ctx)
{
	(void) fd;
	(void) what;

	ipv4_entry_t *ipv4 = ctx;
	ipv4->expire_func(ipv4, ipv4->ctx);
}

static ipv4_entry_t *ipv4_entry_create(struct in_addr *ina, ipv4_entry_expire_func expire_func, void *ctx)
{
	ipv4_entry_t *ipv4 = malloc(sizeof(ipv4_entry_t));
	ipv4->expire_func = expire_func;
	ipv4->ctx = ctx;
	memcpy(&ipv4->ina, ina, sizeof(ipv4->ina));
	ipv4->evt = event_new(dnscache.evb, -1, EV_TIMEOUT, ipv4_time_out, ipv4);
	return ipv4;
}

static void ipv4_entry_reset(ipv4_entry_t *ipv4, size_t ttl)
{
	struct timeval tv = { ttl, 0 };
	event_add(ipv4->evt, &tv);
}

static void ipv4_entry_free(ipv4_entry_t *ipv4)
{
	event_free(ipv4->evt);
	free(ipv4);
}

static void _expire_ipv4(ipv4_entry_t *ipv4, void *ctx)
{
	dnscache_entry_t *d = ctx;

	if (d->expire_cb) {
		char str_ip[INET_ADDRSTRLEN + 1];
		if (!copy_string(str_ip, inet_ntoa(ipv4->ina), sizeof(str_ip))) {
			log_error("ip truncated");
			return;
		}

		d->expire_cb(d->hostname, str_ip, d->ctx);
	}

	HASH_DEL(d->ipv4, ipv4);
	ipv4_entry_free(ipv4);
}

static void dnscache_entry_schedule(dnscache_entry_t *d)
{
	struct timeval tv = { 3, 0 };
	event_add(d->evt, &tv);
}

static void dnscache_entry_dns_cb(int err, char type, int count, int ttl, void *addresses, void *arg)
{
	dnscache_entry_t *d = arg;

	d->active_request = NULL;

	if (err != DNS_ERR_NONE) {
		log_error("dnscache for %s: %s", d->hostname, evdns_err_to_string(err));
		goto retry;
	}

	if (type != DNS_IPv4_A) {
		log_error("dnscache type %d :-(", type);
		goto retry;
	}

	struct timeval tv = { ttl > 1 ? ttl * 0.8 : 1, 0 };
	event_add(d->evt, &tv);

	struct in_addr *ina = addresses;

	ipv4_entry_t *ipv4;
	for (size_t i = 0; i < (size_t) count; i++) {
		HASH_FIND(hh, d->ipv4, &ina[i], sizeof(ina[i]), ipv4);;
		if (ipv4) {
			ipv4_entry_reset(ipv4, ttl);
			continue;
		}

		ipv4 = ipv4_entry_create(&ina[i], _expire_ipv4, d);
		HASH_ADD(hh, d->ipv4, ina, sizeof(ipv4->ina), ipv4);

		if (d->add_cb) {
			char str_ip[INET_ADDRSTRLEN + 1];
			if (!copy_string(str_ip, inet_ntoa(ipv4->ina), sizeof(str_ip))) {
				log_error("ip truncated");
				continue;
			}

			d->add_cb(d->hostname, str_ip, d->ctx);
		}

		ipv4_entry_reset(ipv4, ttl);
	}

	return;

retry:
	dnscache_entry_schedule(d);
}

static void dnscache_entry_refresh(dnscache_entry_t *d)
{
	if (d->active_request) {
		log_info("refresh d->active_request");
		return;
	}

	d->active_request = evdns_base_resolve_ipv4(dnscache.edb, d->hostname, 0, dnscache_entry_dns_cb, d);
	if (!d->active_request) {
		log_error("evdns_base_resolve_ipv4 %s", d->hostname);
		dnscache_entry_schedule(d);
	}
}
static void _dnscache_entry_refresh(evutil_socket_t fd, short what, void *arg)
{
	(void) fd;
	(void) what;

	dnscache_entry_t *d = arg;
	// TODO re-think this event
	// only add for deferring requests?
	dnscache_entry_refresh(d);
}

static dnscache_entry_t *dnscache_entry_new(const char *hostname, dnscache_add_cb add_cb, dnscache_expire_cb expire_cb, void *ctx)
{
	dnscache_entry_t *d = malloc(sizeof(dnscache_entry_t));
	memcpy(d->hostname, hostname, sizeof(d->hostname));

	d->ipv4 = NULL;
	d->active_request = NULL;

	d->evt = event_new(dnscache.evb, -1, EV_TIMEOUT, _dnscache_entry_refresh, d);

	d->add_cb = add_cb;
	d->expire_cb = expire_cb;
	d->ctx = ctx;

	dnscache_entry_refresh(d);
	return d;
}

static void dnscache_entry_free(dnscache_entry_t *d)
{
	if (d->active_request)
		evdns_cancel_request(dnscache.edb, d->active_request);

	ipv4_entry_t *i, *tmp;
	HASH_ITER(hh, d->ipv4, i, tmp) {
		HASH_DEL(d->ipv4, i);
		ipv4_entry_free(i);
	}

	event_free(d->evt);
	free(d);
}

bool dnscache_add(const char *name, dnscache_add_cb add_cb, dnscache_expire_cb expire_cb, void *ctx)
{
	char hostname[MAX_HOSTNAME_LEN_WITH_ZT];
	if (!copy_string(hostname, name, sizeof(hostname)))
		return false;

	dnscache_entry_t *d;
	HASH_FIND_STR(dnscache.url_list, hostname, d);
	if (d)
		return false;

	d = dnscache_entry_new(hostname, add_cb, expire_cb, ctx);
	HASH_ADD_STR(dnscache.url_list, hostname, d);

	return true;
}

bool dnscache_reload_resolv_conf(void)
{
	const char *resolv_conf = getenv("DNSCACHE_RESOLV_CONF");
	if (!resolv_conf)
		resolv_conf = "/etc/resolv.conf";

	if (evdns_base_clear_nameservers_and_suspend(dnscache.edb) == -1) {
		log_error("couldn't halt name resolution");
		return false;
	}

	int res = evdns_base_resolv_conf_parse(dnscache.edb, DNS_OPTIONS_ALL, resolv_conf);
	if (res) {
		log_error("couldn't load resolv.conf \"%s\": %d", resolv_conf, res);
		return false;
	}

	if (evdns_base_resume(dnscache.edb) == -1) {
		log_error("couldn't resume name resolution");
		return false;
	}

	return true;
}

bool dnscache_init(struct event_base *base)
{
	dnscache.evb = base;

	dnscache.edb = evdns_base_new(base, 0);
	if (!dnscache.edb) {
		log_error("!evdns_base_new");
		return false;
	}

	if (!dnscache_reload_resolv_conf()) {
		goto ouch_base;
	}

	dnscache.url_list = NULL;

	return true;

ouch_base:
	evdns_base_free(dnscache.edb, 1);
	return false;
}

void dnscache_cleanup(void)
{
	dnscache_entry_t *d, *tmp;
	HASH_ITER(hh, dnscache.url_list, d, tmp) {
		HASH_DEL(dnscache.url_list, d);
		dnscache_entry_free(d);
	}

	evdns_base_free(dnscache.edb, 1);
}
