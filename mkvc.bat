set SUBTARGET="mame"
set SUFFIX=""

set COMMONFLAGS=SUBTARGET=%SUBTARGET% MSVC_BUILD=1 MAXOPT= PREFIX= SUFFIX=%SUFFIX% WINUI=1 NO_DLL= MAMEMESS=1

call env.bat

set PSDK_DIR=%ProgramFiles%\Microsoft SDKs\Windows\v6.0A

set PATH=%PSDK_DIR%\bin\;%PATH%
set INCLUDE=extravc\include\;%PSDK_DIR%\Include\
set LIB=extravc\lib\;extravc\lib\x86;%PSDK_DIR%\Lib\

call "%VS90COMNTOOLS%vsvars32.bat"

mingw32-make %COMMONFLAGS% maketree obj/windows/mamep%SUFFIX%/osd/windows/vconv.exe
mingw32-make %COMMONFLAGS% NO_FORCEINLINE=1 obj/windows/mamep%SUFFIX%/emu/cpu/m6809/m6809.o

mingw32-make %COMMONFLAGS%
