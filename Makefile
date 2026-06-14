CC = cc

ARCH_FLAGS ?= -march=native

CFLAGS = -std=c11 -O3 $(ARCH_FLAGS) \
         -Wall -Wextra \
         -Wno-unused-parameter \
         -Wno-sign-compare \
         -Wno-unused-function \
         -Wno-misleading-indentation \
         -flto \
         -funroll-loops \
         -fomit-frame-pointer \
         -Isrc

SRCS = src/fearena/arena.c \
       src/felexer/lexer.c \
       src/felexer/token.c \
       src/feparser/parser.c \
       src/fehir/hir.c \
       src/fetc/tc.c \
       src/fesema/sema.c \
       src/fecodegen/fir.c \
       src/fecodegen/codegen.c \
       src/feopt/opt.c \
       src/fellvm/llvm_emit.c \
       src/main.c

OBJS = $(SRCS:.c=.o)

BIN = fermi

ifeq ($(shell uname -o 2>/dev/null),Android)
PREFIX ?= /data/data/com.termux/files/usr
else
PREFIX ?= /usr/local
endif
BINDIR = $(PREFIX)/bin


all: install


$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	strip $@ 2>/dev/null || true


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


build: $(BIN)


install: build
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	chmod 755 $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "Installed $(BIN) -> $(DESTDIR)$(BINDIR)/$(BIN)"


test: build
	@sh test.sh


bench: build
	@echo "=== Hyperfine benchmark (target: <3ms) ==="
	@if command -v hyperfine >/dev/null 2>&1; then \
		hyperfine --warmup 20 --runs 200 './$(BIN) --fir /tmp/fermi_bench.fe' 2>&1; \
	else \
		echo "hyperfine not found. Shell timing (100 runs):"; \
		sh -c 'TIMEFORMAT="%R s for 100 runs"; time for i in $$(seq 100); do ./$(BIN) --fir /tmp/fermi_bench.fe >/dev/null 2>&1; done'; \
	fi


uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@echo "Removed $(DESTDIR)$(BINDIR)/$(BIN)"


clean:
	rm -f $(OBJS) $(BIN)


reinstall: uninstall install


.PHONY: all build install uninstall clean reinstall test bench
