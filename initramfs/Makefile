
TARGET = initramfs.bin

all: $(TARGET)

$(TARGET):
	cd data; find . | cpio -o -H newc > ../$@

clean:
	rm -f $(TARGET)
