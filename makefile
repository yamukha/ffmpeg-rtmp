#CC=g++ -D__STDC_CONSTANT_MACROS 
CC=gcc 
IPATH=/home/amukha/ffmpeg_sources/
LIBFF=-lavformat -lavcodec -lavutil -lavdevice -lswscale -lswresample
LIBENC=-lfaac -lmp3lame -lfdk-aac -lvpx -lx264 -lyasm  -lrtmp -lvorbisenc -lva
LIBJSON=-ljson-c   
LIBSYS=-lz -lm -lpthread
LPATH=/home/amukha/ffmpeg_build/lib

JSONC=$(IPATH)json-c

SOURCES=
NAMEDSK=ffstream

OBJECTS =
EXECUTABLE=
LDFLAGS=-Wl,-rpath=$(LPATH),-rpath=$(JSONC)/.libs/ 
LDFLAGS+= -L$(JSONC)/.libs 
CFLAGS=-Wno-deprecated-declarations 
#CFLAGS += -I$(JSONC)/include/json-c

all: clean $(NAMEDSK) 

do: $(NAMEDSK) 

$(NAMEDSK): 
	$(CC) $(NAMEDSK).c utils.c filter.c -I$(IPATH) $(LIBFF) $(LIBENC) $(LIBSYS)  $(LIBJSON)-L$(LPATH) $(LDFLAGS) -o $(NAMEDSK) 

clean:
	rm $(NAMEDSK)
