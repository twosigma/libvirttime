CFLAGS  += -g -O2 -fPIC -pie -Wall -fvisibility=hidden
LDFLAGS += -fPIC -pie -shared -ldl -lpthread

DEPS    = $(wildcard src/*.h)
SOURCES = $(wildcard src/*.c)
OBJECTS = $(SOURCES:.c=.o)
TARGET  = libvirttime.so

$(TARGET): $(OBJECTS) $(DEPS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

%.o: %.c $(DEPS)
	$(CC) -c $(CFLAGS) $< -o $@

all: $(SOURCES) $(TARGET)

.PHONY: clean

clean:
	rm -f $(OBJECTS) $(TARGET)
