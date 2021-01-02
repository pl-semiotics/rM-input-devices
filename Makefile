.PHONY: all clean

CFLAGS += -DREMARKABLE_VERSION=$(REMARKABLE_VERSION)

all: build/librM-input-devices.so build/librM-input-devices-standalone.a build/rM-mk-uinput build/rM-mk-uinput-standalone
clean:
	rm -rf build

build:
	mkdir -p build
build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS)
build/%.a: | build
	$(AR) cr $@ $^
build/%.so: | build
	$(CC) -shared -z defs -o $@ $^ $(LDFLAGS)

build/uinput.bin: | build
	$(OBJCOPY) -I binary -O elf32-littlearm -B arm $(UINPUT_KO) $@

build/rM-input-devices.o: rM-input-devices.h
build/rM-input-devices-standalone.o: build/rM-input-devices.o input-devices-standalone.ld build/uinput.bin
	$(LD) -Tinput-devices-standalone.ld -i -o $@ build/rM-input-devices.o

build/rM-mk-uinput.o: rM-input-devices.h

build/librM-input-devices-standalone.a: build/rM-input-devices-standalone.o

build/librM-input-devices.so: build/rM-input-devices.o
build/librM-input-devices.so: private override LDFLAGS += -ludev -lpthread

build/rM-mk-uinput: build/rM-mk-uinput.o build/librM-input-devices.so | build
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -lrM-input-devices
build/rM-mk-uinput-standalone: build/rM-mk-uinput.o build/librM-input-devices-standalone.a
	$(CC) $(CFLAGS) -o $@ $< -Lbuild -lrM-input-devices-standalone -ludev -lpthread
