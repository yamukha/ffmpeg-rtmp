CC=gcc
IPATH=/home/amukha/ffmpeg_sources/ffmpeg/
LIBFF=-lavformat -lavcodec -lavutil -lavdevice -lswscale -lswresample
LIBENC=-lfaac -lmp3lame -lfdk-aac -lvpx -lx264 -lyasm  -lrtmp -lvorbisenc -lva   
LIBSYS=-lz -lm -lpthread
LPATH=/home/amukha/ffmpeg_build/lib
SOURCES=
NAMEDSK=ffstream

OBJECTS =
EXECUTABLE=
LDFLAGS=-Wl,-rpath=$(LPATH)
CFLAGS=-Wno-deprecated-declarations 

all: clean $(NAMEDSK) 

do: $(NAMEDSK) 

$(NAMEDSK): 
	$(CC) $(NAMEDSK).c utils.c -I$(IPATH) $(LIBFF) $(LIBENC) $(LIBSYS) -L$(LPATH)  $(LDFLAGS) -o $(NAMEDSK) 

clean:
	rm $(NAMEDSK)
