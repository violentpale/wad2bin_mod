# wad2bin_mod
Converts Rock Band 2 (USA) Split Wii WAD packages to Rock Band 3 (Europe) backup WAD packages (*.bin files) using console-specific keydata. 

Usage:
--------------
```
wad2bin <keys.txt> <device.cert> <input WAD> <output dir> [<parent title ID> [--nullkey]]
```

* Paths must not exceed 259 characters. Relative paths are supported.
* The required directory tree for the *.bin file(s) will be created at the output directory.
* You can set your SD card root directory as the output directory.
* Parent title ID shouls be 00010000535A4250 for using DLC with Rock Band 3 (Europe) game.
* Parameter "--nullkey" shouls be set.
* TMD folder with sZxx sub-folders should be in a same folder as wad2bin or gui.

Supported DLCs:
--------------
* Rock Band 2 (`00010000-535A41xx`) (`SZAx`):
    * `00010005-735A41xx` (`sZAx`).
    * `00010005-735A42xx` (`sZBx`).
    * `00010005-735A43xx` (`sZCx`).
    * `00010005-735A44xx` (`sZDx`).
    * `00010005-735A45xx` (`sZEx`).
    * `00010005-735A46xx` (`sZFx`).
