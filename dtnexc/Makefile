# Makefile for DTNEXC - C implementation of DTNEX
# Based on the bash script by Samo Grasic (samo@grasic.net)

# ION/BP include and library paths
# Using the local ION code for headers - switching to BPv7
ION_INCDIR=../ione-code
ION_LIBDIR=/usr/local/lib

# Compiler and flags
CC=gcc
CFLAGS=-Wall -g -I$(ION_INCDIR)/bpv7/include -I$(ION_INCDIR)/ici/include
LDFLAGS=-L$(ION_LIBDIR) -lbp -lici -lm -lpthread -lcrypto

# Targets
TARGET=dtnexc
SOURCES=dtnexc.c
OBJECTS=$(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin

.PHONY: all clean install