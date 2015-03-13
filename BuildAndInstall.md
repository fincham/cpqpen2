# Introduction #

I'm building the module against a heavily stripped down vanilla kernel, built under a Gentoo build environment with uclibc.

# Details #

Rough outline to make it "work":
  * Checkout the SVN
  * Run "make" in the source directory.
    * If this fails, check that /lib/modules/your-kernel/build links to /usr/src/your-kernel-source
  * Copy cpqpen2.ko to somewhere (haven't decided where it should go yet...)
  * insmod cpqpen2.ko
  * mknod /dev/cpqpen2 c 70 0
  * Cross fingers and run pentest from your build directory