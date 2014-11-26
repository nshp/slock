slock - simple screen locker
============================
Slightly less simple screen locker utility for X.
Forked from http://tools.suckless.org/slock/


Requirements
------------
In order to build slock you need the Xlib header files.


Installation
------------
Edit config.mk to match your local setup (slock is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install slock
(if necessary as root):

    make clean install


Running slock
-------------
Simply invoke the 'slock' command. To get out of it, enter your password.


Differences From Upstream
-------------------------
This fork adds cairo for drawing noninteractive content on the lock screen. By
default, it draws circular memory and battery meters around a centered clock. A
capslock indicator will appear under the clock when appropriate. These only
appear when you begin to enter your password -- the "empty" screen is still
solid black.

Additionally, this adds a slight delay when checking the password to avoid local
brute force, e.g. via a device pretending to be a USB keyboard.
