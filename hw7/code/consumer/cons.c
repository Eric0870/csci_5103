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
    char item[32], *sitems;

    // usage
    if ( argc != 2 )
    {
        printf( "Usage: ./prod <num items> \n" );
        exit( 1 );
    }

    // open device for read
    if ( (fid = open("/dev/scullbuffer0", O_RDONLY)) < 0 )
    {
        printf( "CONS: Unable to open /dev/scullbuffer0 \n" );
        exit( 1 );
    }

    // parse command line inputs
    sitems = argv[1];

    // get number of items from command line argument
    len    = strlen(sitems);
    nitems = 0;
    for ( ii=0; ii<len; ii++ )
    {
        nitems += ( sitems[ii] - '0' ) * (int)pow( 10.0, (double)(len-ii-1) );
    }
    printf( "CONS: configured for %d items \n", nitems );

    // as a convenience, nap long enough for operator to start producer process
    sleep( WAIT_TIME_SEC );

    // loop to consume required number of items
    done = false;
    for ( ii=0; ii<nitems; ii++ )
    {
        // consume item
        nbytes = read( fid, item, 32 );
        switch ( nbytes )
        {
            case -1:
                printf( "CONS: Error occured during read \n" );
                break;
            case 0:
                printf( "CONS: Buffer empty, and no producer processes currently have scullbuffer open for writing \n" );
                done = true; // exit application
                break;
            default:
                printf( "CONS: %d bytes read from scullbuffer \n", nbytes );
        }

        if ( done )
        {
            printf( "CONS: Exiting application early\n" );
            break;
        }

        // DEBUG
        printf( "CONS: consumed item from buffer %s \n", item );
    }
    printf( "\nCONS: consumed %d items \n\n", ii );

    // close device
    close( fid );

    // exit application
    exit( 0 );
}
