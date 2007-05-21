SUBVERSION = /usr/include/subversion-1/
APR = /usr/include/apr-1.0/
CFLAGS = -O2 -fpic   -D_REENTRANT -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -ggdb -fomit-frame-pointer -pipe -pthread
LDFLAGS = -O -shared -fpic  
LIBS = -lsvn_client-1 -lsvn_wc-1 -lsvn_ra-1 -lsvn_delta-1 -lsvn_diff-1 -lsvn_subr-1 -laprutil-1 -lexpat -lapr-1 -luuid -lrt -lcrypt -lpthread -ldl -lz


target: luasvn.so

luasvn.so: luasvn.o
	gcc $(LDFLAGS) -o $@ $< $(LIBS) 

luasvn.o: luasvn.c
	gcc $(CFLAGS) -I $(SUBVERSION) -I $(APR) -c -o $@ $<

clean:
	rm luasvn.so luasvn.o
