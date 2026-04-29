# AT Command Parser Makefile

RAGEL = ragel

.PHONY: all clean
.DEFAULT_GOAL = all

RAGEL_SRC = $(shell find . -name '*.rl')
CXX_SRC = $(RAGEL_SRC:.rl=.rl.cpp)
$(RAGEL_SRC): ;

%.rl.cpp: %.rl
	@$(RAGEL) $< -o $@

clean:
	@rm -f *.rl.cpp

all: $(CXX_SRC)
