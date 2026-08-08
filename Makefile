CC=m68k-amigaos-gcc
CFLAGS=-s -Os

TARGET=robovac
SRC=robovac.c

all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
