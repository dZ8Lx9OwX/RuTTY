RuTTY 0.14

Record and Replay PuTTY, RRPuTTY, RuTTY !
see RuTTY.rtf for documentation

other/
rutty.exe	windows executable
RuTTY.rtf	documentation 

notes:
RuTTY is a modified version of PuTTY release 0.63
only PuTTY, no other tools from the suite
only windows

build with minGW/gcc:
make -f makefile.mgw VER="-DRELEASE=0.63 -Drutty=0.14" putty.exe

