include $(top_srcdir)/test-templates/Makefile.decl
SUBDIRS = . test
TEST_SUBDIRS = test

lib_LTLIBRARIES = libgnc-qof.la

libgnc_qof_la_LDFLAGS= -version-info $(LIBQOF_LIBRARY_VERSION)
libgnc_qof_common_libs =  \
  $(GLIB_LIBS) \
  $(REGEX_LIBS) \
  $(top_builddir)/lib/libc/libc-missing.la
libgnc_qof_la_LIBADD = $(libgnc_qof_common_libs)

AM_CPPFLAGS = \
  -I$(top_srcdir)/lib/libc \
  -I$(top_srcdir)/src \
  $(GLIB_CFLAGS)

libgnc_qof_la_SOURCES =  \
   gnc-date.cpp        \
   gnc-numeric.cpp   \
   guid.cpp            \
   kvp-util.cpp        \
   kvp_frame.cpp       \
   qofbackend.cpp      \
   qofbook.cpp         \
   qofchoice.cpp       \
   qofclass.cpp        \
   qofevent.cpp        \
   qofid.cpp           \
   qofinstance.cpp     \
   qoflog.cpp          \
   qofobject.cpp       \
   qofquery.cpp        \
   qofquerycore.cpp    \
   qofsession.cpp      \
   qof-string-cache.cpp  \
   qofutil.cpp

qofincludedir = ${pkgincludedir}

qofinclude_HEADERS = \
   gnc-date-p.h      \
   gnc-date.h        \
   gnc-numeric.h     \
   guid.h            \
   kvp-util-p.h      \
   kvp-util.h        \
   kvp_frame.h       \
   qof.h             \
   qofbackend-p.h    \
   qofbackend.h      \
   qofbook.h         \
   qofbookslots.h    \
   qofchoice.h       \
   qofclass.h        \
   qofevent.h        \
   qofid-p.h         \
   qofid.h           \
   qofinstance-p.h   \
   qofinstance.h     \
   qoflog.h          \
   qofobject.h       \
   qofquery.h        \
   qofquerycore.h    \
   qofsession.h      \
   qof-string-cache.h  \
   qofutil.h         \
   qof-gobject.h

noinst_HEADERS = \
   md5.h  \
   qofbook-p.h  \
   qofclass-p.h  \
   qofevent-p.h \
   qofmath128-p.h  \
   qofobject-p.h  \
   qofquery-p.h  \
   qofquerycore-p.h \
   qofsession-p.h
   
# Must compile md5.c without strict aliasing, otherwise function md5_finish_ctx
# gets "dereferencing type-punned pointer will break strict-aliasing rules"
noinst_LTLIBRARIES = libmd5.la
libmd5_la_SOURCES = md5.c
libmd5_la_CFLAGS = ${AM_CFLAGS} -fno-strict-aliasing

libgnc_qof_la_LIBADD += libmd5.la

EXTRA_DIST += \
  qofmath128.cpp

if OS_WIN32
libgnc_qof_la_SOURCES += qof-win32.cpp
else
EXTRA_DIST += qof-win32.cpp
endif

## For testing the qofmath128 routines
# run "make check" (to build the test program) and then run test-qofmath
check_PROGRAMS = test-qofmath
test_qofmath_SOURCES = gnc-numeric.cpp
test_qofmath_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_128_BIT_MULT
test_qofmath_LDFLAGS = $(libgnc_qof_la_LDFLAGS)
test_qofmath_LDADD = $(libgnc_qof_common_libs)
test_qofmath_CXXFLAGS = -Wall -Werror

AM_CPPFLAGS += -DG_LOG_DOMAIN=\"qof\"
