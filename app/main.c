#include <evdnscache.h>
#include <event2/event.h>

#include "log.h"

int main(int argc, char *argv[])
{
	struct event_base *base = event_base_new();

	if (!dnscache_init(base)) {
		log_error("dnscache_init ouch");
		goto cleanup_base;
	}

	for (int i = 0; i < argc; i++) {
		log_info("%s", argv[i]);
	}

	event_base_dispatch(base);

cleanup_base:
	event_base_free(base);

	return 0;
}
