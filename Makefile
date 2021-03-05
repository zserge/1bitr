CFLAGS += -std=c99 -pedantic -Wall -Wextra -Wno-missing-field-initializers -g -O3

ifeq ($(OS),Windows_NT)
	# TODO
else
ifeq ($(shell uname -s),Linux)
	LDLIBS += -lasound
endif
ifeq ($(shell uname -s),Darwin)
	LDFLAGS += -framework CoreAudio
endif
endif

all: 1bitr

1bitr: 1bitr.c

clean:
	$(RM) 1bitr main.o

.PHONY: all clean
