CFLAGS = -O2 -Wno-unused-parameter
all: _pheap.so

_pheap.so: module__pheap.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $<
