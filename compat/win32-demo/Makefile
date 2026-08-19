# Build the Castalia Win32 compatibility-demo executable (Bible §11).
# Default target is 64-bit (PE32+) so it runs under wine64 on amd64 without
# i386 multiarch. Requires gcc-mingw-w64-x86-64 (or -i686 for the 32-bit one).
CC64 = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
CFLAGS = -O2 -mwindows
LIBS = -luser32 -lgdi32

hello.exe: hello.c
	$(CC64) $(CFLAGS) -o $@ $< $(LIBS)

hello32.exe: hello.c
	$(CC32) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f hello.exe hello32.exe
