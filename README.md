
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
```

At this stage, it is a read-only filesystem. Writing will come later.

Good luck.

