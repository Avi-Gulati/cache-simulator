PROGRAMS = main
all: $(PROGRAMS)

ALLPROGRAMS = $(PROGRAMS)

include rules.mk

INCDIR = include
SRCDIR = src
OBJDIR = obj

INC_PARAMS=$(foreach d, $(INCDIR), -I$d)
CFLAGS += $(INC_PARAMS) -mavx2 -mfma

SRC = $(wildcard $(SRCDIR)/*.c)
OBJ = $(patsubst $(SRCDIR)/%,$(OBJDIR)/%,$(SRC:.c=.o))


$(OBJDIR)/%.o: $(SRCDIR)/%.c $(BUILDSTAMP)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(O) $(DEPCFLAGS) -o $@ -c $<

main: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(O) -o $@ $^

clean:
	rm -rf $(ALLPROGRAMS) $(OBJDIR) $(DEPSDIR)

format:
	clang-format -i $(SRC) $(wildcard $(INCDIR)/*.hh)

.PHONY: all clean format
