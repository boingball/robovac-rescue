CC=m68k-amigaos-gcc
CFLAGS=-s -Os

TARGET=robovac
SRC=robovac.c

GD_TARGET=geodash_opt
GD_SRC=geodash_opt.c
GD_CFLAGS=-m68020 -O2 -fno-builtin -noixemul

all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

geodash_opt:
	$(CC) $(GD_CFLAGS) -o $(GD_TARGET) $(GD_SRC)

clean:
	rm -f $(TARGET) $(GD_TARGET)
