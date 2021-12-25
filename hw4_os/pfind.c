#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define SUCCESS (0)
#define FAILURE (1)

#define True (1)
#define False (0)

/* Flag indicating threads to start/stop work */
int g_is_search = False;
/* Number of seatching threads - command line argument */
int g_threads_num;
/* search term from command line argument */
const char *g_search_term;
/* flag used to inital sync threads after broadcast from main thread*/
int g_all_threads_ready = False;
/* Counter for matched files */
_Atomic int g_files_found = 0;
/* Counter for failed threads */
int g_failed_threads = 0;

/* Synch objects */
pthread_mutex_t search_mutex;
pthread_mutex_t finish_mutex;
pthread_mutex_t queue_mutex;
pthread_cond_t search_cv;
pthread_cond_t finish_cv;
/* Array of condition variables  - one for each thread 
allows for signaling specific thread and implemnting fifo order */
pthread_cond_t *queue_cv;

/* Entry in directories queue */
typedef struct dir_queue_entry_t
{
    char path_name[PATH_MAX];
    char dir_name[PATH_MAX];
    struct dir_queue_entry_t *next;
    struct dir_queue_entry_t *prev;
} dir_queue_entry_t;

/* Directories queue */
typedef struct
{
    dir_queue_entry_t *head;
    dir_queue_entry_t *tail;
    unsigned int count;
} dir_queue_t;

/* Thread queue entry */
typedef struct thread_queue_entry_t
{
    long thread_id;
    struct thread_queue_entry_t *next;
    struct thread_queue_entry_t *prev;
    /* associeted directory to handle. if NULL no directory is asssoicated with this thread */
    struct dir_queue_entry_t *dir_to_handle;
} thread_queue_entry_t;

typedef struct
{
    thread_queue_entry_t *head;
    thread_queue_entry_t *tail;
    unsigned int count;
} thread_queue_t;

/* Gloabal dirs queue */
dir_queue_t g_dir_queue;
/* Global threads queue */
thread_queue_t g_thread_queue;

/* exit point for searching threads in error */
void thread_exit_on_error()
{
    fprintf(stderr, "%s \n", strerror(errno));
    pthread_mutex_lock(&finish_mutex);
    g_failed_threads++;
    /* This thread failed may cause the finish program flow in main thread */
    pthread_cond_signal(&finish_cv);
    pthread_mutex_unlock(&finish_mutex);
    pthread_exit((void *)FAILURE);
}

int is_dir_queue_empty()
{
    return g_dir_queue.count == 0;
}

int is_thread_queue_empty()
{
    return g_thread_queue.count == 0;
}

dir_queue_entry_t *dir_dequeue()
{
    /* Assumes the queue isn't empty */
    dir_queue_entry_t *removed_entry = g_dir_queue.tail;
    if (g_dir_queue.count == 1)
    {
        /* The queue will become empty */
        g_dir_queue.head = NULL;
        g_dir_queue.tail = NULL;
    }
    else
    {
        g_dir_queue.tail = g_dir_queue.tail->prev;
        g_dir_queue.tail->next = NULL;
    }
    g_dir_queue.count--;
    return removed_entry;
}

thread_queue_entry_t *thread_dequeue()
{
    /* Assumes the queue isn't empty */
    thread_queue_entry_t *removed_entry = g_thread_queue.tail;
    if (g_thread_queue.count == 1)
    {
        /* The queue will become empty */
        g_thread_queue.head = NULL;
        g_thread_queue.tail = NULL;
    }
    else
    {
        g_thread_queue.tail = g_thread_queue.tail->prev;
        g_thread_queue.tail->next = NULL;
    }
    g_thread_queue.count--;
    return removed_entry;
}

dir_queue_entry_t *dir_enqueue(const char *dir_name, const char *path_name)
{
    dir_queue_entry_t *new_entry = (dir_queue_entry_t *)malloc(sizeof(dir_queue_entry_t));
    if (new_entry == NULL)
    {
        thread_exit_on_error();
    }
    /* new_entry is inserted as the new head */
    strcpy(new_entry->dir_name, dir_name);
    strcpy(new_entry->path_name, path_name);
    /* if the queue is empty next will be NULL (single element in queue) */
    new_entry->next = g_dir_queue.head;
    new_entry->prev = NULL;
    if (g_dir_queue.count == 0)
    {
        g_dir_queue.tail = new_entry;
    }
    else
    {
        g_dir_queue.head->prev = new_entry;
    }
    g_dir_queue.head = new_entry;
    g_dir_queue.count++;
    return g_dir_queue.head;
}

thread_queue_entry_t *thread_enqueue(thread_queue_entry_t *new_entry)
{
    if (new_entry == NULL)
    {
        thread_exit_on_error();
    }
    /* new_entry is inserted as the new head */
    /* if the queue is empty next will be NULL (single element in queue) */
    new_entry->next = g_thread_queue.head;
    new_entry->dir_to_handle = NULL;
    new_entry->prev = NULL;
    if (g_thread_queue.count == 0)
    {
        g_thread_queue.tail = new_entry;
    }
    else
    {
        g_thread_queue.head->prev = new_entry;
    }
    g_thread_queue.head = new_entry;
    g_thread_queue.count++;
    return g_thread_queue.head;
}

void dir_queue_init()
{
    g_dir_queue.count = 0;
    g_dir_queue.head = NULL;
    g_dir_queue.tail = NULL;
}

void thread_queue_init()
{
    g_thread_queue.count = 0;
    g_thread_queue.head = NULL;
    g_thread_queue.tail = NULL;
}

int is_searchable(const char *dir_path)
{
    struct stat root_stat;
    if (stat(dir_path, &root_stat) == -1)
    {
        fprintf(stderr, "from is_searchable %s \n", strerror(errno));
        return 0;
    }

    /* A directory can be searched if the process has both read and execute permissions for it */
    return ((root_stat.st_mode & S_IRUSR) && (root_stat.st_mode & S_IXUSR));
}

int is_dot_folder(const char *dir_name)
{
    return (strcmp(dir_name, ".") == 0) || (strcmp(dir_name, "..") == 0);
}

void get_stat(const char *pathname, struct stat *entry_stat)
{
    if (stat(pathname, entry_stat) == -1)
    {
        thread_exit_on_error();
    }
}

int is_thread_in_queue(long t_id)
{
    thread_queue_entry_t *tmp = g_thread_queue.head;
    while (tmp)
    {
        if (tmp->thread_id == t_id)
        {
            return True;
        }
        tmp = tmp->next;
    }
    return False;
}

int should_thread_sleep(long t_id, thread_queue_entry_t *thread)
{
    if (g_is_search == True)
    {
        if (!g_all_threads_ready)
        {
            /* Not all threads are waiting on their cv's yet this is used
            to sync the thread after the inital broadcast from main */
            return True;
        }
        if (is_dir_queue_empty() && thread->dir_to_handle == NULL)
        {
            /* nominal case : thread should sleep only if the dir queue is logicaly empty 
            (the actual dir queue can be empty while thread was associated with a directory to work on)*/
            return True;
        }
        else
        {
            return False;
        }
    }
    else
    {
        /* No search should be done so no need to sleep 
        this will happen in the program finish flow */
        return False;
    }
}

void wait_for_start(long t_id)
{
    pthread_mutex_lock(&search_mutex);
    while (!g_is_search)
    {
        /* Wait for main thread to singal search start*/
        pthread_cond_wait(&search_cv, &search_mutex);
    }
    pthread_mutex_unlock(&search_mutex);
}

void iterate_in_directory(dir_queue_entry_t *removed_entry, DIR *curr_dir, long t_id)
{
    struct stat entry_stat;
    struct dirent *curr_entry;
    while ((curr_entry = readdir(curr_dir)) != NULL)
    {
        /* construct the relative path for current entry in cur_dirr */
        char new_pathname[PATH_MAX];
        strcpy(new_pathname, removed_entry->path_name);
        strcat(new_pathname, "/");
        strcat(new_pathname, curr_entry->d_name);
        /* if the entry is '.' ot '..' we ignore it */
        if (!is_dot_folder(curr_entry->d_name))
        {
            get_stat(new_pathname, &entry_stat);

            if (S_ISDIR(entry_stat.st_mode))
            {
                /* this is a directory */
                if ((entry_stat.st_mode & S_IRUSR) && (entry_stat.st_mode & S_IXUSR))
                {
                    /* This directory can be serched - the process has both read and execute permissions for it.*/
                    /* Get the lock for shared queues */
                    pthread_mutex_lock(&queue_mutex);
                    /* if the thread queue is not empty we can assign this directory entry to that thread
                        In this case we don't insert the directory to dir_queue since it is schedueled to work by 
                        the dequeued thread. 
                        Create a directory entry object and assign it to that thread
                        finaly siganl that thread */
                    if (!is_thread_queue_empty())
                    {
                        dir_queue_entry_t *new_dir = (dir_queue_entry_t *)malloc(sizeof(dir_queue_entry_t));
                        strcpy(new_dir->dir_name, curr_entry->d_name);
                        strcpy(new_dir->path_name, new_pathname);
                        thread_queue_entry_t *thread = thread_dequeue();
                        thread->dir_to_handle = new_dir;
                        pthread_cond_signal(&queue_cv[thread->thread_id]);
                    }
                    else
                    {
                        /* No available threads. Add this dir to the queue
                            when a thread finish his job it'll handle this directory */
                        dir_enqueue(curr_entry->d_name, new_pathname);
                    }
                    pthread_mutex_unlock(&queue_mutex);
                }
                else
                {
                    /* This directory can't be searched */
                    printf("Directory %s: Permission denied.\n", new_pathname);
                }
            }
            else
            {
                /* This is not a directory */
                if (strstr(curr_entry->d_name, g_search_term) != NULL)
                {
                    /* The file name contains the seatch term */
                    printf("%s\n", new_pathname);
                    g_files_found++;
                }
            }
        }
    }
}

/* Check we can actuall open this directory */
void open_dir_and_check(DIR **curr_dir, char *path_name)
{
    if ((*curr_dir = opendir(path_name)) == NULL)
    {
        thread_exit_on_error();
    }
}

/* Searching thread entry point*/
void *searching_entry(void *thread_id)
{
    long t_id = (long)thread_id;

    /* Wait for a signal from main thread */
    wait_for_start(t_id);

    DIR *curr_dir;
    dir_queue_entry_t *removed_dir_entry;
    /* Each thread has a single object represting him can be in 2 states :
    1. idle = waiting in the threads queue
    2. working = not in the queue and has an assoiceted directory to work on */
    thread_queue_entry_t *thread_entry = (thread_queue_entry_t *)malloc(sizeof(thread_queue_entry_t));
    if (thread_entry == NULL)
    {
        thread_exit_on_error();
    }
    thread_entry->thread_id = t_id;
    while (True)
    {
        pthread_mutex_lock(&queue_mutex);
        while (should_thread_sleep(t_id, thread_entry))
        {

            pthread_mutex_lock(&finish_mutex);
            /* Thread is going to become idle => go to threads queue */
            if (!is_thread_in_queue(t_id))
            {
                thread_enqueue(thread_entry);
            }

            /* going to sleep so signal to main thread 
            (This will potentialy cause the program to finish - main thread will check)
            the pthread_cond_wait is inside this critical section (relative to queue mutex)
            so when main will signal this thread to stop this thread is realy waiting */
            pthread_cond_signal(&finish_cv);
            pthread_mutex_unlock(&finish_mutex);

            if (!g_all_threads_ready && g_thread_queue.count == g_threads_num)
            {
                /* This section occurs only once 
                The last thread to arrive here from the broadcast signal from the main thread 
                1. set the g_all_threads_ready flag to True
                2. if there is more than one thread in the program dequeue a thread and signal it to start 
                3. if a single thread : continue (all the flags are up and root directory is waiting in the dir queue) */
                g_all_threads_ready = True;
                thread_queue_entry_t *thread = thread_dequeue();
                if (g_threads_num != 1)
                {
                    pthread_cond_signal(&queue_cv[thread->thread_id]);
                    pthread_cond_wait(&queue_cv[t_id], &queue_mutex);
                }
                continue;
            }
            /* becoming idle can wake when 
            1. another searching thread found a directory for this thread
            2. main thread signal this thread to finish */
            pthread_cond_wait(&queue_cv[t_id], &queue_mutex);
        }
        if (!g_is_search)
        {
            /* global set to false - Everyone finished so exit.
            release the lock so other threads could exit */
            pthread_mutex_unlock(&queue_mutex);
            pthread_exit((void *)SUCCESS);
        }
        if (thread_entry->dir_to_handle != NULL)
        {
            /* This thread is associated with a directory to search in */
            removed_dir_entry = thread_entry->dir_to_handle;
        }
        else
        {
            /* Don't have an assoicated directory to search in so get one
            from directories queue (which can't be empty here) */
            removed_dir_entry = dir_dequeue();
        }
        pthread_mutex_unlock(&queue_mutex);

        /* This call can't fail becouse of premission reasons since removed_entry was checked
        to have read and execute premissions before added to the queue 
        we check for error that could happen becouse of other reasons (not premissions) */
        open_dir_and_check(&curr_dir, removed_dir_entry->path_name);

        iterate_in_directory(removed_dir_entry, curr_dir, t_id);
        /* Finished search job */
        thread_entry->dir_to_handle = NULL;
        free(removed_dir_entry);
        closedir(curr_dir);
    }
    return (void *)FAILURE;
}

pthread_t *main_init()
{
    pthread_t *threads_arr = (pthread_t *)malloc(g_threads_num * sizeof(pthread_t));
    if (threads_arr == NULL)
    {
        fprintf(stderr, "Error in malloc for threads array \n");
        exit(FAILURE);
    }

    /* Init locks and condition variables */
    pthread_mutex_init(&search_mutex, NULL);
    pthread_mutex_init(&finish_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&search_cv, NULL);
    pthread_cond_init(&finish_cv, NULL);

    /* Init codition variables array */
    queue_cv = (pthread_cond_t *)malloc(g_threads_num * sizeof(pthread_cond_t));
    if (queue_cv == NULL)
    {
        fprintf(stderr, "Error in malloc for condition variables array \n");
        exit(FAILURE);
    }
    /* Init threads queue. each thread will enqueue itself */
    thread_queue_init();
    /* Init threads and their corresponding condition variables */
    for (long t_id = 0; t_id < g_threads_num; t_id++)
    {
        pthread_create(&threads_arr[t_id], NULL, searching_entry, (void *)t_id);
        pthread_cond_init(&queue_cv[t_id], NULL);
    }

    return threads_arr;
}

int main(int argc, char const *argv[])
{
    /* Validating command line argsuments */
    if (argc != 4)
    {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    DIR *root_dir = opendir(argv[1]);
    if (root_dir == NULL)
    {
        /* Failed openning the root directory */
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    if (!is_searchable(argv[1]))
    {
        /* root can't be searched */
        fprintf(stderr, "Root directory can't be searched \n");
        return FAILURE;
    }

    /* Number of threades is assumed to be a non negative int */
    g_threads_num = atoi(argv[3]);
    g_search_term = argv[2];
    /* End of arguments validation */

    /* Init directories queue and insert root directory for search (from command line) */
    dir_queue_init();
    dir_enqueue(argv[1], argv[1]);

    /* Create an array of threads to be used by main thread to monitor them */
    pthread_t *threads_arr = main_init();

    /* signal all threads that search can start by raising the flag */
    pthread_mutex_lock(&search_mutex);
    g_is_search = True;
    pthread_cond_broadcast(&search_cv);
    pthread_mutex_unlock(&search_mutex);
    /*===========================*/
    pthread_mutex_lock(&finish_mutex);
    /* The only case where the main thread should should enter the program exit flow is when
        1. the dirictories queue is empty AND no thread is working  
        OR
        2. all threads failed */
    while (!(is_dir_queue_empty() && (g_thread_queue.count + g_failed_threads) == g_threads_num) && (g_failed_threads != g_threads_num))
    {
        pthread_cond_wait(&finish_cv, &finish_mutex);
    }
    g_is_search = False;

    /* if we get here 2 cases :
    1.  the dir queue was empty and no threads were working so 
        all threads are waiting - we changed g_is_search to False so they'll wake and exit 
        locking the queue mutex insures that in this point the searching thread are waiting
    2.  all searching threads failed (the signal won't have an effect) */
    pthread_mutex_lock(&queue_mutex);
    for (long t_id = 0; t_id < g_threads_num; t_id++)
    {
        pthread_cond_signal(&queue_cv[t_id]);
    }
    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_unlock(&finish_mutex);

    /* All threads were signaled to exit - tWait for all threads to complete */
    for (int i = 0; i < g_threads_num; i++)
    {
        pthread_join(threads_arr[i], NULL);
    }

    printf("Done searching, found %d files\n", g_files_found);

    pthread_mutex_destroy(&search_mutex);
    pthread_mutex_destroy(&finish_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&search_cv);
    pthread_cond_destroy(&finish_cv);

    for (int i = 0; i < g_threads_num; i++)
    {
        pthread_cond_destroy(&queue_cv[i]);
    }

    /* if some thread failed return code is 1*/
    if (g_failed_threads != 0)
    {
        return FAILURE;
    }
    return SUCCESS;
}
