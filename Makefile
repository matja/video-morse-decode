
CXX = g++

PKG_CONFIGS = libavcodec libavutil libavfilter libavformat libswscale
INCLUDE = $(shell pkg-config --cflags-only-I $(PKG_CONFIGS))
LIBS = -lm $(shell pkg-config --libs-only-l $(PKG_CONFIGS))

.PHONY: all clean run

all : video-morse-decode

clean :
	-rm -f video-morse-decode

video-morse-decode : video-morse-decode.cpp
	$(CXX) -O2 -std=c++14 $(INCLUDE) -o $@ $< $(LIBS)
