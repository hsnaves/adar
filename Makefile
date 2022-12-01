# makefile for adar

all: build

build:
	$(MAKE) -C src

clean:
	$(MAKE) -C src clean

.PHONY: all build clean
