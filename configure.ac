dnl --------------------------------------------------------------------------
dnl JL yuvutils, some utilities for processing YUV4MPEG streams
dnl Copyright 2005 Johannes Lehtinen
dnl --------------------------------------------------------------------------
dnl
dnl Process this file with autoconf to produce a configure script.

# Autoconf initialization
# -----------------------
AC_INIT([JL yuvutils], [1.0], [johannes.lehtinen@iki.fi], [jl-yuvutils])
AC_COPYRIGHT([Copyright 2005 Johannes Lehtinen
This configure script is free software; Johannes Lehtinen gives unlimited
permission to copy, distribute and modify it.])
AC_CONFIG_SRCDIR([yuvinfo.c])
AC_CONFIG_AUX_DIR([auxliary])

# Automake initialization
# -----------------------
AM_INIT_AUTOMAKE([foreign])

# C/C++ compiler settings
# -------------------
AC_LANG([C])
AC_PROG_CC
if test -z "$CC"; then
  AC_MSG_ERROR([C compiler was not found (required)])
fi

# Check for MJPEG tools headers
# -----------------------------
AC_ARG_WITH([mjpegtools-headers], AS_HELP_STRING([--with-mjpegtools-headers=PATH], [use MJPEG tools headers at PATH]))
case "$with_mjpegtools_headers" in
	yes)
		with_mjpegtools_headers=''
		;;
	no)
		AC_MSG_ERROR([MJPEG tools headers are required])
		;;
esac
AC_CACHE_CHECK([where to find yuv4mpeg.h], [jlyu_cv_inc_mjpegtools],
[if test -n "$with_mjpegtools_headers"; then
  jlyu_cv_inc_mjpegtools="-I$with_mjpegtools_headers"
else
  for tmp_ip in '' -I/usr/local/include/mjpegtools -I/usr/include/mjpegtools; do
    tmp_found=''
    old_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$tmp_ip $CPPFLAGS"
    AC_PREPROC_IFELSE([#include <yuv4mpeg.h>
], [tmp_found=yes])
	CPPFLAGS="$old_CPPFLAGS"
    if test -n "$tmp_found"; then
      jlyu_cv_inc_mjpegtools="$tmp_ip"
      break
    fi
  done
fi])
CPPFLAGS="$jlyu_cv_inc_mjpegtools $CPPFLAGS"
AC_CHECK_HEADER([yuv4mpeg.h], [], AC_MSG_ERROR([yuv4mpeg.h from MJPEG tools is required]))

# Check for MJPEG tools mjpegutils library
# ----------------------------------------
AC_ARG_WITH([mjpegtools-libmjpegutils], AS_HELP_STRING([--with-mjpegtools-libmjpegutils=FILE], [use FILE as MJPEG tools mjpegutils library]))
case "$with_mjpegtools_libmjpegutils" in
	yes)
		with_mjpegtools_libmjpegutils=''
		;;
	no)
		AC_MSG_ERROR([MJPEG tools mjpegutils library is required])
		;;
esac
AC_MSG_CHECKING([where to find mjpegutils library])
if test -z "$with_mjpegtools_libmjpegutils"; then
  AC_MSG_RESULT([-lmjpegutils])
  AC_CHECK_LIB([mjpegutils], [y4m_si_get_plane_length], [LIBS="-lmjpegutils $LIBS"], AC_MSG_ERROR([mjpegutils library is required]))
else
  AC_MSG_RESULT([$with_mjpegtools_libmjpegutils])
  LIBS="$with_mjpegtools_libmjpegutils $LIBS"
  AC_CHECK_FUNC([y4m_si_get_plane_length], [], AC_MSG_ERROR([could not link correctly with mjpegutils library]))
fi

# Output Makefiles
# ----------------
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
