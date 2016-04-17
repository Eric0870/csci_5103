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
#define NUM_ITEMS   1000
#define BUF_SIZE    4
#define MAX_CHARS   100
#define SEC_TO_USEC 1000000

/* global vars */
static char  buf1[BUF_SIZE][MAX_CHARS];
static char  buf2[BUF_SIZE][MAX_CHARS];
static int   buf1_head = 0;
static int   buf2_head = 0;
static int   buf1_tail = 0;
static int   buf2_tail = 0;
static int   buf1_cnt  = 0;
static int   buf2_cnt  = 0;
//
pthread_mutex_t lock;
pthread_cond_t spaceA, itemA;

/* function prototypes */
static void* prod_black( void * );
static void* prod_green( void * );
static void  prod_generic( char * );
static void* consumer( void *arg );
static void  create_item( char *, const char * );
static int   print_item( FILE *, char * );
static void  printids( void );

int main( int argc, char *argv[] )
{
    pthread_t tid_black, tid_green, tid_cons;
    int err;

    printf( "Begin main() thread, " );
    printids();

    /* initialize mutex and conditions */
    pthread_mutex_init( &lock, NULL );
    pthread_cond_init( &spaceA, NULL );
    pthread_cond_init( &itemA, NULL );

    /* spawn black producer thread */
    err  = pthread_create( &tid_black, NULL, prod_black, NULL );
    if ( err )
        printf("Can't create prod_black thread! \n");

    /* spawn green producer thread */
    err  = pthread_create( &tid_green, NULL, prod_green, NULL);
    if ( err )
        printf("Can't create prod_green thread! \n");

    /* spawn consumer thread */
    err  = pthread_create( &tid_cons, NULL, consumer, NULL);
    if ( err )
        printf("Can't create consumer thread! \n");

    /* wait on producer and consumer threads to complete before exiting */
    pthread_join( tid_black, NULL );
    pthread_join( tid_green, NULL );
    pthread_join( tid_cons, NULL );

    printf( "End main() thread \n" );
    exit(0);
}

void* prod_black( void *arg )
{
    prod_generic( "prod_black" );
    return ((void *)0);
}

void* prod_green( void *arg )
{
    prod_generic( "prod_green" );
    return ((void *)0);
}

void prod_generic( char *tname )
{
    /*
     * use generic producer function to minimize code redundancy
     */
    char filename[100];
    int ii;
    FILE *fout;

    printf( "\tBegin %s() thread, ", tname );
    printids();

    /* open output file */
    strcpy(filename, tname);
    strcat(filename, ".txt");
    fout = fopen( filename, "w+" );

    /* enter loop to
     * - create items
     * - add items to buffer for consumer
     * - print items to log file
     */
    for ( ii=0; ii<NUM_ITEMS; ii++ )
    {
        // check for buffer availability
        pthread_mutex_lock( &lock );    // enter critical section
            /* wait until space available in buffer */
            while ( (buf1_cnt + buf2_cnt) == 2*BUF_SIZE )
                while ( pthread_cond_wait(&spaceA, &lock) != 0 );
                    /* sleep using condition variable */
            /*
             * by this point we know there is space in one of the buffers
             * create and add item to available buffer
             */
            if ( buf1_cnt < BUF_SIZE )
            {
                create_item( &buf1[buf1_tail][0], tname );
                print_item( fout, &buf1[buf1_tail][0] );
                buf1_tail = ( buf1_tail + 1 ) % BUF_SIZE;
                buf1_cnt++;
            }
            else
            {
                create_item( &buf2[buf2_tail][0], tname );
                print_item( fout, &buf2[buf2_tail][0] );
                buf2_tail = ( buf2_tail + 1 ) % BUF_SIZE;
                buf2_cnt++;
            }
        pthread_mutex_unlock( &lock );  // exit critical section
        pthread_cond_signal( &itemA );  // signal consumer that there is an item available
    }

    /* close the file */
    fclose( fout );

    printf( "\tEnd %s() thread \n", tname );
}

void* consumer( void *arg )
{
    int ii;
    FILE *fout;

    printf( "\tBegin consumer() thread,   " );
    printids();

    fout = fopen( "output.txt", "w+" );

    for ( ii=0; ii<2*NUM_ITEMS; ii++ )
    {
        pthread_mutex_lock( &lock );    // enter critical section
            /* wait until item available in buffer */
            while ( buf1_cnt + buf2_cnt == 0 )
                while ( pthread_cond_wait(&itemA, &lock) != 0 );
                    /* sleep using condition variable */

            /* by this point we know there is an item in one of the buffers */
            if ( buf1_cnt > 0 )
            {
                print_item( fout, &buf1[buf1_head][0] );
                buf1_head = (buf1_head + 1) % BUF_SIZE;
                buf1_cnt--;
            }
            else
            {
                print_item( fout, &buf2[buf2_head][0] );
                buf2_head = (buf2_head + 1) % BUF_SIZE;
                buf2_cnt--;
            }
        pthread_mutex_unlock( &lock );  // exit critical section
        pthread_cond_signal( &spaceA ); // signal producer that there is space available
    }

    /* close the file */
    fclose( fout );

    printf( "\tEnd consumer() thread \n" );
    return ((void *)0);
}

void create_item( char *item, const char *tname )
{
    /*
     * create item
     * - either black or green item, depending on input color
     * - item includes a unix timestamp in microseconds
     *   -- use seconds field to unwrap timestamps
     * - creation includes a random time delay between 0-100 ms
     */
    struct timeval  mytimeval;
    struct timespec mytimespec;
    char   str[100];

    /* prepend color to item */
    if ( strcmp(tname, "prod_black") == 0 )
        strcpy( item, "BLACK" );
    else
        strcpy( item, "GREEN" );

    /* append timestamp to item */
    gettimeofday( &mytimeval, NULL );
    sprintf( str, " %li", mytimeval.tv_sec*SEC_TO_USEC + mytimeval.tv_usec );
    strcat( item, str );

    /* introduce a random delay between 0-100 ms */
    srand( (unsigned int)mytimeval.tv_sec );    // set seed from time of day
    mytimespec.tv_sec  = 0;
    mytimespec.tv_nsec = rand() % 100000000;    // get random number for delay (in ns)
    nanosleep( &mytimespec, NULL );
}

int  print_item( FILE *fid, char *item )
{
    /* write item to output file */
    fprintf( fid, "%s \n", item );

    return 0;
}

void printids( void )
{
    pid_t     pid = getpid();
    pthread_t tid = pthread_self();
    printf( "pid= %u, tid= %u \n", (unsigned int)pid, (unsigned int)tid );
}
