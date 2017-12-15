SRC := $(wildcard *.c)
OBJS := $(SRC:.c=.o)
all : $(OBJS) 
.PHONY : all
$(OBJS) : %.o : %.c
	$(CC) $< -o $@

.PHONY : clean
clean :
	-rm $(OBJS)
