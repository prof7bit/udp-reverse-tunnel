service     ?= localhost:51820
outside     ?= jump.example.com:51820
listen      ?= 51820
prefix      ?= /usr/local

name        = udp-tunnel
objs        = main.o connlist.o args.o sha-256.o mac.o misc.o main-inside.o main-outside.o
deps        = $(patsubst %.o,%.d,$(objs))
cflags      = -O3 -flto -Wall -Wextra
unit_dir    = /etc/systemd/system

all: $(name)

clean:
	rm -f $(name) $(objs) $(deps)

install:
	install -m 755 udp-tunnel $(prefix)/bin/

install-inside: install
	install -m 644 udp-tunnel-inside.service $(unit_dir)/
	sed -ie 's/{{SERVICE}}/$(service)/g' $(unit_dir)/udp-tunnel-inside.service
	sed -ie 's/{{OUTSIDE}}/$(outside)/g' $(unit_dir)/udp-tunnel-inside.service
	systemctl daemon-reload

install-outside: install
	install -m 644 udp-tunnel-outside.service $(unit_dir)/
	sed -i 's/{{LISTEN}}/$(listen)/g' $(unit_dir)/udp-tunnel-outside.service
	systemctl daemon-reload

uninstall:
	-systemctl stop udp-tunnel-outside.service
	-systemctl stop udp-tunnel-inside.service
	rm -f $(unit_dir)/udp-tunnel-outside.service
	rm -f $(unit_dir)/udp-tunnel-inside.service
	rm -f $(prefix)/bin/udp-tunnel
	systemctl daemon-reload

# compile the modules
%.o: %.c $(HEADERS)
	$(CC) -MMD $(cflags) -c $< -o $@

# link the executable
$(name): $(objs)
	$(CC) -o $@ $(cflags) $(LFLAGS) $^

# also depend on changes in the makefile
$(objs): Makefile

# try to include compiler generated dependency makefile snippets *.d
-include $(deps)
