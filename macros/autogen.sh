#!/bin/sh
# Run this to generate all the initial makefiles, etc.

DIE=0

#GETTEXTIZE=${GETTEXTIZE:-gettextize}
INTLTOOLIZE=${INTLTOOLIZE:-intltoolize}
LIBTOOLIZE=${LIBTOOLIZE:-libtoolize}
LIBTOOL=${LIBTOOL:-libtool}

if [ -n "$GNOME2_PATH" ]; then
	for dir in `echo $GNOME2_PATH | sed 's/:/ /g'`; do
	    if test -d "${dir}/share/aclocal"; then
		ACLOCAL_FLAGS="${ACLOCAL_FLAGS} -I ${dir}/share/aclocal"
	    fi;
	    if test -d "${dir}/bin"; then
		PATH="${PATH}:${dir}/bin"
	    fi;
	done;
	export PATH
fi

# usage: test_version program version
# returns 0 if program >= version; returns 1 if not.
test_version()
{
    this_prog="$1"
    want_vers="$2"

    testv=`"$this_prog" --version 2>/dev/null | head -n 1 | awk '{print $NF}'`
    if test -z "$testv" ; then return 1 ; fi

    testv_major=`echo "$testv" | sed 's/\([0-9]*\).\([0-9]*\).*$/\1/'`
    testv_minor=`echo "$testv" | sed 's/\([0-9]*\).\([0-9]*\).*$/\2/'`

    vers_major=`echo "$want_vers" | sed 's/\([0-9]*\).\([0-9]*\).*$/\1/'`
    vers_minor=`echo "$want_vers" | sed 's/\([0-9]*\).\([0-9]*\).*$/\2/'`

    # if wanted_major > found_major, this isn't good enough
    if test $vers_major -gt $testv_major ; then
	return 1
    # if wanted_major < found_major, then this is fine
    elif test $vers_major -lt $testv_major ; then
	return 0
    # if we get here, then the majors are equal, so test the minor version
    # we want found_minor >= want_minor.
    # So, if want_minor > found_minor, this is bad.
    elif test $vers_minor -gt $testv_minor ; then
	return 1
    # this is it.
    else
	return 0
    fi
}

# usage: find_program preset program version "<other versions>"
# sets "program" to the name of the program to use.
# if preset is set, then use that regardless,
#  otherwise check if "program" is of a good enough version and use that,
#  otherwise check if "program-version" is of a good enough version and use that.
#  otherwise return an error.
find_program()
{
    find="$1"
    prog="$2"
    vers="$3"
    extravers="$4"

    if test -n "$find" ; then
	test_version "$find" "$vers"
	status="$?"
	if test "$status" = 0 ; then
	    program="$find"
	    return 0
	fi
	echo "**Error**: cannot use $find"
    else

	test_version "$prog" "$vers"
	status=$?
	if test "$status" = 0 ; then
	    program="$prog"
	    return 0
	fi

	for test_vers in $vers $extravers ; do
	    test_version "$prog-$test_vers" "$vers"
	    status=$?
	    if test "$status" = 0 ; then
		program="$prog-$test_vers"
		return 0
	    fi
	done
    fi

    echo
    echo "**Warning**: Could not find a $prog that identifies itself >= $vers."
    echo
    program="$prog"
}

find_program "$AUTOCONF" autoconf 2.53
AUTOCONF="$program"
find_program "$AUTOHEADER" autoheader 2.53
AUTOHEADER="$program"
find_program "$AUTOMAKE" automake 1.5 "1.6 1.7"
AUTOMAKE="$program"
find_program "$ACLOCAL" aclocal 1.5 "1.6 1.7"
ACLOCAL="$program"


(${AUTOCONF} --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`autoconf' installed to compile GnuCash."
  echo "Download the appropriate package for your distribution,"
  echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
  DIE=1
}

gettext_version=`gettextize --version 2>&1 | sed -n 's/^.*GNU gettext.* \([0-9]*\.[0-9.]*\).*$/\1/p'`
case $gettext_version in
0.10.*)
	;;
	
*)
	INTL="--intl --no-changelog";;
esac

#(grep "^AM_PROG_LIBTOOL" $srcdir/configure.in >/dev/null) && {
#  (${LIBTOOL} --version) < /dev/null > /dev/null 2>&1 || {
#    echo
#    echo "**Error**: You must have \`libtool' installed to compile GnuCash."
#    echo "Get ftp://ftp.gnu.org/pub/gnu/libtool-1.4.2.tar.gz"
#    echo "(or a newer version if it is available)"
#    DIE=1
#  }
#}

#grep "^AM_GNU_GETTEXT" $srcdir/configure.in >/dev/null && {
#  grep "sed.*POTFILES" $srcdir/configure.in >/dev/null || \
#  (${GETTEXT} --version) < /dev/null > /dev/null 2>&1 || {
#    echo
#    echo "**Error**: You must have \`gettext' installed to compile GnuCash."
#    echo "Get ftp://alpha.gnu.org/gnu/gettext-0.10.35.tar.gz"
#    echo "(or a newer version if it is available)"
#    DIE=1
#  }
#}

#grep "^AM_GNOME_GETTEXT" $srcdir/configure.in >/dev/null && {
#  grep "sed.*POTFILES" $srcdir/configure.in >/dev/null || \
#  (${GETTEXT} --version) < /dev/null > /dev/null 2>&1 || {
#    echo
#    echo "**Error**: You must have \`gettext' installed to compile GnuCash."
#    echo "Get ftp://alpha.gnu.org/gnu/gettext-0.10.35.tar.gz"
#    echo "(or a newer version if it is available)"
#    DIE=1
#  }
#}

(${AUTOMAKE} --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`automake' installed to compile GnuCash."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.4.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
  NO_AUTOMAKE=yes
}


# if no automake, don't bother testing for aclocal
test -n "$NO_AUTOMAKE" || (${ACLOCAL} --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: Missing \`aclocal'.  The version of \`automake'"
  echo "installed doesn't appear recent enough."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake-1.4.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
}

if test "$DIE" -eq 1; then
  exit 1
fi

if test -z "$*"; then
  echo "**Warning**: I am going to run \`configure' with no arguments."
  echo "If you wish to pass any to it, please specify them on the"
  echo \`$0"' command line."
  echo
fi

case $CC in
xlc )
  am_opt=--include-deps;;
esac

for coin in `find $srcdir -name configure.in -print`
do 
  dr=`dirname $coin`
  if test -f $dr/NO-AUTO-GEN; then
    echo skipping $dr -- flagged as no auto-gen
  else
    echo processing $dr
    macrodirs=`sed -n -e 's,AM_ACLOCAL_INCLUDE(\(.*\)),\1,gp' < $coin`
    ( cd $dr
      macrodirs=`find . -name macros -print`
      for i in $macrodirs; do
	if test -f $i/gnome-gettext.m4; then
	  DELETEFILES="$DELETEFILES $i/gnome-gettext.m4"
	fi
      done

      echo "deletefiles is $DELETEFILES"
      aclocalinclude="$ACLOCAL_FLAGS"
      for k in $aclocalinclude; do
  	if test -d $k; then
	  if [ -f $k/gnome.m4 -a "$GNOME_INTERFACE_VERSION" = "1" ]; then
	    rm -f $DELETEFILES
	  fi
        fi
      done
      for k in $macrodirs; do
  	if test -d $k; then
          aclocalinclude="$aclocalinclude -I $k"
	  if [ -f $k/gnome.m4 -a "$GNOME_INTERFACE_VERSION" = "1" ]; then
	    rm -f $DELETEFILES
	  fi
        fi
      done

      echo
      echo "*** WARNING ***"
      echo "*** We're about to run \"gettext\" which may spew a few paragraphs"
      echo "*** of crap at you and ask you to acknowledge it.  If it does this,"
      echo "*** just hit return to acknowledge gettext.  You DO NOT need to do"
      echo "*** anything that it asks of you except hitting return."
      echo
      if grep "^AM_GNU_GETTEXT" configure.in >/dev/null; then
	if grep "sed.*POTFILES" configure.in >/dev/null; then
	  : do nothing -- we still have an old unmodified configure.in
	else
	  echo "Creating $dr/aclocal.m4 ..."
	  test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	  echo "(1) Running ${GETTEXTIZE}...  Ignore non-fatal messages."
	  echo "no" | ${GETTEXTIZE} --force --copy $INTL
	  echo "Making $dr/aclocal.m4 writable ..."
	  test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
        fi
        grep "intl/Makefile" configure.in > /dev/null ||
        ( sed -e 's#^AC_OUTPUT(#AC_OUTPUT( intl/Makefile po/Makefile.in#' \
  	configure.in >configure.in.new && mv configure.in.new configure.in )
      fi
      if grep "^AM_GNOME_GETTEXT" configure.in >/dev/null; then
        echo "Creating $dr/aclocal.m4 ..."
        test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	echo "(2) Running ${GETTEXTIZE}...  Ignore non-fatal messages."
        echo "no" | gettextize --force --copy $INTL
        echo "Making $dr/aclocal.m4 writable ..."
        test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
        grep "intl/Makefile" configure.in > /dev/null ||
        ( sed -e 's#^AC_OUTPUT(#AC_OUTPUT( intl/Makefile po/Makefile.in#' \
  	configure.in >configure.in.new && mv configure.in.new configure.in )
      fi
      if grep "^AM_GLIB_GNU_GETTEXT" configure.in >/dev/null; then
        echo "Creating $dr/aclocal.m4 ..."
        test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
        echo "(3) Running gettextize...  Ignore non-fatal messages."
        echo "no" | glib-gettextize --force --copy
        echo "Making $dr/aclocal.m4 writable ..."
        test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
        grep "po/Makefile.in" configure.in > /dev/null ||
        ( sed -e 's#^AC_OUTPUT(#AC_OUTPUT( po/Makefile.in#' \
  	configure.in >configure.in.new && mv configure.in.new configure.in )
      fi
      if grep "^AC_PROG_INTLTOOL" configure.in >/dev/null; then
        echo "Running intltoolize ..."
        intltoolize --copy
      fi
#      if grep "^A[CM]_PROG_LIBTOOL" configure.in >/dev/null; then
#        echo "Running libtoolize..."
#        libtoolize --force --copy
#      fi
      echo "Running $ACLOCAL $aclocalinclude ..."
      $ACLOCAL $aclocalinclude
      if grep "^AM_CONFIG_HEADER" configure.in >/dev/null; then
	echo "Running ${AUTOHEADER}..."
	${AUTOHEADER} || { echo "**Error**: autoheader failed."; exit 1; }
      fi
      echo "Running $AUTOMAKE --gnu $am_opt ..."
      echo "*** You should ignore any complaints about files in lib/goffice"
      echo "    overriding the CFLAGS variable."
      $AUTOMAKE --add-missing --gnu $am_opt
      echo "Running autoconf ..."
      autoconf
    )
  fi
done

conf_flags="--enable-maintainer-mode --enable-error-on-warning --enable-compile-warnings" # --enable-iso-c

if test x$NOCONFIGURE = x; then
  echo Running $srcdir/configure $conf_flags "$@" ...
  $srcdir/configure $conf_flags "$@" \
  && echo Now type \`make\' to compile $PKG_NAME || exit 1
else
  echo Skipping configure process.
fi
