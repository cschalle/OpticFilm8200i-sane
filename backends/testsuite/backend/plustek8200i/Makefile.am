##  Makefile.am -- an automake template for Makefile.in file
##  Copyright (C) 2026  Sane Developers.
##
##  This file is part of the "Sane" build infra-structure.  See
##  included LICENSE file for license information.

TEST_LDADD = \
  ../../../sanei/sanei_init_debug.lo \
  ../../../lib/liblib.la \
  $(MATH_LIB)

check_PROGRAMS = plustek8200i_unit_tests
TESTS = plustek8200i_unit_tests

AM_CPPFLAGS += -I. -I$(srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include \
    -DBACKEND_NAME=plustek8200i

plustek8200i_unit_tests_SOURCES = tests.c
plustek8200i_unit_tests_LDADD = $(TEST_LDADD)
