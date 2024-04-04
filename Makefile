CFLAGS = -Wall -Wextra -pedantic
CXXFLAGS = -Wall -Wextra -pedantic -Wno-unused-parameter

subproc.o: subproc.c include/snaketongs_subproc.h entry.py.str.h Makefile
	# compiling $< into $@
	$(CC) $(CFLAGS) -c $< -o $@

entry.py.str.h: entry.py Makefile
	# preprocessing $< for inclusion in subproc.c
	sed 's/\s*#.*//;s/.*/"\0\\n"/' $< > $@

test: test.cpp subproc.o include/snaketongs.hpp include/snaketongs_subproc.h Makefile
	# compiling $< into $@
	$(CXX) -I include -std=c++20 $(CXXFLAGS) $< subproc.o -o $@
	# running tests
	./$@

clean: Makefile
	# cleaning files ignored by git, skipping directories
	git clean -fX
