#include <evdnscache.h>
#include <event2/event.h>
#include <stdarg.h>
#include <stdlib.h>

#include "log.h"
#include "proc_util.h"

#define RECORD_TYPE_ENV "RECORD_TYPE"

static void _expire_cb(const char *hostname, const char *value, void *ctx)
{
	const char *handler = ctx;

	//DNS_IPv4_A
	setenv(RECORD_TYPE_ENV, "A", true);
	fork_and_wait_for_stdout(handler, "expire", hostname, value, NULL);
	unsetenv(RECORD_TYPE_ENV);
}

static void _add_cb(const char *hostname, const char *value, void *ctx)
{
	const char *handler = ctx;

	//DNS_IPv4_A
	setenv(RECORD_TYPE_ENV, "A", true);
	fork_and_wait_for_stdout(handler, "add", hostname, value, NULL);
	unsetenv(RECORD_TYPE_ENV);
}

int main(int argc, char *argv[])
{
	struct event_base *base = event_base_new();

	if (!dnscache_init(base)) {
		log_error("dnscache_init ouch");
		goto cleanup_base;
	}

	const char *change_handler = getenv("HANDLER");
	if (!change_handler)
		change_handler = "dnscache_change_handler";

	if (!fork_and_wait_for_stdout(change_handler, "init", NULL)) {
		log_error("couldn't call handler \"%s\"", change_handler);
		goto cleanup_base;
	}

	for (int i = 1; i < argc; i++) {
		const char *host = argv[i];
		if (!dnscache_add(host, _add_cb, _expire_cb, (void *) change_handler)) {
			log_error("dnscache_add %s ouch", host);
			goto cleanup_dnscache;
		}
	}

	event_base_dispatch(base);

cleanup_dnscache:
	dnscache_cleanup();

cleanup_base:
	event_base_free(base);

	return 0;
}
