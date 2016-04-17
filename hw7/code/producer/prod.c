/*============================================================================
 CSci5103 Spring 2016
 Assignment#        : 7
 Name               : John Erickson
 Student id         : 2336359
 x500 id            : eric0870
 CSELABS machine    : csel-kh4250-03.cselabs.umn.edu
 Virtual machine    : csel-x34-umh.cselabs.umn.edu
 ============================================================================*/

/**********************************************************************************************
 * Requirements
 *  - ...
 *
 * Considerations
 *  - ...
 *
 * Code reuse
 *  - ...
 *
**********************************************************************************************/

/* includes */
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

/* constants */


/* global vars */


/* function prototypes */


int main( int argc, char *argv[] )
{
    int fid, nbytes;

    // open device so that it can be read from and written to
    fid = open( "/dev/scullbuffer", O_RDWR );
    if ( fid < 0 )
    {
        printf( "Unable to open scullbuffer" );
        exit( 1 );
    }

    // deposit item
    nbytes = write( fid, "test output...\n", 15 );
    switch ( nbytes )
    {
        case -1:
            printf( "Error occured during write \n" );
            break;
        case 0:
            printf( "Buffer is full \n"
                    "and there are no consumer processes that currently have scullbuffer open for reading" );
            break;
        default:
            printf( "%d bytes written to buffer", nbytes );
    }

    // close scullbuffer device
    close( fid );

    // exit application
    exit( 0 );
}
