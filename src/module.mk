OBJS := $(OBJS) fs.o main.o utils.o

fs.o: fs.c fs.h utils.h
main.o: main.c fs.h utils.h
utils.o: utils.c utils.h
