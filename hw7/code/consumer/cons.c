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
        printf( "Unable to open /dev/scullbuffer0 \n" );
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

    // as a convenience, nap long enough for operator to start producer process
    sleep( 3 );

    // loop to consume required number of items
    done = false;
    for ( ii=0; ii<nitems; ii++ )
    {
        // consume item
        nbytes = read( fid, item, 32 );
        switch ( nbytes )
        {
            case -1:
                printf( "Error occured during read \n" );
                break;
            case 0:
                printf( "Buffer is empty, \n"
                        "  and there are no producer processes \n "
                        "  that currently have scullbuffer open for writing \n" );
                done = true; // exit application
                break;
            default:
                printf( "CONS: %d bytes read from scullbuffer \n", nbytes );
        }

        if ( done )
        {
            printf("Exiting application early\n");
            break;
        }

        // DEBUG
        printf("CONS: consumed item %s \n", item);
    }

    // close device
    close( fid );

    // exit application
    exit( 0 );
}
