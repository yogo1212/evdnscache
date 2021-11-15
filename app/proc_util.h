#ifndef PROC_UTIL_H
#define PROC_UTIL_H

#include <stdbool.h>

bool fork_and_wait_for_stdout(const char *handler, ...);

#endif
