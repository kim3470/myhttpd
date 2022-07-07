CXX = g++ -fPIC -lrt
NETLIBS= -lnsl -lpthread -ldl
CC = gcc

all: git-commit myhttpd daytime-server use-dlopen hello.so jj-mod.so

daytime-server : daytime-server.o
	$(CXX) -o $@ $@.o $(NETLIBS)

myhttpd : myhttpd.o
	$(CXX) -o $@ $@.o $(NETLIBS)

use-dlopen: use-dlopen.o
	$(CXX) -o $@ $@.o $(NETLIBS) -ldl

jj-mod.o: jj-mod.c
	$(CC) -g -fPIC -c $<

util.o: util.c
	$(CC) -c util.c

hello.so: hello.o
	ld -G -o ./http-root-dir/cgi-bin/$@ $^

jj-mod.so: jj-mod.o util.o
	ld -G -g -o ./http-root-dir/cgi-bin/$@ $^

%.o: %.cc
	@echo 'Building $@ from $<'
	$(CXX) -g -o $@ -c -I. $<

.PHONY: git-commit
git-commit:
	git checkout
	git add *.cc *.h Makefile >> .local.git.out  || echo
	git commit -a -m 'Commit' >> .local.git.out || echo
	git push origin master 

.PHONY: clean
clean:
	rm -f *.o use-dlopen hello.so
	rm -f *.o daytime-server myhttpd

