BIN_DIR = bin
INC_DIR = include
SRC_DIR = src
TEST_DIR = test

CC = gcc
CCFLAGS = -std=gnu99 -Wall -O0 -g -DNDEBUG -pthread -ggdb -I$(INC_DIR)
LDFLAGS = -pthread -pthread

LDLIBUV = -luv -Wl,-rpath=/usr/local/lib

COMM_FILES = $(SRC_DIR)/utils.c
COMM_FILES += $(SRC_DIR)/server.c

EXECUTABLES = 	sequential-server \
				thread-server \
				threadpool-server \
				threadpool-test \
				blocking-listener \
				nonblocking-listener \
				select-server

all: $(EXECUTABLES)

sequential-server: $(COMM_FILES) $(SRC_DIR)/sequential-server.c
	mkdir -p $(BIN_DIR)
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

thread-server: $(COMM_FILES) $(SRC_DIR)/thread-server.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)


threadpool-server: $(COMM_FILES) $(SRC_DIR)/thread-pool.c $(SRC_DIR)/threadpool-server.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

threadpool-test: $(COMM_FILES) $(SRC_DIR)/thread-pool.c $(TEST_DIR)/threadpool-test.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)


blocking-listener: $(COMM_FILES) $(SRC_DIR)/blocking-listener.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

nonblocking-listener: $(COMM_FILES) $(SRC_DIR)/nonblocking-listener.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

select-server: $(COMM_FILES) $(SRC_DIR)/select-server.c
	$(CC) $(CCFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS)

.PHONY: clean format

clean:
	rm -rf $(BIN_DIR)/*

format:
	clang-format -style=file -i *.c *.h