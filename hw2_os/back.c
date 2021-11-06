#include <stdio.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    for (size_t i = 0; i < 10; i++)
    {
        printf("in back %d \n", i);
        sleep(10);
    }
    printf("finished back !\n");
    return 0;
}
