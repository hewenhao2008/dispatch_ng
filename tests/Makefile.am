
common_cflags = $(EVENT_CFLAGS) -I$(top_srcdir)
common_libadd = ../src/libdispatch.a $(EVENT_LIBS)


check_LIBRARIES = libtest.a
libtest_a_SOURCES = libtest.c libtest.h
libtest_a_CFLAGS = $(common_cflags)
#libtest_a_LIBADD = $(common_libadd)

check_PROGRAMS = \
	utils \
	network \
	balancer \
	setup \
	test-ipv4 \
	test-ipv6 \
	test-domain \
	test-domain-ipv6

TESTS = $(check_PROGRAMS)
AM_CFLAGS = $(common_cflags)
LDADD = libtest.a $(common_libadd)

EXTRA_DIST = valgrind-suppressions

