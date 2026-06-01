
mgwfs - Read/write Atari Games/Midway Games West filesystem.
===

Written via an interface to libfuse.

The license remains GPL because it appears that is what libfuse requires.

You will obviously need the libfuse tools and headers to build this module.<br>
I am no expert at this but this is what I did to get this project to build on Ubuntu LTS24:
```
sudo apt install -y libfuse3-3 libfuse3-dev
```
On Fedora41:
```
dnf install -y fuse3 fuse3-devel
```
On either system:
```
git clone https://github.com/daveshepperd/mgwfs.git mgwfs
cd mgwfs
make
mkdir game
./mgwfs --image=<path-to-Atari-image> <path-to-local-mount-point>

Use ./mgwfs --help for list of command line options.
```

It has taken ne quite a while to get to it, but with the enormous and grateful help of Claude 4.8, write functionality has been added. I haven't done a lot of testing with it. Claude did quite a bit of testing while it was writing its stuff, so it probably works okay. Add the --rw command line option to allow read/write to the image.

NOTE: The boot file has special marking on versions of the filesystem greater than v1.1. In order to allow you to mark a file as being a boot file, use the new tool mgwfsctl.
Assuming your boot file is at relative location on the game disk <b>somewhere/over/the/rainbow/bootme.img</b>. An example of how you would mark it as the boot file:

```
# Create a local mount point
mkdir -p /mnt/mgw
# Mount the Atari image on it
./mgwfs --image=<path-to-Atari-image> --rw /mnt/mgw
# If you are interested, get some info about the filesystem
./mgwfsctl stats /mnt/mgw
# Mark your file as bootable
./mgwfsctl setboot /mnt/mgw/somewhere/over/the/rainbow/bootme.img
```

The program will emit an error message if the filesystem doesn't support that option.

You are not likely to run into it in the field, but filesystems with version 1.7 allow up to 4 different boot images. Which one is booted is typically selected with a 2 position DIP switch on the game board. You would specify which boot file with an additional command line argument:

```
./mgwfsctl setboot /mnt/mgw/somewhere/over/the/rainbow/bootme.img 3
```
Would assign the boot file to slot 3 of the 4 available.

Good luck.

