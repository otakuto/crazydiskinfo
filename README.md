# CrazyDiskInfo
![](http://raw.github.com/wiki/otakuto/CrazyDiskInfo/images/0.png)

## Introduction
CrazyDiskInfo is an interactive TUI S.M.A.R.T viewer for Unix systems.

## Features
* UI similar to CrystalDiskInfo.
* Health and temperature checking algorithms based on CrystalDiskInfo.

## Required libraries
* ncurses
* libatasmart

#### Debian(or derivative) Systems
```
# apt install libatasmart-dev libncurses-dev
```

## Build and Run
```
$ mkdir build
$ cd build
$ cmake ..
$ make && sudo make install
$ sudo crazy
```

### Binary Package
Debian package is available from [OBS(Open Build Service)](https://build.opensuse.org/package/show/home:tsuroot/CrazyDiskInfo)
Direct Links:
[Debian_8.0](http://download.opensuse.org/repositories/home:/tsuroot/Debian_8.0/)
Ubuntu [14.04](http://download.opensuse.org/repositories/home:/tsuroot/xUbuntu_14.04/)/[16.04](http://download.opensuse.org/repositories/home:/tsuroot/xUbuntu_16.04/)/[16.10](http://download.opensuse.org/repositories/home:/tsuroot/xUbuntu_16.10/)

I requested for tSU-RooT.
Thank you very much for your accept with my request.
