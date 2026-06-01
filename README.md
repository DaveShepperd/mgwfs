
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
./mgwfs --image=<path-to-Atari-image> game

Use ./mgwfs --help for list of command line options.
```

It has taken quite a while, but with the help of Claude, I added write functionality to it. It probably works. Add the --rw command line option to allow read/write to the image.

NOTE: The boot file has special marking on versions of the filesystem greater than v1.1. In order to allow you to mark a file as being a boot file, use the new image mgwfsctl.
Use ./mgwfsctl --help to get a list of command line options for that tool. I.e., if the mount point spec'd with mgwfs is /mnt/mgw and the boot file you want to mark is at
/mnt/mgw/somewhere/over/the/rainbow/bootme.img, then use this command to mark it:

```
./mgwfsctl setboot /mnt/mgw/somewhere/over/the/rainbow/bootme.img
```

The program will emit an error message if the filesystem doesn't support that option. Use ```./mgwfsctl stats /mnt/mgw``` to get information about the mounted image. 

Good luck.

