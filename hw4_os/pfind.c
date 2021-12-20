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

#define SUCCESS         (0)
#define FAILURE         (1)

#define True            (1)
#define False           (0)

/* Flag indicating threads to work */
int g_is_search = False;
/* Number of threads currently working */
atomic_int g_threads_working = 0;
/* Number of thread to be used to search - argument */
int g_threads_num;
/* search term from command line argument */
const char* g_search_term;

int g_all_threads_ready = False;

_Atomic int g_files_found = 0;

int g_failed_threads = 0;

pthread_mutex_t search_mutex;
pthread_mutex_t finish_mutex;
pthread_mutex_t queue_mutex;
pthread_cond_t search_cv;
pthread_cond_t finish_cv;
/* Array of condition variables  - one for each thread 
allows for signaling specific thread and implemnting fifo order */
pthread_cond_t* queue_cv;

typedef struct thread_queue_entry_t {
    long thread_id;
    struct thread_queue_entry_t* next;
    struct thread_queue_entry_t* prev;
} thread_queue_entry_t;

typedef struct {
    thread_queue_entry_t* head;
    thread_queue_entry_t* tail;
    unsigned int count;
} thread_queue_t;

typedef struct dir_queue_entry_t {
    char path_name[PATH_MAX];
    char dir_name[PATH_MAX];
    struct dir_queue_entry_t* next;
    struct dir_queue_entry_t* prev;
} dir_queue_entry_t;

typedef struct {
    dir_queue_entry_t* head;
    dir_queue_entry_t* tail;
    unsigned int count;
} dir_queue_t;

/* Gloabal dirs queue */
dir_queue_t g_dir_queue;
thread_queue_t g_thread_queue;

int is_dir_queue_empty() {
    return g_dir_queue.count == 0;
}

int is_thread_queue_empty() {
    return g_thread_queue.count == 0;
}

int is_thread_next_for_work(long t_id) {
    return g_thread_queue.tail->thread_id == t_id;
}

long next_thread_to_signal() {
    return g_thread_queue.tail->thread_id;
}

dir_queue_entry_t* dir_dequeue() {
    /* Assumes the queue isn't empty */
    dir_queue_entry_t* removed_entry = g_dir_queue.tail;
    if (g_dir_queue.count == 1) {
        /* The queue will become empty */
        g_dir_queue.head = NULL;
        g_dir_queue.tail = NULL;
    } else {
        g_dir_queue.tail = g_dir_queue.tail->prev;
        g_dir_queue.tail->next = NULL;
    }
    g_dir_queue.count--;
    return removed_entry;
}

thread_queue_entry_t* thread_dequeue() {
    /* Assumes the queue isn't empty */
    thread_queue_entry_t* removed_entry = g_thread_queue.tail;
    if (g_thread_queue.count == 1) {
        /* The queue will become empty */
        g_thread_queue.head = NULL;
        g_thread_queue.tail = NULL;
    } else {
        g_thread_queue.tail = g_thread_queue.tail->prev;
        g_thread_queue.tail->next = NULL;
    }
    g_thread_queue.count--;
    return removed_entry;
}

void dir_enqueue(const char* dir_name, const char* path_name) {
    dir_queue_entry_t* new_entry = (dir_queue_entry_t*) malloc(sizeof(dir_queue_entry_t));
    if (new_entry == NULL) {
        // TODO WHAT TO DO HERE ???
    }
    /* new_entry is inserted as the new head */
    strcpy(new_entry->dir_name, dir_name);
    strcpy(new_entry->path_name, path_name);
    /* if the queue is empty next will be NULL (single element in queue) */
    new_entry->next = g_dir_queue.head;
    new_entry->prev = NULL;
    if (g_dir_queue.count == 0) {
        g_dir_queue.tail = new_entry;
    } else {
        g_dir_queue.head->prev = new_entry;
    }
    g_dir_queue.head = new_entry;
    g_dir_queue.count++;
}

void thread_enqueue(long t_id) {
    thread_queue_entry_t* new_entry = (thread_queue_entry_t*) malloc(sizeof(thread_queue_entry_t));
    if (new_entry == NULL) {
        // TODO WHAT TO DO HERE ???
    }
    /* new_entry is inserted as the new head */
    /* if the queue is empty next will be NULL (single element in queue) */
    new_entry->next = g_thread_queue.head;
    new_entry->thread_id = t_id;
    new_entry->prev = NULL;
    if (g_thread_queue.count == 0) {
        g_thread_queue.tail = new_entry;
    } else {
        g_thread_queue.head->prev = new_entry;
    }
    g_thread_queue.head = new_entry;
    g_thread_queue.count++;
}

void dir_queue_init() {
    g_dir_queue.count = 0;
    g_dir_queue.head = NULL;
    g_dir_queue.tail = NULL;
}

void thread_queue_init() {
    g_thread_queue.count = 0;
    g_thread_queue.head = NULL;
    g_thread_queue.tail = NULL;
}

int is_searchable(const char* dir_path) {
    struct stat root_stat;
    if (stat(dir_path, &root_stat) == -1) {
        fprintf(stderr, "from is_searchable %s \n", strerror(errno));
        return 0;
    }

    /* A directory can be searched if the process has both read and execute permissions for it */
    return ((root_stat.st_mode & S_IRUSR) && (root_stat.st_mode & S_IXUSR));
}

int is_dot_folder(const char* dir_name) {
    return (strcmp(dir_name, ".") == 0) || (strcmp(dir_name, "..") == 0);
}

void thread_exit_on_error() {
    fprintf(stderr, "%s \n", strerror(errno));
    pthread_mutex_lock(&finish_mutex);
    g_failed_threads++;
    g_threads_working--;
    pthread_cond_signal(&finish_cv);
    pthread_mutex_lock(&queue_mutex);
    if (!is_dir_queue_empty() && !is_thread_queue_empty()) {
        pthread_cond_signal(&queue_cv[next_thread_to_signal()]);
    }
    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_unlock(&finish_mutex);
    pthread_exit((void*) FAILURE);
}

void get_stat(const char* pathname, struct stat* entry_stat) {
    if (stat(pathname, entry_stat) == -1) {
        thread_exit_on_error();
    }
}

int is_thread_in_queue(long t_id) {
    thread_queue_entry_t* tmp = g_thread_queue.head;
    while (tmp) {
        if (tmp->thread_id == t_id) {
            return True;
        }
        tmp = tmp->next;
    }
    return False;
}

void print_thread_queue() {
    printf("thread queue \n");
    thread_queue_entry_t* tmp = g_thread_queue.head;
    while (tmp) {
        printf("%ld -> ", tmp->thread_id);
        tmp = tmp->next;
    }
    printf("\n");
}

int should_thread_sleep(long t_id) {
    if (g_is_search == True) {
        if (!g_all_threads_ready) {
            printf("%ld - not all are ready \n", t_id);
            return True;
        }
        if (is_dir_queue_empty()) {
            printf("%ld - dir queue empty \n", t_id);
            return True;
        } else {
            if (is_thread_in_queue(t_id) && g_threads_num > 1) {
                printf("%ld - in queue \n", t_id);
                return True;
            }
            printf("%ld shouldn't sleep \n", t_id);
            return False;
        }
    } else {
        /* No search should be done so no need to sleep */
        return False;
    }
}

void wait_for_start(long t_id) {
    pthread_mutex_lock(&search_mutex);
    while (!g_is_search) {
        pthread_cond_wait(&search_cv, &search_mutex);
        
    }
    printf("thread %ld finished got broadcast\n", t_id);
    pthread_mutex_unlock(&search_mutex);
}

void iterate_in_directory(dir_queue_entry_t* removed_entry, DIR* curr_dir, long t_id) {
    struct stat entry_stat;
    struct dirent* curr_entry;
    while ((curr_entry = readdir(curr_dir)) != NULL) {
        char new_pathname[PATH_MAX];
        strcpy(new_pathname, removed_entry->path_name);
        strcat(new_pathname, "/");
        strcat(new_pathname, curr_entry->d_name);
        printf("thread %ld found %s\n", t_id, new_pathname);
        /* if the entry is '.' ot '..' we ignore it */
        if (!is_dot_folder(curr_entry->d_name)) {
            get_stat(new_pathname, &entry_stat);
            
            if (S_ISDIR(entry_stat.st_mode)) {
                /* this is a directory */
                if ((entry_stat.st_mode & S_IRUSR) && (entry_stat.st_mode & S_IXUSR)) {
                    /* This directory can be serched - the process has both read and execute permissions for it.*/
                    /* Get the lock for shared queue */
                    pthread_mutex_lock(&queue_mutex);
                    /* Add to the queue */
                    dir_enqueue(curr_entry->d_name, new_pathname);
                    /* signal the thread that should handle the directory we found.
                    this signal won't necessarily cause that thread to start working 
                    since it could wake before it's turn (for that there is another signaling location
                    just before we go to wait) */
                    if (!is_thread_queue_empty()) {
                        thread_queue_entry_t* thread =  thread_dequeue();
                        pthread_cond_signal(&queue_cv[thread->thread_id]);
                        free(thread);
                    }
                    pthread_mutex_unlock(&queue_mutex);
                } else {
                    /* This directory can't be searched */
                    printf("Directory %s: Permission denied.\n", new_pathname);
                }
            } else {
                /* This is not a directory */
                if (strstr(curr_entry->d_name, g_search_term) != NULL) {
                    /* The file name contains the seatch term */
                    printf("%s\n", new_pathname);
                    g_files_found++;
                }
            }
        }
    } 
}

void open_dir_and_check(DIR** curr_dir, char* path_name) {
    if((*curr_dir = opendir(path_name)) == NULL) {
        thread_exit_on_error();
    }
}

void *searching_entry(void *thread_id) {
    long t_id = (long) thread_id;

    wait_for_start(t_id);

    DIR *curr_dir;
    dir_queue_entry_t* removed_entry;
    int is_working = False;
    while (True) {

        pthread_mutex_lock(&queue_mutex);
        while (should_thread_sleep(t_id)) {
            /* Update number of working threads 
            since updating g_threads_working which is being watched by main thread 
            this is a critical section */
            pthread_mutex_lock(&finish_mutex);
            if (!is_thread_in_queue(t_id)) {
                thread_enqueue(t_id);
            }
            
            print_thread_queue();
            if (is_working) {
                g_threads_working--;
            }
            is_working = False;

            /* going to sleep so signal to main thread 
            (This will potentialy cause the program to finish - main thread will check)
            the pthread_cond_wait is inside this critical section (relative to queue mutex)
            so when main get the signal this thread is realy waiting */
            pthread_cond_signal(&finish_cv);
            pthread_mutex_unlock(&finish_mutex);

            if (!g_all_threads_ready && g_thread_queue.count == g_threads_num) {
                g_all_threads_ready = True;
                thread_queue_entry_t* thread =  thread_dequeue();
                printf("'tail id is %ld\n", thread->thread_id);
                pthread_cond_signal(&queue_cv[thread->thread_id]);
                pthread_cond_wait(&queue_cv[t_id], &queue_mutex);
                free(thread);
                continue;
            }

            if (g_thread_queue.count == g_threads_num && !is_dir_queue_empty()) {
                thread_queue_entry_t* thread =  thread_dequeue();
                printf("pre emption tail is %ld\n", thread->thread_id);
                pthread_cond_signal(&queue_cv[thread->thread_id]);
                free(thread);
            }
            
            printf("thread %ld going to wait num of working threads : %d \n", t_id, g_threads_working);
            pthread_cond_wait(&queue_cv[t_id], &queue_mutex);   
            printf("thread %ld woke up \n", t_id);       
        }
        if (!g_is_search) {
            /* global set to false - Everyone finished so exit.
            release the lock so other threads could exit */
            pthread_mutex_unlock(&queue_mutex);
            pthread_exit((void*) SUCCESS);
        }
        if (!is_working) {
            /* Increment number of working threads only when this thread
            changed from waiting to working */
            pthread_mutex_lock(&finish_mutex);
            g_threads_working++;
            pthread_mutex_unlock(&finish_mutex);
            is_working = True;
        }
        removed_entry = dir_dequeue(); 
        printf("thread %ld removed dir %s\n", t_id, removed_entry->path_name);
        pthread_mutex_unlock(&queue_mutex);

        /* This call can't fail becouse of premission reasons since removed_entry was checked
        to have read and execute premissions before added to the queue 
        we check for error that could happen becouse of other reasons (not premissions) */
        open_dir_and_check(&curr_dir, removed_entry->path_name);
        
        iterate_in_directory(removed_entry, curr_dir, t_id);

        closedir(curr_dir);

        pthread_mutex_lock(&queue_mutex);
        thread_enqueue(t_id);
        pthread_mutex_unlock(&queue_mutex);
    }
    return (void*)FAILURE;

}

int main(int argc, char const *argv[])
{
    /* Validating command line argsuments */
    if (argc != 4) {
        fprintf(stderr, "Number of arguments is wrong \n");
        return FAILURE;
    }

    DIR* root_dir = opendir(argv[1]);
    if (root_dir == NULL) {
        /* Failed openning the root directory */
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    if (!is_searchable(argv[1])) {
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
    pthread_t* threads_arr = (pthread_t*) malloc(g_threads_num * sizeof(pthread_t));
    if (threads_arr == NULL) {
        fprintf(stderr, "Error in malloc for threads array \n");
        return FAILURE;
    }

    /* Init locks and condition variables */
    pthread_mutex_init(&search_mutex, NULL);
    pthread_mutex_init(&finish_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&search_cv, NULL);
    pthread_cond_init(&finish_cv, NULL);

    /* Init codition variables array */
    queue_cv = (pthread_cond_t*) malloc(g_threads_num * sizeof(pthread_cond_t));
    if (queue_cv == NULL) {
        fprintf(stderr, "Error in malloc for condition variables array \n");
        return FAILURE;
    }
    thread_queue_init();
    /* Init threads and their corresponding condition variables */
    for (long t_id = 0; t_id < g_threads_num; t_id++) {
        pthread_create(&threads_arr[t_id], NULL, searching_entry, (void *)t_id);
        pthread_cond_init(&queue_cv[t_id], NULL);
        printf("crated thread %ld \n", t_id);
        // thread_enqueue(t_id);
    }
    
    /* signal all threads that search can start by raising the flag */
    pthread_mutex_lock(&search_mutex);
    printf("main raised g_is_search\n");
    g_is_search = True;
    pthread_cond_broadcast(&search_cv);
    pthread_mutex_unlock(&search_mutex);
    
    pthread_mutex_lock(&finish_mutex); // Start of finish critical section
    /* The only case where the main thread should should enter the program exit flow is when
        1. the dirictories queue is empty AND no thread is working  
        OR
        2. all threads failed */
    while (!(is_dir_queue_empty() && g_threads_working == 0) && g_failed_threads != g_threads_num) {
        pthread_cond_wait(&finish_cv, &finish_mutex);
    }
    g_is_search = False;
    /* In case there is a thread that didn't work at all 
    (More threads than folders) make sure it exits */
    pthread_mutex_lock(&search_mutex);
    pthread_cond_broadcast(&search_cv);
    pthread_mutex_unlock(&search_mutex);

    /* if we get here 2 cases :
    1.  the queue was empty and no threads were working so 
        all threads are waiting - we changed g_is_search to False so they'll wake and exit 
    all threads are waiting - we changed g_is_search to False so they'll wake and exit 
        all threads are waiting - we changed g_is_search to False so they'll wake and exit 
        locking the queue mutex insures that in this point the searching thread are waiting
    2.  all searching threads failed (the signal won't have an effect) */
    pthread_mutex_lock(&queue_mutex);
    for (long t_id = 0; t_id < g_threads_num; t_id++) {
        pthread_cond_signal(&queue_cv[t_id]);
    }
    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_unlock(&finish_mutex);// End of finish critical section

    /* All threads were signaled to exi - tWait for all threads to complete */
    for (int i = 0; i < g_threads_num; i++) {
        pthread_join(threads_arr[i], NULL);
    }
    pthread_mutex_destroy(&search_mutex);
    pthread_cond_destroy(&search_cv);

    printf("Done searching, found %d files\n", g_files_found);

    /* if some thread failed return code is 1*/
    if (g_failed_threads != 0) {
        return FAILURE;
    }
    return SUCCESS;
}
