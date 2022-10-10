NAME = udp-tunnel
OBJS = main.o connlist.o args.o
DEPS = $(patsubst %.o,%.d,$(OBJS))

CFLAGS = -O3 -flto -Wall -Wextra

all: $(NAME)

clean:
	rm -f $(NAME) $(OBJS) $(DEPS)

# compile the modules
$(BUILDDIR)%.o: %.c $(HEADERS)
	$(CC) -MMD $(CFLAGS) -c $< -o $@

# link the executable
$(NAME): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(LFLAGS) $^

# also depend on changes in the makefile
$(OBJS): Makefile

# try to include compiler generated dependency makefile snippets *.d
-include $(DEPS)
