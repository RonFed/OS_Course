#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define SUCCESS         (0)
#define FAILURE         (1)

#define True            (1)
#define False           (0)

int g_is_search = False;
/* Array of threads wait flag 
if g_threads_waiting[i] == True then thread i is idle (waiting) */
int* g_threads_waiting;
int g_threads_num;

pthread_mutex_t search_mutex;
pthread_cond_t search_cv;

typedef struct queue_entry_t {
    char path_name[PATH_MAX];
    char dir_name[PATH_MAX];
    struct queue_entry_t* next;
    struct queue_entry_t* prev;
} queue_entry_t;

typedef struct {
    queue_entry_t* head;
    queue_entry_t* tail;
    unsigned int count;
} dir_queue_t;

dir_queue_t g_queue;

int is_dir_queue_empty() {
    return g_queue.count == 0;
}

queue_entry_t* dir_dequeue() {
    /* Assumes the queue isn't empty */
    queue_entry_t* removed_entry = g_queue.tail;
    if (g_queue.count == 1) {
        /* The queue will become empty */
        g_queue.head = NULL;
        g_queue.tail = NULL;
    } else {
        g_queue.tail = g_queue.tail->prev;
        g_queue.tail->next = NULL;
    }
    g_queue.count--;
    return removed_entry;
}

void dir_enqueue(const char* dir_name, const char* path_name) {
    queue_entry_t* new_entry = (queue_entry_t*) malloc(sizeof(queue_entry_t));
    if (new_entry == NULL) {
        // TODO WHAT TO DO HERE ???
    }
    /* new_entry is inserted as the new head */
    strcpy(new_entry->dir_name, dir_name);
    strcpy(new_entry->path_name, path_name);
    /* if the queue is empty next will be NULL (single element in queue) */
    new_entry->next = g_queue.head;
    new_entry->prev = NULL;
    if (g_queue.count == 0) {
        g_queue.tail = new_entry;
    } else {
        g_queue.head->prev = new_entry;
    }
    g_queue.head = new_entry;
    g_queue.count++;
}

void dir_queue_init() {
    g_queue.count = 0;
    g_queue.head = NULL;
    g_queue.tail = NULL;
}

int is_searchable(const char* dir_path) {
    struct stat root_stat;
    if (stat(dir_path, &root_stat) == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    /* A directory can be searched if the process has both read and execute permissions for it */
    return ((root_stat.st_mode & S_IRUSR) && (root_stat.st_mode & S_IXUSR));
}

void get_stat(struct dirent* dir_entry, struct stat* entry_stat) {
    char* entry_name = dir_entry->d_name;
    if (stat(entry_name, entry_stat) == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
    }

    // return S_ISDIR(entry_stat.st_mode);
    // return entry_stat;
}

void *searching_entry(void *thread_id) {
    long t_id = (long) thread_id;
    pthread_mutex_lock(&search_mutex);
    while (!g_is_search) {
        pthread_cond_wait(&search_cv, &search_mutex);
        printf("thread %ld woke up for the first time \n", (long)thread_id);
    }
    pthread_mutex_unlock(&search_mutex);


    DIR *curr_dir;
    struct dirent* curr_entry;
    queue_entry_t* removed_entry;

    while (g_is_search) {

        pthread_mutex_lock(&search_mutex);
        while (is_dir_queue_empty()) {
            /* Mark this thread as waiting */
            g_threads_waiting[thread_id] = True;
            pthread_cond_wait(&search_cv, &search_mutex);
        }
        g_threads_waiting[thread_id] = False;
        removed_entry = dir_dequeue();
        pthread_mutex_unlock(&search_mutex);


        if ((curr_dir = opendir(removed_entry->dir_name)) == NULL) {
            // TODO
            // fprintf(stderr, "Can't open %s\n", dir);
            // return 0;
        }

        struct stat entry_stat;
        int dir_entries_found = 0;
        while ((curr_entry = readdir(curr_dir)) != NULL) {
            get_stat(curr_entry, &entry_stat);
            if (S_ISDIR(entry_stat.st_mode)) {
                printf("thresd %ld found dir %s inside dir %s \n", (long)thread_id, curr_entry->d_name, removed_entry->dir_name);
                if (is_searchable(curr_entry->d_name)) {
                    char new_pathname[PATH_MAX];
                    strcpy(new_pathname, removed_entry->dir_name);
                    strcat(new_pathname, "/");
                    strcat(new_pathname, curr_entry->d_name);
                    dir_entries_found++;
                    pthread_mutex_lock(&search_mutex);
                    dir_enqueue(curr_entry->d_name, new_pathname);
                    pthread_mutex_unlock(&search_mutex);
                } else {
                    // TODO 
                }
            }
        }

        pthread_mutex_lock(&search_mutex);
        int is_someone_working = False;
        for (int i = 0; i < g_threads_num; i++)
        {
            if (g_threads_waiting[i] == False) {
                /* Found a working thread */
                is_someone_working = True;
                break;
            }
        }
        if (!is_someone_working) {
            /* All threads are idle */
            g_is_search = False;
            pthread_cond_broadcast(&search_cv)
        }
        pthread_mutex_unlock(&search_mutex);
    }

    pthread_exit((void*) SUCCESS);
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
    
     /* End of arguments validation */

    /* Number of threades is assumed to be a non negative int */
    g_threads_num = atoi(argv[3]);
    pthread_t* threads_arr = (pthread_t*) malloc(g_threads_num * sizeof(pthread_t));
    if (threads_arr == NULL) {
        fprintf(stderr, "Error in malloc for threads array \n");
        return FAILURE;
    }
    g_threads_waiting = (int*)calloc(g_threads_num, sizeof(int));
    if (g_threads_waiting == NULL) {
        fprintf(stderr, "Error in calloc for g_threads_waiting \n");
        return FAILURE;
    }

    pthread_mutex_init(&search_mutex, NULL);
    pthread_cond_init(&search_cv, NULL);

    for (long t_id = 0; t_id < g_threads_num; t_id++) {
        pthread_create(&threads_arr[t_id], NULL, searching_entry, (void *)t_id);
    }
    
    g_is_search = True;
    pthread_cond_broadcast(&search_cv);



    return SUCCESS;
}
