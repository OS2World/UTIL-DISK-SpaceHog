@echo off

If (%1)==() goto TELL
goto %1

:VACPP
  icc space.c
  goto :QUIT

:WATCOM32
  wcl386 -3 space.c
  goto :QUIT

:WATCOM16
  wcl -i=\watcom\h\os21x -x -2 -lp -bt=os2 space.c
  goto :QUIT

:MSC6
  cl /AS /Lp space.c os2.lib /link /pm:vio
  goto :QUIT

:GCC
  gcc space.c
  goto :QUIT

:TELL
echo +----------------------------------------------------------------+
echo . This batch file builds SPACE.exe using one of these compilers: .
echo .                                                                .
echo . 32 bit IBM VisualAge C++ v3       --- build vacpp             .
echo . 32 bit Watcom C/C++      v11      --- build watcom32          .
echo . 32 bit EMX/GCC           v2.7.2.1 --- build gcc               .
echo .                                                                .
echo . 16 bit Watcom C/C++      v11      --- build watcom16          .
echo . 16 bit Microsoft C       v6       --- build msc6              .
echo .                                                                .
echo +-------------------------------------------- wfyuen@ibm.net ----+

:QUIT
