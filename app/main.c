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

static void reload_resolv_conf(int fd, short events, void *arg)
{
	(void) fd;
	(void) events;
	(void) arg;

	log_info("reload resolv.conf");
	// TODO might want to die if it fails
	dnscache_reload_resolv_conf();
}

static void end_loop(int fd, short events, void *arg)
{
	(void) events;
	struct event_base *base = arg;

	log_info("ending on user request (signal %d)", fd);
	event_base_loopbreak(base);
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

	struct event *sighup_event = evsignal_new(base, SIGHUP, reload_resolv_conf, NULL);
	event_add(sighup_event, NULL);

	struct event *sigint_event = evsignal_new(base, SIGINT, end_loop, base);
	event_add(sig_event, NULL);

	struct event *sigterm_event = evsignal_new(base, SIGTERM, end_loop, base);
	event_add(sig_event, NULL);

	event_base_dispatch(base);

	event_free(sigterm_event);
	event_free(sigint_event);
	event_free(sighup_event);

cleanup_dnscache:
	dnscache_cleanup();

cleanup_base:
	event_base_free(base);

	return 0;
}
