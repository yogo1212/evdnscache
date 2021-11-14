NAME = evdnscache

LIBSRCDIR = lib
LIBOBJDIR = lib/obj

APPSRCDIR = app
APPOBJDIR = app/obj

BINDIR = bin

DIRS = $(APPOBJDIR) $(LIBOBJDIR) $(BINDIR)

INCDIR = include

LIBSOURCES = $(wildcard $(LIBSRCDIR)/*.c)
LIBOBJECTS = $(patsubst $(LIBSRCDIR)/%.c,$(LIBOBJDIR)/%.o,$(LIBSOURCES))

APPSOURCES = $(wildcard $(APPSRCDIR)/*.c)
APPOBJECTS = $(patsubst $(APPSRCDIR)/%.c,$(APPOBJDIR)/%.o,$(APPSOURCES))

LIBBIN = $(BINDIR)/lib$(NAME).a
APPBIN = $(BINDIR)/$(NAME)

CFLAGS += -std=gnu99 -pedantic -Wall -Wextra -I$(INCDIR)

ifeq (1,$(DEBUG))
CFLAGS += -g
else
CFLAGS += -O2
endif

CFLAGS += -fPIC
CFLAGS += $(DEFINES)

LIBCFLAGS += $(CFLAGS)

APPCFLAGS += $(CFLAGS)
APPLDFLAGS += -Lbin/ -levdnscache


.PHONY: clean default debug

default: $(LIBBIN) $(APPBIN)

debug:
	export DEBUG=1; "$(MAKE)"

$(LIBOBJECTS): $(LIBOBJDIR)/%.o : $(LIBSRCDIR)/%.c | $(LIBOBJDIR)
	$(CC) -c $< -o $@ $(LIBCFLAGS)

$(LIBBIN): $(LIBOBJECTS) | $(BINDIR)
	ar rcs $@ $^

$(APPOBJECTS): $(APPOBJDIR)/%.o : $(APPSRCDIR)/%.c | $(APPOBJDIR)
	$(CC) -c $< -o $@ $(APPCFLAGS)

$(APPBIN): $(APPOBJECTS) | $(LIBBIN)
	$(CC) -o $@ $^ $(APPLDFLAGS)

$(DIRS):
	mkdir -p $@

clean::
	rm -rf $(DIRS)
