/*============================================================================
 CSci5103 Spring 2016
 Assignment#        : 4-2
 Name               : John Erickson
 Student id         : 2336359
 x500 id            : eric0870
 CSELABS machine    : csel-kh4250-03.cselabs.umn.edu
 ============================================================================*/

/**********************************************************************************************
 * Requirements
 *  - Create shared memory bank to facilitate communication between 3 threads
 *    -- 2 buffers to hold black and green items
 *       -- each item is a string of the form "<color> <unix_timestamp>"
 *          -- color may be the string "BLACK" or "GREEN" depending on the producer
 *          -- time tag is a local unix timestamp in microseconds
 *    -- head and tail pointers for each buffer
 *    -- mutex variable for mutual exclusion
 *    -- condition variables to signal space and item availability in buffers
 *
 *  - Create 2 producer processes
 *    -- Each producer process creates a specific colored item
 *       -- one producer creates BLACK items and the other producer creates GREEN items
 *       -- each producer creates 1000 items before completing
 *       -- wait for random delay between 0-100 ms before producing an item
 *    -- deposit item in available buffer in shared memory
 *    -- write item to output file
 *
 *  - Create a consumer process
 *    -- read contents of shared memory
 *    -- write contents to output file
 *
 * Considerations
 *  - Use POSIX API for thread management and synchronization
 *
 * Code reuse
 *  - Lecture Notes 6
 *  - homework assignment 1
 *
**********************************************************************************************/

/* includes */
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/shm.h>
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
typedef struct
{
    char  buf1[BUF_SIZE][MAX_CHARS];
    char  buf2[BUF_SIZE][MAX_CHARS];
    int   buf1_head;
    int   buf2_head;
    int   buf1_tail;
    int   buf2_tail;
    int   buf1_cnt;
    int   buf2_cnt;
    //
    pthread_mutex_t lock;
    pthread_cond_t spaceA;
    pthread_cond_t itemA;
} SH_MEM_OBJ;
//
static key_t key;        /* key to access shared memory segments */
static int   flag;       /* r/w permissions */

/* function prototypes */
static void  prod_generic( char * );
static void  consumer( void );
static void  create_item( char *, const char * );
static int   print_item( FILE *, char * );
static void  printids( void );

int main( int argc, char *argv[] )
{
    int size; /* memory size in bytes */
    static int         shmem_id;   /* shared memory identifier */
    static SH_MEM_OBJ *shmem_ptr;  /* pointer to shared segment */
    //
    pthread_mutexattr_t attr_sh_lock;
    pthread_condattr_t  attr_sh_spaceA;
    pthread_condattr_t  attr_sh_itemA;

    printf( "Begin main() thread, " );
    printids();

    /*******************************************
     * Create shared memory bank
     *******************************************/
    key  = 4455;
    flag = 1023;    /* 1023 = 111111111b (all permissions and modes set) */
    size = sizeof( SH_MEM_OBJ );

    /* First, create a shared memory segment */
    shmem_id = shmget(key, size, flag);
    if (shmem_id == -1)
    {
        perror("shmget failed");
        exit(1);
    }

    /* Now attach the new segment into my address space */
    shmem_ptr = shmat(shmem_id, (void *) NULL, flag);
    if (shmem_ptr == (void *) -1)
    {
        perror("shmat failed");
        exit(2);
    }

    /* initialize buffer pointers, mutex and conditions */
    shmem_ptr->buf1_head = 0;
    shmem_ptr->buf2_head = 0;
    shmem_ptr->buf1_tail = 0;
    shmem_ptr->buf2_tail = 0;
    shmem_ptr->buf1_cnt  = 0;
    shmem_ptr->buf2_cnt  = 0;
    //
    pthread_mutexattr_init( &attr_sh_lock );
    pthread_condattr_init( &attr_sh_spaceA );
    pthread_condattr_init( &attr_sh_itemA );
    //
    pthread_mutexattr_setpshared( &attr_sh_lock, PTHREAD_PROCESS_SHARED );
    pthread_condattr_setpshared( &attr_sh_spaceA, PTHREAD_PROCESS_SHARED );
    pthread_condattr_setpshared( &attr_sh_itemA, PTHREAD_PROCESS_SHARED );
    //
    pthread_mutex_init( &(shmem_ptr->lock), &attr_sh_lock );
    pthread_cond_init( &(shmem_ptr->spaceA), &attr_sh_spaceA );
    pthread_cond_init( &(shmem_ptr->itemA), &attr_sh_itemA );

    /* use fork to create child processes
     * - child 1: producer (black items)
     * - child 2: producer (green items)
     * - child 3: consumer
     */
    pid_t pid;
    if ( (pid = fork()) < 0 )
    {
        perror("Error forking");
        exit(1);
    }
    //
    if ( pid == 0 )
    {
        /* child 1 process */
        prod_generic( "prod_black" );
    }
    else
    {
        /* parent process */
        pid_t pid;
        if ( (pid = fork()) < 0 )
        {
            perror("Error forking");
            exit(1);
        }
        //
        if ( pid == 0 )
        {
            /* child 2 process */
            prod_generic( "prod_green" );
        }
        else
        {
            /* parent process */
            pid_t pid2;
            if ( (pid2 = fork()) < 0 )
            {
                perror("Error forking");
                exit(1);
            }
            //
            if ( pid2 == 0 )
            {
                /* child 3 process */
                consumer();
            }
            else
            {
                /* parent process - main thread */
                /* wait on producer and consumer threads to complete before exiting */
                wait( NULL );
                wait( NULL );
                wait( NULL );

                /* release shared memory segment */
                shmctl(shmem_id, IPC_RMID, NULL);

                printf( "End main() thread \n" );
                exit(0);
            }
        }
    }
    printf("\tpid %d exiting\n", getpid());
    return 0;
}

void prod_generic( char *tname )
{
    /*
     * use generic producer function to minimize code redundancy
     */
    char filename[100];
    int ii;
    FILE *fout;
    static int         shmem_id;   /* shared memory identifier */
    static SH_MEM_OBJ *shmem_ptr;  /* pointer to shared segment */

    printf( "\tBegin %s() thread, ", tname );
    printids();

    /*  Reuse key set in main to get the segment id of the
        segment that the parent process created. The size parameter is set
        to zero, and the flag to IPC_ALLOC, indicating that the segment is
        not being created here, it already exists
    */
    shmem_id = shmget(key, 0, 0);
    if (shmem_id == -1)
    {
        perror("child shmget failed");
        exit(1);
    }
    /* Now attach this segment into the child address space */
    shmem_ptr = shmat(shmem_id, (void *) NULL, flag);
    if (shmem_ptr == (void *) -1)
    {
        perror("child shmat failed");
        exit(2);
    }

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
        pthread_mutex_lock( &(shmem_ptr->lock) );    // enter critical section
            /* wait until space available in buffer */
            while ( (shmem_ptr->buf1_cnt + shmem_ptr->buf2_cnt) == 2*BUF_SIZE )
                while ( pthread_cond_wait(&(shmem_ptr->spaceA), &(shmem_ptr->lock)) != 0 );
                    /* sleep using condition variable */
            /*
             * by this point we know there is space in one of the buffers
             * create and add item to available buffer
             */
            if ( shmem_ptr->buf1_cnt < BUF_SIZE )
            {
                create_item( &shmem_ptr->buf1[shmem_ptr->buf1_tail][0], tname );
                print_item( fout, &(shmem_ptr->buf1[shmem_ptr->buf1_tail][0]) );
                shmem_ptr->buf1_tail = ( shmem_ptr->buf1_tail + 1 ) % BUF_SIZE;
                shmem_ptr->buf1_cnt++;
            }
            else
            {
                create_item( &shmem_ptr->buf2[shmem_ptr->buf2_tail][0], tname );
                print_item( fout, &(shmem_ptr->buf2[shmem_ptr->buf2_tail][0]) );
                shmem_ptr->buf2_tail = ( shmem_ptr->buf2_tail + 1 ) % BUF_SIZE;
                shmem_ptr->buf2_cnt++;
            }
        pthread_mutex_unlock( &(shmem_ptr->lock) );  // exit critical section
        pthread_cond_signal( &(shmem_ptr->itemA) );  // signal consumer that there is an item available
    }

    /* close the file */
    fclose( fout );

    /* done with the program, so detach the shared segment */
    shmdt((void *)shmem_ptr);

    printf( "\tEnd %s() thread \n", tname );
}

void consumer( void )
{
    int ii;
    FILE *fout;
    static int         shmem_id;   /* shared memory identifier */
    static SH_MEM_OBJ *shmem_ptr;  /* pointer to shared segment */

    printf( "\tBegin consumer() thread,   " );
    printids();

    /*  Reuse key set in main to get the segment id of the
        segment that the parent process created. The size parameter is set
        to zero, and the flag to IPC_ALLOC, indicating that the segment is
        not being created here, it already exists
    */
    shmem_id = shmget(key, 0, 0);
    if (shmem_id == -1)
    {
        perror("child shmget failed");
        exit(1);
    }
    /* Now attach this segment into the child address space */
    shmem_ptr = shmat(shmem_id, (void *) NULL, flag);
    if (shmem_ptr == (void *) -1)
    {
        perror("child shmat failed");
        exit(2);
    }

    fout = fopen( "output.txt", "w+" );

    for ( ii=0; ii<2*NUM_ITEMS; ii++ )
    {
        pthread_mutex_lock( &(shmem_ptr->lock) );    // enter critical section
            /* wait until item available in buffer */
            while ( shmem_ptr->buf1_cnt + shmem_ptr->buf2_cnt == 0 )
                while ( pthread_cond_wait(&(shmem_ptr->itemA), &(shmem_ptr->lock)) != 0 );
                    /* sleep using condition variable */

            /* by this point we know there is an item in one of the buffers */
            if ( shmem_ptr->buf1_cnt > 0 )
            {
                print_item( fout, &(shmem_ptr->buf1[shmem_ptr->buf1_head][0]) );
                shmem_ptr->buf1_head = (shmem_ptr->buf1_head + 1) % BUF_SIZE;
                shmem_ptr->buf1_cnt--;
            }
            else
            {
                print_item( fout, &(shmem_ptr->buf2[shmem_ptr->buf2_head][0]) );
                shmem_ptr->buf2_head = (shmem_ptr->buf2_head + 1) % BUF_SIZE;
                shmem_ptr->buf2_cnt--;
            }
        pthread_mutex_unlock( &(shmem_ptr->lock) );  // exit critical section
        pthread_cond_signal( &(shmem_ptr->spaceA) ); // signal producer that there is space available
    }

    /* close the file */
    fclose( fout );

    /* done with the program, so detach the shared segment */
    shmdt((void *)shmem_ptr);

    printf( "\tEnd consumer() thread \n" );
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
    printf( "pid= %u \n", (unsigned int)pid );
}
