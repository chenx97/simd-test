ifeq ($(shell uname -m),x86_64)
AVX ?= -DUSE_AVX2
endif
ifneq ($(CROSS),1)
MARCH=-march=native
endif
LDFLAGS += -lpthread -flto -fno-tree-vectorize $(MARCH) $(AVX)

BINARIES = $(patsubst %.c, %, $(wildcard *.c))

all: $(BINARIES)

%: %.c
	$(CC) -O3 -o $@ $< $(LDFLAGS)

clean:
	$(RM) $(BINARIES)
