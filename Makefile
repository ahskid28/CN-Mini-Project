CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic
ifeq ($(OS),Windows_NT)
EXT = .exe
LIBS = -lws2_32
else
EXT =
LIBS = -pthread
endif
PROGRAMS = cnproxy$(EXT) origin$(EXT) test_proxy$(EXT) cnbenchmark$(EXT)
all: $(PROGRAMS)
cnproxy$(EXT): src/proxy.c src/platform.h
	$(CC) $(CFLAGS) src/proxy.c -o $@ $(LIBS)
origin$(EXT): tests/origin.c src/platform.h
	$(CC) $(CFLAGS) tests/origin.c -o $@ $(LIBS)
test_proxy$(EXT): tests/test_proxy.c tests/lab.h tests/clients.h src/platform.h
	$(CC) $(CFLAGS) tests/test_proxy.c -o $@ $(LIBS)
cnbenchmark$(EXT): tests/benchmark.c tests/lab.h tests/clients.h src/platform.h
	$(CC) $(CFLAGS) tests/benchmark.c -o $@ $(LIBS)
test: all
	./test_proxy$(EXT) ./cnproxy$(EXT) ./origin$(EXT)
benchmark: all
	./cnbenchmark$(EXT) ./cnproxy$(EXT) ./origin$(EXT) benchmark-results.json
clean:
	$(RM) $(PROGRAMS)
.PHONY: all test benchmark clean

