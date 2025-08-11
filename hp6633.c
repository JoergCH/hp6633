/* vi:set syntax=c expandtab tabstop=4 shiftwidth=4:

 H P 6 6 3 3 . C

 Controls the HP663[2,3,4]A Power Supply using GPIB.

 Copyright (c) 2005...2025 by Joerg Hau.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License version 2 as
 published by the Free Software Foundation, provided that the copyright
 notice remains intact even in future versions. See the file LICENSE
 for details.

 If you use this program (or any part of it) in another application,
 note that the resulting application becomes also GPL. In other
 words, GPL is a "contaminating" license.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 --------------------------------------------------------------------

 Modification/history (adapt VERSION below when changing!):

 2005-10-01     creation based on s7150.c (JHa)
 2005-10-02     added downramping and 'keep' flag (JHa)
 2005-12-04     improved plotting (JHa)
 2005-12-05     added error checks; added pre-defined parameters for
                the HP6632 and HP6634(JHa)
 2005-12-11     fixed reading of comment text (JHa)
 2011-09-03     fixed gnuplot error when reading indexed files (JHa)
 2013-06-08     added parameter for ramp start voltage, 
                fixed display of single-ramp data (JHa)
 2015-11-22     cmd line switch to omit "press any key" (JHa)
 2016-02-15     bug fix around init error msg
 2016-02-17     updated doc (JHa)
 2017-01-23     minor bug fix around keyboard handling (JHa)
 2025-08-11     moved everything to GitHub (JHa)
 
 This should compile with any C compiler, something like:

 gcc hp6633.c -Wall -O2 -lgpib -o hp6633 

 You may want to rename the output file if you use a 6632 or 6634 ;-)

 Make sure the user accessing GPIB devices is in group 'gpib'.

 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>          /* command line reading */
#include <unistd.h>
#include <termios.h>        /* kbhit() */
#include <sys/io.h>
#include <sys/time.h>       /* clock timing */
#include "gpib/ib.h"

#define VERSION "V20250811"    /* String! */
#define GNUPLOT "gnuplot"      /* gnuplot executable */

#define MAXLEN   81         /* text buffers etc */
#define ESC      27

#define ERR_FILE  4         /* error code */
#define ERR_INST  5         /* error code */

#define GPIB_BOARD_ID 0     /* GPIB card #, default is 0 */

/* --- specific settings for HP6632, 6634, 6635 --- */

#define HP6633

#ifdef HP6632
#define MAXVOLT 25
#define MAXAMP  4
#endif

#ifdef HP6633
#define MAXVOLT 50
#define MAXAMP  2
#endif

#ifdef HP6634
#define MAXVOLT 100
#define MAXAMP  1
#endif

/* --- stuff for reading the command line --- */

char *optarg;               /* global: pointer to argument of current option */
int optind = 1;             /* global: index of which argument is next. Is used
                            as a global variable for collection of further
                            arguments (= not options) via argv pointers */

/* --- stuff for kbhit() ---- */

static  struct termios initial_settings, new_settings;
static  int peek_character = -1;

void    init_keyboard(void);
void    close_keyboard(void);
int     kbhit(void);
int     readch(void);

/* --- miscellaneous function prototypes ---- */

double  timeinfo (void);
int     strclean (char *buf);
int     GetOpt (int argc, char *argv[], char *optionS);

/* --- hp663X-related function prototypes ---- */

int     hp663X_open (const int adr, const char do_reset);
int 	hp663X_set (const int inst, const char cmd[], const float val);
int     hp663X_setup (const int inst, const float volt, \
                     const float amp, const float limvolt, const char ocp);
int     hp663X_read (const int inst, const char what[], char *result);
int     hp663X_close (const int adr, const char do_reset);



/********************************************************
* main:       main program loop.                        *
* Input:      see below.                                *
* Return:     0 if OK, else error code                  *
********************************************************/
int main (int argc, char *argv[])
{
static char *disclaimer =
"\nhp6633 - Control of the HP6633A Power Supply over GPIB. " VERSION ".\n"
"Copyright (C) 2005...2025 by Joerg Hau.\n\n"
"This program is free software; you can redistribute it and/or modify it under\n"
"the terms of the GNU General Public License, version 2, as published by the\n"
"Free Software Foundation.\n\n"
"This program is distributed in the hope that it will be useful, but WITHOUT ANY\n"
"WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A\n"
"PARTICULAR PURPOSE. See the GNU General Public License for details.\n\n";

static char *msg = "\nSyntax: %s [-h] [-a id] [-u setV] [-U upperV] [-M maxV] [-i A] [-I] [-r dV] [-R] [-t dt] [-k] [-K] [-c txt] [-n | -g /path/to/gnuplot] [-f] outfile"
"\n        -h       this help screen"
"\n        -a id    use instrument at GPIB address 'id' (default is 5)"
"\n        -u V     set actual voltage to 'V' Volt"
"\n        -U V     set upper ramp voltage to 'V' Volt"
"\n        -M V     set voltage limiter to 'V' Volt"
"\n        -i A     set current limiter to 'A' Ampere"
"\n        -I       enable overcurrent trip (default off)"
"\n        -r dV    ramp voltage by increment 'dV' mV (default 0 mV)"
"\n        -R       run ramp up and down (default is one-way)"
"\n        -t dt    delay between measurements or steps in 0.1 s (default is 10;"
"\n                 '0' quits after setting parameters and implies -k and -n)"
"\n        -k       keep settings before and after run (default: switches off)"
"\n        -K       do not ask for keypress before exit (default: wait for key)"
"\n        -w x     force write to disk every x samples (default 100)"
"\n        -f       force overwriting of existing output file"
"\n        -c txt   comment text"
"\n        -g       specify path/to/gnuplot (if not in your current PATH)"
"\n        -n       no graphics\n\n";

FILE    *outfile = NULL,
        *gp = NULL;         /* will be a pipe to gnuplot */
char    buffer[MAXLEN], filename[MAXLEN], comment[MAXLEN] = "", gnuplot[MAXLEN];
char    do_graph = 1,       /* use graphics */
        do_overwrite = 0,   /* force overwriting existing output file */
        do_keypress = 1,    /* wait for keypress at the end */
        do_ocp = 0,         /* use overcurrent trip */
        do_reset = 1,       /* do reset after run */
        dramp = 0,          /* do dual ramp */
        dramp_avail = 0;    /* dual ramp second dataset is available */
int     inst, pad=5, key, do_flush = 100, delay = 10, ramp = 0;
unsigned long loop = 0L;
double  t0, t1;             /* timer */
float	volt, amp, ramp_volt=0.0, set_volt=0.0, max_volt=0.0, set_limvolt=MAXVOLT, set_amp=MAXAMP;
time_t  t;

/* --- set the gnuplot executable --- */

sprintf (gnuplot, "%s", GNUPLOT);

/* --- show the usual text --- */

fprintf (stderr, disclaimer);

/* --- decode and read the command line --- */

while ((key = GetOpt(argc, argv, "hfnkKIRu:U:i:M:a:w:t:c:g:r:")) != EOF)
    switch (key)
        {
        case 'h':                   /* help me */
            fprintf (stderr, msg, argv[0]);
            return 0;
        case 'f':                   /* force overwriting of existing file */
            do_overwrite = 1;
            continue;
        case 'n':                   /* disable graph display */
            do_graph = 0;
            continue;
        case 'k':                   /* "keep", do not reset at end */
            do_reset = 0;
            continue;
        case 'K':                   /* do not wait for keypress at end */
            do_keypress = 0;
            continue;
        case 'I':
            do_ocp = 1;             /* overcurrent protection (trip) on */
            continue;
        case 'R':
            dramp = 1;              /* Dual Ramp on */
            dramp_avail=0;          /* no data acquired yet */
            continue;
        case 'c':
            if (strclean (optarg))    
                strcpy (comment, optarg);
            continue;
        case 'g':                   /* path to gnuplot */
            sscanf (optarg, "%80s", gnuplot);
            continue;
        case 'u':                   /* set output voltage */
            sscanf (optarg, "%8f", &set_volt);
            if (set_volt < 0.0 || set_volt > MAXVOLT)
                {
                fprintf (stderr, "Error: Voltage must be in range 0...%d V.\n", MAXVOLT);
                return 1;
                }
            continue;
        case 'U':                   /* set max. ramp voltage */
            sscanf (optarg, "%8f", &max_volt);
            if (max_volt < 0.0 || max_volt > MAXVOLT)
                {
                fprintf (stderr, "Error: Voltage must be in range 0...%d V.\n", MAXVOLT);
                return 1;
                }
            continue;
        case 'M':                   /* set max. output voltage (limiter) */
            sscanf (optarg, "%8f", &set_limvolt);
            if (set_limvolt < 0.0 || set_limvolt > MAXVOLT)
                {
                fprintf (stderr, "Error: Voltage limit must be in range 0...%d V.\n", MAXVOLT);
                return 1;
                }
            continue;
        case 'i':                   /* set output current limiter */
            sscanf (optarg, "%8f", &set_amp);
            if (set_amp < 0.0 || set_amp > MAXAMP)
                {
                fprintf (stderr, "Error: Current limit must be in range 0...%d A.\n", MAXAMP);
                return 1;
                }
            continue;
        case 'r':                   /* set output voltage increment (mV) for ramp */
            sscanf (optarg, "%6d", &ramp);
            if (abs(ramp) < 1 || abs(ramp) > 1000)
                {
                fprintf (stderr, "Error: Ramp steps must be in range (+/-)1...1000 mV.\n");
                return 1;
                }
            continue;
        case 'w':                   /* flush buffer every ... events */
            sscanf (optarg, "%5d", &do_flush);
            if (do_flush < 1 || do_flush > 10000)
                {
                fprintf (stderr, "Error: Flush must occur every 1...10000 points.\n");
                return 1;
                }
            continue;
        case 'a':                   /* GPIB address */
            sscanf (optarg, "%5d", &pad);
            if (pad < 0 || pad > 30)
                {
                fprintf(stderr, "Error: primary address must be between 0 and 30.\n");
                return 1;
                }
            continue;
        case 't':                   /* delay between measurements/steps, in units of 100 ms */
            sscanf (optarg, "%5d", &delay);
            if (delay < 0 || delay > 600)	/* delay == 0 is special, see below */
                {
                fprintf(stderr, "Error: delay must be 1 ... 600 (1/10 s).\n");
                return 1;
                }
            continue;
        case '~':                    /* invalid arg */
        default:
            fprintf (stderr, "'%s -h' for help.\n\n", argv[0]);
            return 1;
        }

/* more error checking */
if ((ramp) && (max_volt < set_volt))
    {
    fprintf (stderr, "Error: Upper ramp voltage (-U) must be higher than set voltage (-u).\n");
    return 1;
    }

if ((ramp) && (max_volt > set_limvolt))
    {
    fprintf (stderr, "Error: Upper ramp voltage (-U) must be less than voltage limit (-M).\n");
    return 1;
    }
    
/* if delay is > 0, we need at least one parameter on cmd line */
if ((argv[optind] == NULL) && (delay > 0))
    {
    fprintf (stderr, msg, argv[0]);
    fprintf (stderr, "Please specify a data file.\n");
    return 1;
    }

if (delay == 0)  /* if delay is 0, we will set instrument values and exit */
    {
    do_graph = 0;
    do_reset = 0;
    }
else            /* if delay is > 0, prepare output data file */
    {
    strcpy (filename, argv[optind]);
    if ((!access(filename, 0)) && (!do_overwrite))  /* If file exists and overwrite is NOT forced */
        {
        fprintf (stderr, "\a\nFile '%s' exists - Overwrite? [Y/*] ", filename);
        key = fgetc(stdin);         // read from keyboard
        switch (key)
            {
            case 'Y':
            case 'y':
                break;
                default:
                return 1;
            }
        }

    if (NULL == (outfile = fopen(filename, "wt")))
        {
        fprintf(stderr, "Could not open '%s' for writing.\n", filename);
        pclose(gp);
        return ERR_FILE;
        }

    /* --- prepare gnuplot for action --- */
    gp = popen(gnuplot,"w");
    if (NULL == gp)
        {
        fprintf(stderr, "\nCannot launch gnuplot, will continue \"as is\".\n") ;
        fflush(stderr);
        do_graph = 0;    /* do NOT abort here, just continue */
        }
}

if (do_graph)       /* set gnuplot display defaults */
    {
    fprintf(gp, "set mouse;set mouse labels; set style data lines; set title '%s'\n", filename);
    fprintf(gp, "set grid xt; set grid yt\n");
    if (ramp)	/* if ramping is desired, we plot I vs. U ... else plot U and I over time */
    	fprintf(gp, "set xlabel 'V'; set ylabel 'A'\n");
    else
    	fprintf(gp, "set xlabel 'min'; set ylabel 'V'; set y2label 'A'; set y2tics\n");
    fflush (gp);
    }

/* preparations are finished, now let's get it going ... */

inst = hp663X_open(pad, do_reset);
if (inst == 0)
    {
    fprintf(stderr, "Quit.\n");
    if (gp) 
        pclose(gp);
    return ERR_INST;
    }

if (0 == hp663X_setup(inst, (ramp > 0 ? 0.0 : set_volt), set_amp, set_limvolt, do_ocp))
    {
    fprintf(stderr, "Quit.\n");
    if (gp) 
        pclose(gp);
    return ERR_INST;
    }

if (delay == 0)
	goto end;	/* my first 'goto' for many years ;-) */

printf("\n GPIB address :  %d", pad);
printf("\n  Output file :  %s", filename);
if (strlen(comment))
	printf("\n      Comment :  %s", comment);
printf("\nVoltage limit :  %.4f V", set_limvolt);
printf("\nCurrent %5s :  %.4f A", do_ocp ? "trip" : "limit", set_amp);
printf("\n     Sampling :  %.1f s", delay/10.0);
if (ramp)
    {
    printf("\n   Ramp start :  %.4f V", set_volt);
    printf("\n     Ramp end :  %.4f V", max_volt);
    printf("\n    Increment :  %d mV", ramp);
    }
printf("\n      Refresh :  %d", do_flush);
printf("\n         Stop :  Press 'q' or ESC.\n");
printf("\n     Count           Time      Reading\n");
fflush(stdout);

/* Get time, write file header */
time(&t);
fprintf(outfile, "# hp6633 " VERSION "\n");
fprintf(outfile, "# %s\n", comment);
fprintf(outfile, "# Start: %s", ctime(&t));
fprintf(outfile, "# min\tVolt\tAmpere\n");

/* if ramp is positive, run from set_volt to max_volt
   if ramp is negative, run from max_volt to set_volt
*/
ramp_volt = (ramp > 0  ? set_volt : max_volt);

t0 = timeinfo();
init_keyboard();    /* for kbhit() functionality */

key = 0;
do  {
    if (ramp)    /* != 0, i.e. if voltage ramping was desired */
	{
	/* exit the loop if voltage limit reached */
	if (((ramp > 0) && (ramp_volt > max_volt)) || ((ramp < 0) && (ramp_volt < set_volt)))
	    {
	    if (!dramp)		/* if no dual-ramping is desired ... */
		{
		key = ESC; 	/* ... exit the loop here */
	    	break;
		}
	    else		/* if dual-ramping was desired ... */
		{
		ramp = -ramp;	    /* invert ramp polarity, */
                dramp_avail = 1;    /* set flag */
  		dramp = 0;	/* and adjust this so that the loop will be quit next time */

        /* and print two empty lines into file, allowing gnuplot to use 'index' */
		fprintf(outfile, "\n\n");
		}
	    }
	ramp_volt += ramp*0.001; /* note: ramp is given in mV */
	if (0 == hp663X_set(inst, "VSET", ramp_volt))
	    {
	    fprintf(stderr, "Quit.\n");
	    if (gp) 
                pclose(gp);
	    fclose (outfile);
	    close_keyboard();
	    return ERR_INST;
	    }
	}

    usleep (delay * 100000); 	/* wait (delay * 0.1) s */
    t1 = (timeinfo()-t0)/60.0;  /* get actual time */

    /* read 'real' output voltage */
    if (0 == (hp663X_read(inst, "VOUT?", buffer)))
        {
        fprintf(stderr, "Quit.\n");
        if(gp)
	    pclose(gp);
	fclose (outfile);
        close_keyboard();
        return ERR_INST;
        }
    sscanf (buffer, "%f", &volt);

    /* read output current */
    if (0 == (hp663X_read(inst, "IOUT?", buffer)))
        {
        fprintf(stderr, "Quit.\n");
        if (gp)
            pclose(gp);
	fclose (outfile);
        close_keyboard();
        return ERR_INST;
        }
    sscanf (buffer, "%f", &amp);

    /* show data to screen and write them to file */
    printf("%10lu %10.2f min %10.4f V %10.4f A\r", ++loop, t1, volt, amp);
    fprintf(outfile, "%.4f\t%.4f\t%.4f\n", t1, volt, amp);
    fflush (stdout);

    /* ensure write & display at least every x data points */
    if (!(loop % do_flush))
        {
        fflush (outfile);
        if (do_graph)
            {
    	    if (ramp)	/* if ramping is desired, we plot I vs. U ... else plot U and I over time */
                {
		        if (dramp_avail)
                    fprintf(gp, "plot '%s' using 2:3 index 0 ti 'I vs. U (1)', '' u 2:3 index 1 ti 'I vs. U (2)'\n", filename);      
                else    
                    fprintf(gp, "plot '%s' using 2:3 ti 'I vs. U (1)'\n", filename);
                }    
	        else
	            fprintf(gp, "plot '%s' using 1:2 title 'Voltage', '' u 1:3 axis x1y2 title 'Current'\n", filename);
            fflush (gp);
            }
        }

    /* look up keyboard for keypress */
    if(kbhit())
        key = readch();
    }
    while ((key != 'q') && (key != ESC));

time(&t);
fprintf(outfile, "# Stop: %s\n", ctime(&t));
fclose (outfile);

end:

/* terminate, evtl. send reset to instrument */
if (! hp663X_close(inst, do_reset))
    {
    fprintf(stderr, "Quit.\n");
    if (gp) 
        pclose(gp);
    close_keyboard();
    return ERR_INST;
    }

if (do_graph)   /* if graphic display was used, replot of data (using same cmd as above) */
    {
    if (ramp)   /* if ramping is desired, we plot I vs. U ... else plot U and I over time */
        {
		if (dramp_avail)
            fprintf(gp, "plot '%s' using 2:3 index 0 ti 'I vs. U (1)', '' u 2:3 index 1 ti 'I vs. U (2)'\n", filename);      
        else    
            fprintf(gp, "plot '%s' using 2:3 ti 'I vs. U (1)'\n", filename);
        }    
    else
        fprintf(gp, "plot '%s' using 1:2 title 'Voltage', '' u 1:3 axis x1y2 title 'Current'\n", filename);
    fflush (gp);

    if (do_keypress)    /* wait for user input */
        {
        printf("\nAcquisition finished. Press any key to terminate graphic display and exit.\n");
        while (!kbhit())
            usleep (100000); 	/* wait 0.1 s */
        }
    pclose(gp);
    }

close_keyboard();
printf("\n");
return 0;
}


/********************************************************
* hp663X_open: Connect and initialise HP6633A           *
* Input:       - GPIB address                           *
* 	       - flag if dev should be cleared (0 = no) *
* Return:      0 if error, file descriptior if OK       *
********************************************************/
int hp663X_open (const int pad, const char do_reset)
{
int inst;
static char buf[MAXLEN];

inst = ibdev(GPIB_BOARD_ID, pad, 0, T1s, 1, 0);
if(inst < 0)
    {
    fprintf(stderr, "Error trying to open GPIB address %i\n", pad);
    return 0;
    }

/*  if requested, perform a device clear */
if (do_reset)
    {
    strcpy (buf, "OUT 0;RST;CLR\n");
    if (ibwrt(inst, buf, strlen(buf)) & ERR )
        {
        fprintf(stderr, "Error during init of GPIB address %i!\n", pad);
        return 0;
        }
    sleep (1);
    }

/* arrive here if OK */
return inst;
}



/********************************************************
* hp663X_set: Sets one parameter of the HP6633A      	*
* Input:    - file pointer delivered by hp663X_open()   *
*           - instruction ("VSET", ...)			*
*	    - value for instruction ('13.6', ...)	*
* Return:   1 if OK, 0 if error                         *
********************************************************/
int hp663X_set (const int inst, const char cmd[], const float val)
{
static char buf[MAXLEN];

sprintf (buf, "%s %f\n", cmd, val);
if (ibwrt(inst, buf, strlen(buf)) & ERR )
    {
    fprintf(stderr, "Error executing '%s'!\n", buf);
    return 0;
    }
return 1;
}


/********************************************************
* hp663X_setup: Sets operating mode of the HP6633A      *
* Input:    - file pointer delivered by hp663X_open()   *
*           - volt, amp, limvolt, ocp                   *
* Return:   1 if OK, 0 if error                         *
********************************************************/
int hp663X_setup (const int inst, const float volt, 
    const float amp, const float limvolt, const char ocp)
{
static char buf[MAXLEN];

sprintf (buf, "VSET %f;ISET %f;OVSET %f;OCP %d\n", volt, amp, limvolt, (ocp ? 1:0));
if (ibwrt(inst, buf, strlen(buf)) & ERR )
    {
    fprintf(stderr, "Error during mode setting!\n");
    return 0;
    }
return 1;
}


/********************************************************
* hp663X_read: Reads voltage or current from HP6633.    *
* Input:    - file ptr as delivered by hp663X_open()    *
*           - instruction ("VOUT?", "IOUT?", ...)       *
*           - ptr to char for result                    *
* Return:   1 if OK, 0 if error                         *
********************************************************/
int hp663X_read (const int inst, const char what[], char *result)
{
static char buf[MAXLEN];

/* send query string to instrument */
sprintf (buf, "%s\n", what);
if (ibwrt(inst, buf, strlen(buf)) & ERR )
    {
    fprintf(stderr, "Error during read!\n");
    return 0;
    }

/* read from instrument.The HP6633A sends always (?) 9 characters, 
   the last 2 are a CR/LF sequence ... example:
   VOUT? --> ' 12.009'
   IOUT? --> '-0.0005'
 */
if(ibrd(inst, result, 11) & ERR)
    {
    fprintf(stderr, "Error trying to read from instrument!\n");
    return 0;
    }

//printf("\nreceived string:'%s', number of bytes read: %i\n", result, ibcnt);

/* make sure string is null-terminated; 
   at the same time, cut off CR/LF  */
result[ibcnt-2] = 0x0;        

return 1;
}



/*********************************************************
* hp663X_close: Reset and switch off HP66330             *
* Input:    - file pointer as delivered by hp663X_open() *
*           - flag if reset should be performed (0 = no) *
* Return:   0 if error, 1 if OK                          *
*********************************************************/
int hp663X_close (const int inst, const char do_reset)
{
static char buf[15];

/*  if requested, perform a device clear */
if (do_reset)
    {
    strcpy (buf, "OUT 0;RST;CLR\n");
    if (ibwrt(inst, buf, strlen(buf)) & ERR )
        {
        fprintf(stderr, "Error during reset of instrument!\n");
        return 0;
        }
    }
return 1;
}


/********************************************************
* TIMEINFO: Returns actual time elapsed since The Epoch *
* Input:    Nothing.                                    *
* Return:   time in microseconds                        *
* Note:     #include <time.h>                           *
*           #include <sys/time.h>                       *
********************************************************/
double timeinfo (void)
{
struct timeval t;

gettimeofday(&t, NULL);
return (double)t.tv_sec + (double)t.tv_usec/1000000.0;
}


/************************************************************************
* Function:     strclean                                                *
* Description:  "cleans" a text buffer obtained by fgets()              *
* Arguments:    Pointer to text buffer                                  *
* Returns:      strlen of buffer                                        *
*************************************************************************/
int strclean (char *buf)
{
int i;

for (i = 0; i < strlen (buf); i++)    /* search for CR/LF */
    {
    if (buf[i] == '\n' || buf[i] == '\r')
        {
        buf[i] = 0;        /* stop at CR or LF */
        break;
        }
    }
return (strlen (buf));
}


/********************************************************
* KBHIT: provides the functionality of DOS's kbhit()    *
* found at http://linux-sxs.org/programming/kbhit.html  *
* Input:    Nothing.                                    *
* Return:   time in microseconds                        *
* Note:     #include <termios.h>                        *
********************************************************/
void init_keyboard (void)
{
tcgetattr( 0, &initial_settings );
new_settings = initial_settings;
new_settings.c_lflag &= ~ICANON;
new_settings.c_lflag &= ~ECHO;
new_settings.c_lflag &= ~ISIG;
new_settings.c_cc[VMIN] = 1;
new_settings.c_cc[VTIME] = 0;
tcsetattr( 0, TCSANOW, &new_settings );
}

void close_keyboard(void)
{
tcsetattr( 0, TCSANOW, &initial_settings );
}

int kbhit (void)
{
char ch;
int nread;

if( peek_character != -1 )
    return( 1 );
new_settings.c_cc[VMIN] = 0;
tcsetattr( 0, TCSANOW, &new_settings );
nread = read( 0, &ch, 1 );
new_settings.c_cc[VMIN] = 1;
tcsetattr( 0, TCSANOW, &new_settings );
if( nread == 1 )
    {
    peek_character = ch;
    return (1);
    }
return (0);
}

int readch (void)
{
char ch;

if( peek_character != -1 )
    {
    ch = peek_character;
    peek_character = -1;
    return( ch );
    }
/* else */
read( 0, &ch, 1 );
return( ch );
}


/***************************************************************************
* GETOPT: Command line parser, system V style.
*
*  Widely (and wildly) adapted from code published by Borland Intl. Inc.
*
*  Note that libc has a function getopt(), however this is not guaranteed
*  to be available for other compilers. Therefore we provide *this* function
*  (which does the same).
*
*  Standard option syntax is:
*
*    option ::= SW [optLetter]* [argLetter space* argument]
*
*  where
*    - SW is '-'
*    - there is no space before any optLetter or argLetter.
*    - opt/arg letters are alphabetic, not punctuation characters.
*    - optLetters, if present, must be matched in optionS.
*    - argLetters, if present, are found in optionS followed by ':'.
*    - argument is any white-space delimited string.  Note that it
*      can include the SW character.
*    - upper and lower case letters are distinct.
*
*  There may be multiple option clusters on a command line, each
*  beginning with a SW, but all must appear before any non-option
*  arguments (arguments not introduced by SW).  Opt/arg letters may
*  be repeated: it is up to the caller to decide if that is an error.
*
*  The character SW appearing alone as the last argument is an error.
*  The lead-in sequence SWSW ("--") causes itself and all the rest
*  of the line to be ignored (allowing non-options which begin
*  with the switch char).
*
*  The string *optionS allows valid opt/arg letters to be recognized.
*  argLetters are followed with ':'.  Getopt () returns the value of
*  the option character found, or EOF if no more options are in the
*  command line. If option is an argLetter then the global optarg is
*  set to point to the argument string (having skipped any white-space).
*
*  The global optind is initially 1 and is always left as the index
*  of the next argument of argv[] which getopt has not taken.  Note
*  that if "--" or "//" are used then optind is stepped to the next
*  argument before getopt() returns EOF.
*
*  If an error occurs, that is an SW char precedes an unknown letter,
*  then getopt() will return a '~' character and normally prints an
*  error message via perror().  If the global variable opterr is set
*  to false (zero) before calling getopt() then the error message is
*  not printed.
*
*  For example, if
*
*    *optionS == "A:F:PuU:wXZ:"
*
*  then 'P', 'u', 'w', and 'X' are option letters and 'A', 'F',
*  'U', 'Z' are followed by arguments. A valid command line may be:
*
*    aCommand  -uPFPi -X -A L someFile
*
*  where:
*    - 'u' and 'P' will be returned as isolated option letters.
*    - 'F' will return with "Pi" as its argument string.
*    - 'X' is an isolated option.
*    - 'A' will return with "L" as its argument.
*    - "someFile" is not an option, and terminates getOpt.  The
*      caller may collect remaining arguments using argv pointers.
***************************************************************************/
int GetOpt (int argc, char *argv[], char *optionS)
{
   static char *letP = NULL;    /* remember next option char's location */
   static char SW = '-';    /* switch character */

   int opterr = 1;      /* allow error message        */
   unsigned char ch;
   char *optP;

   if (argc > optind)
   {
      if (letP == NULL)
      {
     if ((letP = argv[optind]) == NULL || *(letP++) != SW)
        goto gopEOF;

     if (*letP == SW)
     {
        optind++;
        goto gopEOF;
     }
      }
      if (0 == (ch = *(letP++)))
      {
     optind++;
     goto gopEOF;
      }
      if (':' == ch || (optP = strchr (optionS, ch)) == NULL)
     goto gopError;
      if (':' == *(++optP))
      {
     optind++;
     if (0 == *letP)
     {
        if (argc <= optind)
           goto gopError;
        letP = argv[optind++];
     }
     optarg = letP;
     letP = NULL;
      }
      else
      {
     if (0 == *letP)
     {
        optind++;
        letP = NULL;
     }
     optarg = NULL;
      }
      return ch;
   }

 gopEOF:
   optarg = letP = NULL;
   return EOF;

 gopError:
   optarg = NULL;
   errno = EINVAL;
   if (opterr)
      perror ("\nCommand line option");
   return ('~');
}

