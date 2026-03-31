# kittenJPEG-GL

Where the Kitten comes from Shaders

## What is this?

This project is a fani playground in which **JPEG is being decoded by few OpenGL fragment shaders**.

Biologically speaking it's a fork of [kittenJPEG](https://github.com/kittennbfive/kittenJPEG) (kudos to its author) crossbreeded with
a [humble draft drafted a year before](https://github.com/aslobodeniuk/fun/blob/master/jpegdec_shader.c).

So far it's only a proof-of-concept that is even enough far away from benchmarking, such enough that we
unlikely ever get there, but who said all was done for speed?!

Now ain't that beautiful how "our" subject have inherited the LICENSE file?! It really is.

## Licence and Disclaimer
This code is provided under AGPLv3+ and WITHOUT ANY WARRANTY!  
The example picture `kitten.jpg` (and its resized version `kitten_small.jpg`) was copied from [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Chat_regardant_le_ciel_\(2016_photo;_cropped_2022\).jpg) and is licenced under Creative Commons CC0 1.0 Universal Public Domain Dedication by its author susannp4 (Thank you!).  

## Why did you made this?

"We" ask "ourselves" the same question, and get multiple controversal answers in a batch.

## How to use

### Compilen

`gcc kittenJPEG.c -lX11 -lEGL -lGLESv2 -lm -o kit`

### Runen

`./kit`

## How to have fun with this "project"?

Fork and do everything with it

## Project Status:

Version -0.69

Only one jpg file exists on this planet that doesn't break the code TODAY.
All of a sudden the dirtiest code survived the Holy Rabit Hole of "clean" coding,
but "We" will fixet.
