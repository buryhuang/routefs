routefs
======
routefs is a FUSE file system that routes files to and from different locations. Routefs is similar to the idea of union-fs, but with a much more flexible design by using flexible config file. It fits much better in caching and cloud solutions.

Build and Installation
======
I tested using ubuntu 14.04. The following commands addes dependencies for building.
```
git clone https://github.com/buryhuang/routefs.git
sudo apt-get install build-essential
sudo apt-get install libfuse-dev
sudo install pkg-config
make
```
The resulted binariescan be used as below:
```
./routefs -h
usage:  routefs [FUSE and mount options] rootDir mountPoint
```

Core Design
======
The core design of routefs is to use a simple syntax to flexiblly configure where the data or files goes to. By this design, this simple routing layer does not need to be changed to adapt to various totally different personal or enterprise storage solutions.

This example demostrate the self-explained concept of how this file system works:
The file format is:
```
<suffix>,<target folder>
```

typemap.default example:
```
.mhg,/routefs_data/L3store/deobj
.bsf,/routefs_data/L2store/deblock
*,/routefs_data/L1store/raw
```

Cache Layer
-----
Cache layer is experimental but works in a certain degree. It uses a desinated folder as "staging" or "cache" folder, then background post process copy-then-remove (move) the data to the next high laytency - but high capacity storage.

Unlimited Use Cases By Design
-----
With this simple flexible design, this routefs makes efficient use cases possible.
For example, for personal backup devices, it's as simple as:
local SSD -> local hard drive -> Amazon S3.
With the routefs, we don't need to bother change the code. Simply use a different config file.

VS Union-FS
-----
Union-FS is kernel FS which provides much better performance because of less context switch. Though, in current heteriougenous environment, network enabled storage hides the advantage of being a kernel FS. Union-FS has many limitation for being a kernel FS, while routefs provides much more flexibility and much easy to understand.
