
mgwfs - Read/write Atari Games/Midway Games West filesystems.
===

This was cloned from hellofs which in turn cloned from simplefs. This no longer resembles hellofs being it uses the filesystem structure of the game's disk image.

But starting with hellofs was way easier to understand what had to be changed. I found starting with simplefs a bit too much to understand what needed to be done.

This filesystem was developed on Fedora 41 (Kernel 6.11.11) and tested on Ubuntu 24LTS (kernel 6.8.0). Note hellofs was developed under a much earlier kernel (3.x)
and lots of changes to the various kernel API's have been made between kernel versions 3.x and 6.x.

The license remains GPL because some kernel functions require it to be available.


