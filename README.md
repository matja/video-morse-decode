## Decode morse code from video

### Description

This program reads a video file and analyses the luminance of a specific area for a morse code signal and attempts to decode it.

It will automatically detect the timing and luminance parameters of the signal.

I created this to help with the Morse code puzzle from the Battlefield 4 Dragon Valley easter egg.

### Usage

    video-morse-decode <video_filename> <start_frame> <end_frame> <x0> <y0> <x1> <y1>`

### Example

    youtube-dl https://www.youtube.com/watch?v=... --output video.mp4
    ./video-morse-decode video.mp4 - 0 -1 0.4 0.4 0.6 0.6

The parameters `0.4 0.4 0.6 0.6` represent the coordinates (0.4,0.4)-(0.6,0.6) and here specify the center 20% of the frame

### Options

    <video_filename> : MPEG4, AVI, FLV etc - anything FFmpeg supports
    <start_frame>    : 0 = start from first frame, 30 = skip 1 second (if 30fps)
    <end_frame>      : -1 = end at last frame, 60 = end at 2 second (if 30fps)
    <x0> <y0>        : coordinates of top-left area to examine (0.0-1.0)
    <x1> <y1>        : coordinates of bottom-right area to examine (0.0-1.0)

The coordinates are represented where (0,0) is top-left, and (1,1) is bottom-right.

### Compile

`make` using provided Makefile.

(C++14 compiler and FFmpeg libraries and headers are required)
