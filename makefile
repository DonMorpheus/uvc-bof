CC64 := x86_64-w64-mingw32-gcc
CC32 := i686-w64-mingw32-gcc
CFLAGS := -c -Os -I source
STREAM_CFLAGS := -I webcam-bof -I webcam-bof/common -Os -fno-weak \
	-fno-function-sections -fno-inline -fno-common -fno-data-sections -w -c -DBOF

all: ObjectFiles/WebcamStream.x64.o ObjectFiles/WebcamStream.x86.o \
	ObjectFiles/WebcamPrep.x64.o ObjectFiles/WebcamPrep.x86.o

ObjectFiles/WebcamStream.x64.o: webcam-bof/entry.c
	mkdir -p ObjectFiles
	$(CC64) $(STREAM_CFLAGS) -o $@ webcam-bof/entry.c

ObjectFiles/WebcamStream.x86.o: webcam-bof/entry.c
	mkdir -p ObjectFiles
	$(CC32) $(STREAM_CFLAGS) -o $@ webcam-bof/entry.c

ObjectFiles/WebcamPrep.x64.o: source/webcam-prep.c source/beacon.h
	mkdir -p ObjectFiles
	$(CC64) $(CFLAGS) -o $@ source/webcam-prep.c

ObjectFiles/WebcamPrep.x86.o: source/webcam-prep.c source/beacon.h
	mkdir -p ObjectFiles
	$(CC32) $(CFLAGS) -o $@ source/webcam-prep.c

clean:
	rm -f ObjectFiles/WebcamStream.x64.o ObjectFiles/WebcamStream.x86.o \
		ObjectFiles/WebcamPrep.x64.o ObjectFiles/WebcamPrep.x86.o
