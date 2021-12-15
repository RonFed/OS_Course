#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define SUCCESS         (0)
#define FAILURE         (1)

#define True            (1)
#define False           (0)




int is_dir_queue_empty() {
    // TODO
    return False;
}

void *seatching_entry(void *thread_id) {

}

static int is_searchable(const char* dir_path) {
    struct stat root_stat;
    if (stat(dir_path, &root_stat) == -1) {
        fprintf(stderr, "%s \n", strerror(errno));
        return FAILURE;
    }

    /* A directory can be searched if the process has both read and execute permissions for it */
    return ((root_stat.st_mode & S_IRUSR) && (root_stat.st_mode & S_IXUSR));
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
    int threads_num = atoi(argv[3]);
    pthread_t* threads_arr = (pthread_t*) malloc(threads_num * sizeof(pthread_t));
    if (threads_arr == NULL) {
        fprintf(stderr, "Error in malloc for threads array \n");
        return FAILURE;
    }


    return SUCCESS;
}
