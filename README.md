## FSV

FSV is a 3D filesystem visualiser. It is derived from a program known as FSN (pronounced "Fusion") which was written for SGI IRIX, and is featured in the movie _Jurassic Park_. The latest version of FSN was released in 1996, and was written by Joel Tesler and Steve Strasnick. Franly it's pretty cool. 

FSN was forked and ported to Linux by [Daniel Richard G.](http://fox.mit.edu/skunk/), a former student of Computer Science at the MIT. That version is [available here](http://fsv.sourceforge.net/).

That version was later forked by Maurus Cuelenaere. That version is [also available](https://github.com/mcuelenaere/fsv).

That version used GTK1 and very outdated OpenGL code, and would not build on modern Linux systems. It was also missing many features that I believe the original Linux port was supposed to implement. 

This version updates the code to use GTK4 and updated OpenGL calls. It also re-implements the Disc Visualisation and the settings options. 

Claude Code did a lot of heavy lifting. I really feel more like a project manager... 


**Requirements**

- GTK 4.0 or later
- libepoxy (for OpenGL function loading)
- cglm (C OpenGL mathematics library)
- OpenGL 3.3 core profile

**Install**

1. Install dependencies:
    - **Debian/Ubuntu:** `sudo apt-get install libgtk-4-dev libepoxy-dev libcglm-dev libgl-dev meson ninja-build`
    - **Rocky/RHEL/Fedora:** `sudo dnf install gtk4-devel libepoxy-devel cglm-devel mesa-libGL-devel meson ninja-build`
    - **Arch Linux:** `sudo pacman -S gtk4 libepoxy cglm mesa meson ninja`
2. Clone the repository
3. Build:
    - `meson setup builddir`
    - `ninja -C builddir`
    - `sudo ninja -C builddir install`

