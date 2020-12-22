ODIR := ./objs
OBJS := $(patsubst %.c,$(ODIR)/%.o,$(wildcard *.c))

CFLAGS := -c -O2 -Wall -Wextra
LFALGS := 

CC := gcc

BIN := rename-ordered

all : $(BIN)

$(BIN) : $(OBJS)
	$(CC) $^ $(LFLAGS) -o $@

$(ODIR)/%.o : %.c | $(ODIR)
	$(CC) $(CFLAGS) -o $@ $<

$(ODIR) :
	mkdir $(ODIR)

.PHONY : clean
clean :
	rm -rf $(ODIR)
