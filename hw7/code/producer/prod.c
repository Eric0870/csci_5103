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
 *  - produce items and deposit them in scullbuffer0 device
 *  - accept command line input for number of items to produce
 *  - accept command line input for producer tag
 *
 * Considerations
 *  - exit gracefully (no crash or hang) regardless of buffer state
 *
 * Code reuse
 *  - none
 *
**********************************************************************************************/

/* includes */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

#define WAIT_TIME_SEC 2.0

int main( int argc, char *argv[] )
{
    int fid, nbytes, ii, nitems, len, done;
    char item[32], scntr[16], *ptag, *sitems;
    static long int icntr = 0;

    // usage
    if ( argc != 3 )
    {
        printf( "Usage: ./prod <num items> <producer tag> \n" );
        exit( 1 );
    }

    // open device for read and write
    if ( (fid = open("/dev/scullbuffer0", O_RDWR)) < 0 )
    {
        printf( "PROD: Unable to open /dev/scullbuffer0 \n" );
        exit( 1 );
    }

    // parse command line inputs
    sitems = argv[1];
    ptag   = argv[2];

    // get number of items from command line argument
    len    = strlen(sitems);
    nitems = 0;
    for ( ii=0; ii<len; ii++ )
    {
        nitems += ( sitems[ii] - '0' ) * (int)pow( 10.0, (double)(len-ii-1) );
    }
    printf( "PROD: configured for %d items \n", nitems );

    // as a convenience, nap long enough for operator to start consumer process
    sleep( WAIT_TIME_SEC );

    // loop to produce and deposit required number of items
    done = false;
    for ( ii=0; ii<nitems; ii++ )
    {
        // item is 32 bytes wide
        // to guard against overflow, use the following design
        //   - up to 18 bytes used to hold producer tag
        //     -- preamble: "producer_tag_" (13 bytes)
        //     -- cmd line: <argv[1]> (up to 5 bytes)
        //     -- separate tag and counter with "_" (1 byte)
        //   - up to 13 bytes uses to hold produced item counter
        //   - 1 byte to hold NULL terminator for item

        // initialization
        memset( item,  '\0', sizeof(item) );
        memset( scntr, '\0', sizeof(scntr) );

        // prepend producer tag to item, including command line component
        strcpy(  item, "producer_tag_" );
        strncat( item, ptag, 5 );
        strcat(  item, "_" );

        // copy up to 13 digits of icntr to item
        snprintf( scntr, 13, "%ld", ++icntr );
        strcat( item, scntr );

        printf( "PROD: created item %s \n", item );

        // deposit item
        nbytes = write( fid, item, 32 );
        switch ( nbytes )
        {
            case -1:
                printf( "PROD: Error occured during write \n" );
                break;
            case 0:
                printf( "PROD: Buffer full, and no consumer processes currently have scullbuffer open for reading \n" );
                done = true; // exit application
                break;
            default:
                printf( "PROD: %d bytes written to scullbuffer \n", nbytes );
        }

        if ( done )
        {
            printf( "PROD: Exiting application early\n" );
            break;
        }
    }
    printf( "\nPROD: added %d items to buffer\n\n", ii );

    // close device
    close( fid );

    // exit application
    exit( 0 );
}
