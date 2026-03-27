CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags`
LIBS = `pkg-config fuse3 --libs`
TARGET = mini_unionfs
SRC = mini_unionfs.c

# Test environment directories
TEST_DIR = ./unionfs_test_env
LOWER = $(TEST_DIR)/lower
UPPER = $(TEST_DIR)/upper
MNT = $(TEST_DIR)/mnt

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

# New: Creates the required folder structure and test files
setup:
	mkdir -p $(LOWER) $(UPPER) $(MNT)
	echo "This is base content" > $(LOWER)/base.txt
	@echo "Setup complete. Folders created in $(TEST_DIR)"

# Unmounts the directory if it's stuck, then removes the binary
clean:
	-fusermount3 -u $(MNT) 2>/dev/null || true
	rm -f $(TARGET)
	rm -rf $(TEST_DIR)

.PHONY: all clean setup
