PHP_ARG_ENABLE(prnl-tools, whether to enable PRNL Tools support,
[ --disable-prnl-tools   Disable PRNL Tools support])

if test "$PHP_PRNL-TOOLS" != "no"; then
  AC_DEFINE(HAVE_PRNLTOOLS, 1, [whether to enable PRNL Tools support])
  PHP_NEW_EXTENSION(prnltools, source.c, $ext_shared)
fi
