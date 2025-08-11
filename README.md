# README to hp6633

## Name
hp6633 - controls the Hewlett-Packard HP663[2,3,4] Power Supply using GPIB

## Description
'hp6633' is a program to access and control the Hewlett-Packard HP6633 System Power Supply using GPIB under Linux. 
It was written and tested with a HP6633 and "is expected to work" with the HP6632 and 6634, too.

Some features:
- set output voltage
- set voltage limit
- set current limit, either using current limiter or overcurrent trip
- ramp output voltage up, down, up-and-down or down-and-up
- keep last settings after quitting the program
- record voltage, current, and elapsed time to file
- **graphical display** ("stripchart recorder") using gnuplot

## Requirements
- At least one HP6633 instrument, and a GPIB cable.
- A computer with Linux and a GPIB interface installed ;-)
- The [Linux-GPIB library](http://sourceforge.net/projects/linux-gpib/) must be installed and configured. You can find a short summary of the required steps at [schweizerschrauber.ch](https://www.schweizerschrauber.ch/sci/elec.html#gpib)
- The graphic display makes use of [gnuplot](http://www.gnuplot.info/), a free software for scientific data plotting. You need gnuplot v4 or later.
- The user accessing the instrument must have access rights to the GPIB instrumentation:
  - With recent versions of linux-gpib, make sure the user is in group 'gpib'.
  - With earlier versions of linux-gpib, either become root before running, or make the executable setuid root.

## Installation
To install, just compile the file(s) according to the instructions given at the beginning of the hp6633.c file, then copy the corresponding executable to any 
location you desire (probably `/usr/local/bin` or `~/bin`). 

Invoke it by [typing its name](README.md#synopsis). As the program is a command-line utility, it needs to be run in a terminal window. 

## Synopsis
`hp6633 [-h] [-u V] [-U upperV] [-m maxV] [-i A] [-I] [-r dV] [-R] [-t dt] [-a id] [-c txt] [-k] [-n] [-g /path/to/gnuplot] [-f] outfile`

### Options and defaults

    -h       this help screen
    -a id    use instrument at GPIB address 'id' (default is 5)
    -k       keep settings before and after run (default: switches off)

    -u V     set actual voltage (and ramp start voltage) to 'V' Volt
    -U V     set upper ramp voltage to 'V' Volt
    -M V     set voltage limiter to 'V' Volt

    -i A     set current limiter to 'A' Ampere
    -I       enable overcurrent trip (default off)

    -r dV    ramp voltage by increment 'dV' mV (default 0 mV), can be pos or neg
    -R       run ramp up and down (default is one-way)
    -t dt    delay between measurements or steps in 0.1 s (default is 10)

    -w x     force write to disk every x samples (default is 100)
    -f       force overwriting of existing output file 
    -c "txt" comment text

    -g       specify path/to/gnuplot (if not in your current PATH anyway)
    -n       no graphics


## Running the Program

At startup, the software expects at least the name of the output data file as an argument. 
This is mandatory (with one exception, see below), since it allows logging of the voltage and current ... which is one of the reasons why I wrote it ;-)

    ./hp6633 path/to/file.dat

This switches the instrument on (albeit at 0 V ;-) and records its output voltage and current into "/path/to/file.dat". 
Press ESC or 'q' to exit.

Now set the output voltage to 12 V (`-u 12`), all the rest remains at the instrument default settings:

    ./hp6633 -u 12 /path/to/file

Set output voltage to 12 V, current limit to 1 A (`-i 1`), and enable the overcurrent trip (`-I`) instead of the current limiter:

    ./hp6633 -u 12 -i 1 -I /path/to/file

Set output voltage to 12 V, current limit to 1 A, overcurrent trip on, and keep these settings (`-k`) after program termination:

    ./hp6633 -u 12 -i 1 -I -k /path/to/file

**Sampling intervals** are specified using option `-t dt`, where `dt` specifies the time between two successive points in units of 0.1 s. 
`dt` must be in the range 0 to 600 (i.e., up to 1 min between two points). The default is 10 (1 Hz).

A `-t 0` has a special meaning; it is used to set the instrument to a given condition (as above), then quits the software immediately. 
This implies `-k` and `-n`, and it is the only time that no output filename is required.

Example: set output voltage to 12 V, current limit to 1 A, overcurrent trip on, then quit (`-t 0`):

    ./hp6633 -u 12 -i 1 -I -t 0

**Voltage ramps** are specified using option `-r`, followed by the voltage step in Milivolts (!). To run the ramp up and down, use option `-R`. 

The ramp start voltage is set with -u , the ramp end voltage with -U.

The time between two successive steps is set with option "-t", as above. 

Thus, to ramp the output voltage in 100-mV increments (`-r 100`) from 0 to 15 V (`-U 15`) in intervals of 0.1 s (`-t 1`):

    ./hp6633 -U 15 -r 100 -t 1 /path/to/file

Same as above, but ramp up and down (`-R`):

    ./hp6633 -U 15 -r 100 -R -t 1 /path/to/file

Same as above, but now start at 15 V and ramp *down* to 0 V, then up again. The trick is to specify a negative ramp voltage (`-r -100`):

    ./hp6633 -U 15 -r -100 -R -t 1 /path/to/file

Same as before, but start at 6 V:   

    ./hp6633 -u 6 -U 15 -r -100 -R -t 1 /path/to/file
    
The other options should be rather self-explaining ;-)

## Exit code

Exit code is
- 0 if program execution was successful,
- 1 if error in command line option
- 4 if file i/o problem
- 5 if communication problem with instrument


# Re-displaying the Data

The data files are tab-delimited ASCII files. To (re)display these data e.g. using `gnuplot`, use something along the following lines:

- for a "standard" plot (voltage and current over time):

      set style data lines
      set grid xt
      set grid yt
      set xlabel 'min'
      set ylabel 'V'
      set y2label 'A'
      set title 'filename'
      plot 'filename' using 1:2 title 'Voltage', '' u 1:3 axis x1y2 title 'Current'

- for data from voltage ramping (current over voltage):

      set style data lines
      set grid xt
      set grid yt
      set xlabel 'V'
      set ylabel 'A'
      set title 'filename'
      plot 'filename' using 2:3 index 0 title 'I vs. U (1)', '' u 2:3 index 1 title 'I vs. U (2)'

## License
This program and its documentation are Copyright (c) 2005...2025 Joerg Hau.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 (**not** "any later version") as published
by the Free Software Foundation, provided that the copyright notice
remains intact even in future versions. See the file LICENSE for details.

If you use this program (or any part of it) in another application, note
that the resulting application becomes also GPL. In other words, GPL is a
"contaminating" license.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License (file
LICENSE) for more details.


## ToDo
Things that are not yet implemented:
- Reading of instrument error conditions
- use of GPIB interrupt control

## History
See top of the hp6633.c file.

I have developed this software on a Pentium-II 400 MHz, later a Pentium-III 600 MHz with SuSE Linux 9.0, Kernel 2.4 and linux-gpib-3.1.101. 
In 2016, this same system was upgraded to Debian 8 with Kernel 3.16 and linux-gpib-4.0.2. 
As of 2025, we're on Debian 12 and no changes were necessary :-)

## Bugs
If you find any bugs related to this software, or if you want to contribute code, feel free to use the github tools.

In case of bugs: Please make sure you can reproduce the problem, and that the problem is really related to *this* software. 
As an example, I will _not_ answer any questions about compiling and/or configuring your GPIB hardware.


Thank you for your interest, and ... have fun!
