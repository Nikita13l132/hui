#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

/* Структура для передачи параметров в поток */
typedef struct {
    int  flag;  /* флаг завершения: 0 — работать, 1 — завершиться */
    char sym;   /* символ, который выводит поток */
} targs;

/* Поточная функция 1 */
static void *thread_func1(void *arg)
{
    targs *args = (targs *)arg;

    while (args->flag == 0) {
        putchar(args->sym);
        fflush(stdout);
        sleep(1);
    }

    /* Передаём код завершения */
    pthread_exit((void *)1);
    return NULL;
}

/* Поточная функция 2 */
static void *thread_func2(void *arg)
{
    targs *args = (targs *)arg;

    while (args->flag == 0) {
        putchar(args->sym);
        fflush(stdout);
        sleep(1);
    }

    pthread_exit((void *)2);
    return NULL;
}

int main(void)
{
    pthread_t id1, id2;

    /* Флаги завершения локальны в main — передаём адреса в потоки */
    targs arg1 = {0, '1'};
    targs arg2 = {0, '2'};

    /* Создаём потоки */
    if (pthread_create(&id1, NULL, thread_func1, &arg1) != 0) {
        perror("pthread_create 1");
        return 1;
    }
    if (pthread_create(&id2, NULL, thread_func2, &arg2) != 0) {
        perror("pthread_create 2");
        return 1;
    }

    printf("Потоки запущены. Нажмите <Enter> для завершения...\n");
    getchar();

    /* Устанавливаем флаги завершения */
    arg1.flag = 1;
    arg2.flag = 1;

    /* Ждём завершения потоков и читаем коды завершения */
    void *exitcode1 = NULL;
    void *exitcode2 = NULL;

    pthread_join(id1, &exitcode1);
    pthread_join(id2, &exitcode2);

    printf("\nПоток 1 завершился с кодом: %ld\n", (long)exitcode1);
    printf("Поток 2 завершился с кодом: %ld\n", (long)exitcode2);

    return 0;
}
