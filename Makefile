CFLAGS = -O2 -Wno-unused-parameter
all: _pheap.so

ifeq (Darwin,$(shell uname -s))
  CFLAGS += -undefined dynamic_lookup -DKRK_MEDIOCRE_TLS
endif

_pheap.so: module__pheap.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<
