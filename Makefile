AR=ar
CXX=g++
CXXFLAGS=-O2 -Wall -Wextra  -fPIC 
INCLUDE=-I. -I../../include
LIBS= 

incdir = ../../include
libdir = ../../lib

incs = clogl.h
libs = libclogl.a

objs = ./var/clogl.o


all: lib install

clean:
	rm -rf $(libs)
	rm -rf ./var

install: $(incs) $(libs)
	mkdir -p $(incdir)
	mkdir -p $(libdir)
	cp $(incs) $(incdir)/
	cp $(libs) $(libdir)/

uninstall:
	@for i in $(incs); do \
		(echo "rm -f $(incdir)/$$i"); \
		(rm -f $(incdir)/$$i); \
	done
	
	@for l in $(libs); do \
		(echo "rm -f $(libdir)/$$l"); \
		(rm -f $(libdir)/$$l); \
	done

lib: $(libs)

####################################################
$(libs): %: $(objs)
	$(AR) -rs $@ $^

$(objs): ./var/%.o: %.c %.h
	mkdir -p var
	$(CXX) $(CXXFLAGS) -c -o $@ $< $(INCLUDE)

