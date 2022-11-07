RuTTY 0.13

Record and Replay PuTTY, RRPuTTY, RuTTY !
see RuTTY.rtf for documentation

other/
rutty.exe	windows executable
RuTTY.rtf	documentation 

notes:
RuTTY is a modified version of PuTTY release 0.62
only PuTTY, no other tools from the suite
only windows

build:
RuTTY.exe was build with minGW/gcc:
make -f makefile.mgw VER="-DRELEASE=0.62 -Drutty=0.13" putty.exe

