MollenOS is a hobby OS project, started back in 2011. 
The aim is to create something usuable, just for trivial tasks in your every normal day like checking the web.

It is written entirely from scratch, 
however the C Library is a custom variation of third party existing c libraries and my own implementations.

MollenOS is currently undergoing a very big rewrite of the kernel (A lot of the old systems were 
unstable and thus required some refactoring.). Also I have added a few new goals to MollenOS after
multicore support was implemented. One of the new goals is it needs to natively support multicore, which
means increased focus on synchronization and locking, and all platform dependant code needs to be seperated
from the independant code. Primary focus is on robustness and error-checking. All new implementations will be
tested thoroughly.