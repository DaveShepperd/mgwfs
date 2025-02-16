
mgwfsf - Read/write Atari Games/Midway Games West filesystem.
===

This project started as the now defunct mgwfs project: a Linux device driver.
I couldn't get that to work so backed off and wrote it as an interface to libfuse.
Much simpler and it came together quickly and quite nicely.

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
git clone https://github.com/daveshepperd/mgwfsf.git mgwfsf
cd mgwfsf
make
mkdir game
./mgwfsf --image=<path-to-Atari-image> game
```

At this stage, it is a read-only filesystem. Writing will come later.

Good luck.

