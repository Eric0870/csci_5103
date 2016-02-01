/*============================================================================
 CSci5103 Spring 2016
 Assignment#		: 1
 Name			    : John Erickson
 Student id		    : 2336359
 x500 id			: eric0870
 CSELABS machine	: test at home (UNITE student)
 ============================================================================*/

/**********************************************************************************************
 * Requirements
 *  - Create shared memory bank to facilitate communication between two processes
 *    -- 1024 byte char array for data transfer
 *    -- time tag indicating time of insertion
 *  - Create a producer process
 *    -- read input file
 *    -- transfer contents of file (up to 1024 bytes) to shared memory
 *    -- add time tag to shared memory
 *  - Create a consumer process
 *    -- read contents of shared memory
 *    -- write contents to output file
 *  - Compute metrics
 *    -- measure and report total time taken for data transfer
 *
 * Considerations
 *  - Must use POSIX real-time signals for process communication and synchronization
 *
 * Code reuse
 *  - Shared memory management
 *    -- CSCI 5103 > Examples > Dynamic Memory > parent.c
 *    -- CSCI 5103 > Examples > Dynamic Memory > child.c
 *  - File IO
 *    -- CSCI 5103 > Examples > File I/O Examples > filecopy.c
 *  - Real-time signal management
 *    -- CSCI 5103 > Examples > Signal Management > realtime.c
 *
 * Optimizations
 *  - Producer reads file into into local buffer if shared memory not consumed
 *  - Consumer reads shared memory into local buffer then sends signal to indicate consumed
**********************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define DEBUG
#define BUFSIZE 1024

/* Prototypes */
static void  consumer( void );
static void  producer( pid_t ch_pid, int argc, char * argv[] );
static void  sig_hdlr_usr(int signal, siginfo_t *info, void *arg __attribute__ ((__unused__)));
static void  send_rt_signal(pid_t pid, int signo, int value);
static float diff_time( struct timespec *t0, struct timespec *tf );

/* Static vars */
/* shared memory includes array of 1024 chars + time tag */
typedef struct
{
    char    buf[BUFSIZE];
    struct timespec ts;
} SH_MEM_OBJ;

static int         shmem_id;   /* shared memory identifier */
static SH_MEM_OBJ *shmem_ptr;  /* pointer to shared segment */
static key_t       key;        /* key to access shared memory segments */
static int         flag;       /* r/w permissions */

static int fdout;              /* output file descriptor */

int main(int argc, char * argv[])
{
    /*******************************************
     * Create shared memory bank
     *******************************************/
    int size;       /* memory size in bytes */

    key  = 4455;
    flag = 1023;    /* 1023 = 111111111b (all permissions and modes set) */
    size = sizeof( SH_MEM_OBJ );

    /* First, create a shared memory segment */
    shmem_id = shmget (key, size, flag);
    if (shmem_id == -1)
    {
        perror("shmget failed");
        exit(1);
    }
    #ifdef DEBUG
        printf("Main:     Got shmem id = %d\n", shmem_id);
    #endif

    /* Now attach the new segment into my address space.
        This will give me a (void *) pointer to the shared memory area.
        The NULL pointer indicates that we don't care where in the address
        space the new segment is attached. The return value gives us that
        location anyway.
    */
    shmem_ptr = shmat (shmem_id, (void *) NULL, flag);
    if (shmem_ptr == (void *) -1)
    {
        perror("shmat failed");
        exit(2);
    }
    #ifdef DEBUG
        printf("Main:     Got ptr = %p\n", shmem_ptr);
    #endif


    /* Setup handler for user signals SIGUSR1 and SIGUSR2
     * - block signals now to avoid race condition
     */
    sigset_t mask;
    struct sigaction action;

    // configure same handler to service both producer and consumer processes
    action.sa_sigaction = sig_hdlr_usr;
    action.sa_flags     = SA_SIGINFO;
    //
    sigemptyset(&action.sa_mask);
    if ((sigaction(SIGUSR1, &action, NULL) < 0) ||
        (sigaction(SIGUSR2, &action, NULL) < 0))
    {
        fprintf(stderr, "sigaction error: %s\n", strerror(errno));
        return 1;
    }
    //
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, NULL);


    /* write start time to shared memory */
    clock_gettime( CLOCK_MONOTONIC, &(shmem_ptr->ts) );

    /* use fork to create child process */
    pid_t pid;
    if ( (pid = fork()) < 0 )
    {
        perror("Error forking");
        exit(1);
    }

    if ( pid == 0 )
    {
        consumer();
    }
    else
    {
        producer(pid, argc, argv);
    }

    printf("  Process ID: %ld exiting\n", (long)getpid());
    return EXIT_SUCCESS;
}


void producer( pid_t ch_pid, int argc, char * argv[] )
{
    /*******************************************
     * Define producer (parent) process
     *******************************************/

    //signal( SIGUSR1, SIG_IGN );
    struct sigaction action;
    action.sa_handler = SIG_IGN;
    sigaction(SIGUSR1, &action, NULL);

    /* Read input file */
    char    buffer[BUFSIZE];
    ssize_t count;
    int     fdin;

    /* validate arguments from program call */
    if ( argc !=2 )
    {
        printf( "Usage: %s  <input_filename>\n", argv[0] );
        exit(1);
    }

    /* validate input file */
    if ( (fdin = open(argv[1], O_RDONLY)) == -1 )
    {
        perror("Error opening the input file");
        exit(2);
    }

    /* enter while loop to process data from input file */
    sigset_t mask;
    sigemptyset ( &mask );
    while ( (count=read(fdin, buffer, BUFSIZE)) > 0 )
    {
        /* Copy data from local buffer into shared memory */
        sprintf(shmem_ptr->buf, "%s", buffer);

        printf("PRODUCER: wrote %zu bytes to shared mem\n", count);

        /* Send signal to consumer */
        printf("PRODUCER: signal Consumer that shared memory ready to read\n");
        send_rt_signal( ch_pid, SIGUSR1, (int)count );

        /* Suspend until signal received from consumer */
        printf("PRODUCER: suspend until signal received from Consumer...\n");
        sigsuspend( &mask );  /* No signals are masked while waiting */
    }

    if ( count == -1 ) {
       perror ("Error reading input file");
       exit(4);
    }

    close(fdin);

    /* Send signal to consumer */
    printf("PRODUCER: signal Consumer that Producer has reached EOF\n");
    send_rt_signal( ch_pid, SIGUSR1, -1 );

    /* Suspend until signal received from consumer */
    printf("PRODUCER: suspend until signal received from Consumer...\n");
    sigsuspend( &mask );  /* No signals are masked while waiting */
}


void consumer( void )
{
    /*******************************************
     * Define consumer (child) process
     *******************************************/

    struct sigaction action;
    action.sa_handler = SIG_IGN;
    sigaction(SIGUSR2, &action, NULL);

    /*  Reuse key set in main to get the segment id of the
        segment that the parent process created. The size parameter is set
        to zero, and the flag to IPC_ALLOC, indicating that the segment is
        not being created here, it already exists
    */
    shmem_id = shmget (key, 0, 0);
    if (shmem_id == -1)
    {
        perror("child shmget failed");
        exit(1);
    }
    #ifdef DEBUG
        printf("Consumer: Got shmem id = %d\n", shmem_id);
    #endif

    /* Now attach this segment into the child address space */
        shmem_ptr = shmat (shmem_id, (void *) NULL, flag);
    if (shmem_ptr == (void *) -1)
    {
        perror("child shmat failed");
        exit(2);
    }
    #ifdef DEBUG
        printf("Consumer: Got ptr = %p\n", shmem_ptr);
    #endif


    /* open output file */
    mode_t  perms;
    perms = 0740;
    if ( (fdout = open ("output", (O_WRONLY | O_CREAT), perms)) == -1 )
    {
        perror("Error in creating output file");
        exit(3);
    }


    /* Suspend until signal received from producer */
    sigset_t mask;
    sigemptyset ( &mask );
    for(;;)
    {
        printf("CONSUMER: suspend until signal received from Producer...\n");
        sigsuspend( &mask );  /* No signals are masked while waiting */
    }
}


/* signal handler: prints the signal number and the accompanying integer */
void sig_hdlr_usr(int signal, siginfo_t *info, void *arg __attribute__ ((__unused__)))
{
    int val = info->si_value.sival_int;
    printf("sig_hdlr_usr: received signal %d with value %d\n", signal, val);

    if ( signal == SIGUSR1 )
    {
        if ( val > 0 )
        {
            /* write fresh data from shared memory to output file */
            if ( write(fdout, shmem_ptr->buf, val) != val )
            {
                perror("Error writing" );
                exit(3);
            }
            printf("CONSUMER: wrote %d bytes to output file \n", val);

            /* Send signal to parent */
            printf("CONSUMER: signal Producer that shared memory has been consumed\n");
            send_rt_signal( getppid(), SIGUSR2, 1 );
        }
        else
        {
            /* close the file */
            close(fdout);

            /* compute metrics from data transfer */
            struct timespec ts_now;
            float           d_xfr_t;

            clock_gettime( CLOCK_MONOTONIC, &ts_now );
            d_xfr_t = diff_time( &(shmem_ptr->ts), &ts_now );
            printf("CONSUMER: time spent on data transfer: %f ms\n", d_xfr_t);

            /* done with the program, so detach the shared segment and terminate */
            shmdt( (void *)shmem_ptr);

            /* send signal to producer to indicate transfer complete */
            printf("CONSUMER: signal Producer that data transfer is complete\n");
            send_rt_signal( getppid(), SIGUSR2, 0 );

            /* terminate consumer process */
            printf("  Process ID: %ld exiting\n", (long)getpid());
            kill(getpid(), SIGINT);
        }
    }
    else if ( signal == SIGUSR2 )
    {
        if ( val > 0 )
        {
            /* Consumer indicating that shared memory has been consumed */
            // fall back to producer process
        }
        else
        {
            /* Consumer indicating that data transfer is complete */

            /* done with the program, so detach the shared segment and terminate */
            shmdt( (void *)shmem_ptr);

            /* The following is one way of destroying the shared memory segment
            and returning it to the system. I can do this safely here, because
            I know the parent program won't be using the shared memory any more.
            In general, you can only do this safely when ALL processes that used
            the memory are known to have detached the segment using shmdt(). Look
            up the shmctl man page for details.
            */
            shmctl (shmem_id, IPC_RMID, NULL);

            /* terminate producer process */
            printf("  Process ID: %ld exiting\n", (long)getpid());
            kill(getpid(), SIGINT);
        }
    }

    return;
}


/* send 'value' along with a signal numbered 'signo' */
void send_rt_signal(pid_t pid, int signo, int value)
{
    union sigval sivalue;
    sivalue.sival_int = value;

    /* queue the signal */
    if (sigqueue(pid, signo, sivalue) < 0) {
        fprintf(stderr, "sigqueue failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return;
}


float diff_time( struct timespec *t0, struct timespec *tf )
{
    long long dif_sec;
    long long dif_nsec;

    /* calculate difference of second and nanosecond components */
    dif_sec  = tf->tv_sec  - t0->tv_sec;
    dif_nsec = tf->tv_nsec - t0->tv_nsec;

    /* return time difference in milliseconds */
    return (dif_sec*1.0e3 + dif_nsec*1.0e-6);
}

