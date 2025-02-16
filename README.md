
mgwfs - Read/write Atari Games/Midway Games West filesystem.
===

## DO NOT USE THIS! It doesn't work and will never work. I will delete it someday.

### Use instead the [mgwfsf project](https://github.com/daveshepperd/mgwfsf.git).

### The rest of this document remains present just for future reference.

This was cloned from hellofs which in turn cloned from simplefs. This no longer resembles hellofs being it uses the filesystem structure of the game's disk image.

But starting with hellofs was way easier to understand what had to be changed. I found starting with simplefs a bit too much to understand what needed to be done.

This filesystem was developed on Fedora 41 (Kernel 6.11.11) and tested on Ubuntu 24LTS (kernel 6.8.0). Note hellofs was developed under a much earlier kernel (3.x)
and lots of changes to the various kernel API's have been made between kernel versions 3.x and 6.x.

The license remains GPL because some kernel functions require it to be available.

You will obviously need the kernel-headers and possibly the kernel-devel to build the module. Make it and install the module:
```
make
sudo insmod mgwfs.ko
```
If the game disk is connected directly to your Linux machine at, for example, **/dev/sdg**, mount it like this:
```
sudo mkdir -p /mnt/agame
sudo mount -t mgwfs -o ro /dev/sdg /mnt/agame
```
If the game disk image is in a file, for example **./agame.img**, you would mount it like this:
```
sudo mkdir -p /mnt/agame
sudo mount -t mgwfs -o ro,loop ./agame.img /mnt/agame
```
Good luck.

