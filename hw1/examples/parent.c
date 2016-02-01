#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEBUG

/* Simple example of shared memory usage */
/* Make sure your current working directory is the one these files are in */
/* Then execute `parent' from this directory */

main()
{
    int shmem_id;       /* shared memory identifier */
    int *shmem_ptr;     /* pointer to shared segment */

    key_t key;          /* A key to access shared memory segments */
    int size;           /* Memory size needed, in bytes */
    int flag;           /* Controls things like r/w permissions */

    key = 4455;         /* Some arbitrary integer, which will also be
                            passed to the other processes which need to
                            share memory */
    size = 2048;        /* Assume we need 2Kb of memory, which means we
                            can store 512 integers or floats */
    flag = 1023;        /* 1023 = 111111111 in binary, i.e. all permissions
                            and modes are set. This may not be appropriate
                            for all programs! */

    /* First, create a shared memory segment */
    shmem_id = shmget (key, size, flag);
    if (shmem_id == -1)
    {
        perror ("shmget failed");
        exit (1);
    }
    #ifdef DEBUG
    printf ("Got shmem id = %d\n", shmem_id);
    #endif

    /* Now attach the new segment into my address space.
        This will give me a (void *) pointer to the shared memory area.
        The NULL pointer indicates that we don't care where in the address
        space the new segment is attached. The return value gives us that
        location anyway.
    */

    shmem_ptr = shmat (shmem_id, (void *) NULL, 1023);
    if (shmem_ptr == (void *) -1)
    {
        perror ("shmat failed");
        exit (2);
    }
    #ifdef DEBUG
    printf ("Got ptr = %p\n", shmem_ptr);
    #endif

    /* Fork a child process to test the shared memory */

    if (fork()) /* this is the parent process */
    {
        /* Store some arbitrary integer value into some location within
            the shared memory area. We will read this same location 50
            from the child process to see that it contains the same value
        */

        shmem_ptr[50] = 676;

        printf ("parent stored 676\n");

        /* Here, my shmem_ptr was an (int *) pointer. It could just as
            easily have been a (struct whatever *). So you can for example
            define a struct of your own, and store its contents in the
            shared memory segment using a struct whatever *shmem_ptr;
        */

        /* done with the program, so detach the shared segment and terminate */
        shmdt ( (void *)  shmem_ptr);
    }
    else    /* this is the child process */
    {
        char keystr[10];

        /* Execute the child program in this process, passing it the key to
            our shared memory segment as a command-line parameter. See child.c
            for the code that the child process executes.
        */

        sprintf (keystr, "%d", key);
        execl ("./child", "child", keystr, NULL);
    }
}
