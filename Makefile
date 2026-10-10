CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -pedantic
ifeq ($(OS),Windows_NT)
TARGET = cnproxy.exe
LIBS = -lws2_32
else
TARGET = cnproxy
LIBS = -pthread
endif

all: $(TARGET)
$(TARGET): src/proxy.c src/platform.h
	$(CC) $(CFLAGS) src/proxy.c -o $(TARGET) $(LIBS)
test: all
	python tests/test_proxy.py --binary ./$(TARGET)
benchmark: all
	python tests/benchmark.py --binary ./$(TARGET)
clean:
	$(RM) cnproxy cnproxy.exe
.PHONY: all test benchmark clean
