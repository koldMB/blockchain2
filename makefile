CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

OBJS_APP   = main.o blockchain.o competition.o merkle.o myhash.o
OBJS_TESTS = tests.o blockchain.o competition.o merkle.o myhash.o

all: blockchain tests

blockchain: $(OBJS_APP)
	$(CXX) $(CXXFLAGS) -o $@ $^

tests: $(OBJS_TESTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp blockchain.h myhash.h merkle.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o blockchain tests

.PHONY: all clean
