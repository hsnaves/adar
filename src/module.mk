OBJS := $(OBJS) disk.o main.o utils.o

disk.o: disk.c disk.h utils.h
main.o: main.c disk.h utils.h
utils.o: utils.c utils.h
