# Tapetools

Tapetools is a software that allows you to calibrate/fix old audio devices.

# How to use

## Input ADC Calibration

* Send an AC sine waveform signal (~50Hz) to the input of the sound capture card (you can use the tone generator for that anc connect output of the capure card to the input).
* Measure the signal with a good A
* Report the value of the multimeter to the "Measured RMS" box then press enter
* Click on the "Calibrate from [right/left]" button (right or left channel)
* You're done, the calibration is now done.
* If you need to recalibrate, click on the "Reset calibration" button

# Build instructions :

## Window

### MinGW dependencies
 
Install with pacman :

* mingw64/mingw-w64-x86_64-fftw
* mingw64/mingw-w64-x86_64-SDL2
* mingw64/mingw-w64-x86_64-glew

# Linux

TODO