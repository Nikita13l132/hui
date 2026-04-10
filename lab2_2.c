/* Программа 2 — критический ресурс с БЛОКИРУЮЩИМ мьютексом.
   Два потока выводят символы на экран.
   Перед входом в КУ поток захватывает мьютекс (pthread_mutex_lock),
   при выходе из КУ — освобождает (pthread_mutex_unlock).
   Ожидаемый вывод: 1111111111 2222222222 1111111111 ... */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* Идентификатор мьютекса — глобальный, чтобы был доступен из потоков */
pthread_mutex_t mutex;

typedef struct {
    int  flag;
    char sym;
} targs;

/* ------------------------------------------------------------------ */
/* Поточная функция 1                                                   */
/* ------------------------------------------------------------------ */
static void *thread_func1(void *arg)
{
    printf("поток 1 начал работу\n");
    fflush(stdout);

    targs *args = (targs *)arg; /* приводим void* к типу targs* */

    while (args->flag == 0) {

        /* pthread_mutex_lock — блокирующий захват мьютекса;
           если мьютекс занят другим потоком, текущий поток
           блокируется и ждёт освобождения мьютекса */
        pthread_mutex_lock(&mutex);

        /* === ВХОД В КРИТИЧЕСКИЙ УЧАСТОК === */
        for (int i = 0; i < 10; i++) {  /* 10 итераций в КУ */
            putchar(args->sym);         /* вывод символа на экран (критический ресурс) */
            fflush(stdout);
            sleep(1);
        }
        /* === ВЫХОД ИЗ КРИТИЧЕСКОГО УЧАСТКА === */

        /* pthread_mutex_unlock — освобождение мьютекса;
           разблокирует один из ожидающих потоков */
        pthread_mutex_unlock(&mutex);

        sleep(1); /* работа потока ВНЕ критического участка */
    }

    printf("\nпоток 1 закончил работу\n");
    fflush(stdout);

    pthread_exit((void *)1);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Поточная функция 2                                                   */
/* ------------------------------------------------------------------ */
static void *thread_func2(void *arg)
{
    printf("поток 2 начал работу\n");
    fflush(stdout);

    targs *args = (targs *)arg; /* приводим void* к типу targs* */

    while (args->flag == 0) {

        /* pthread_mutex_lock — блокирующий захват мьютекса */
        pthread_mutex_lock(&mutex);

        /* === ВХОД В КРИТИЧЕСКИЙ УЧАСТОК === */
        for (int i = 0; i < 10; i++) {
            putchar(args->sym);
            fflush(stdout);
            sleep(1);
        }
        /* === ВЫХОД ИЗ КРИТИЧЕСКОГО УЧАСТКА === */

        /* pthread_mutex_unlock — освобождение мьютекса */
        pthread_mutex_unlock(&mutex);

        sleep(1); /* работа потока ВНЕ критического участка */
    }

    printf("\nпоток 2 закончил работу\n");
    fflush(stdout);

    pthread_exit((void *)2);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Основная программа                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("программа начала работу\n");

    /* pthread_mutex_init — инициализация мьютекса;
       &mutex — указатель на идентификатор мьютекса;
       NULL   — атрибуты по умолчанию */
    int rv = pthread_mutex_init(&mutex, NULL);
    if (rv != 0) { /* ненулевой код — ошибка инициализации */
        perror("pthread_mutex_init");
        return 1;
    }

    pthread_t id1, id2;

    targs arg1 = {0, '1'};
    targs arg2 = {0, '2'};

    /* pthread_create — создание потока 1 */
    int rv1 = pthread_create(&id1, NULL, thread_func1, &arg1);
    if (rv1 != 0) {
        perror("pthread_create 1");
        return 1;
    }

    /* pthread_create — создание потока 2 */
    int rv2 = pthread_create(&id2, NULL, thread_func2, &arg2);
    if (rv2 != 0) {
        perror("pthread_create 2");
        return 1;
    }

    printf("программа ждет нажатия клавиши\n");

    /* getchar — блокирующее ожидание нажатия <Enter> */
    getchar();

    printf("клавиша нажата\n");

    arg1.flag = 1;
    arg2.flag = 1;

    void *ec1 = NULL;
    void *ec2 = NULL;

    /* pthread_join — ожидание завершения потока 1 */
    pthread_join(id1, &ec1);

    /* pthread_join — ожидание завершения потока 2 */
    pthread_join(id2, &ec2);

    printf("поток 1 завершился с кодом: %ld\n", (long)ec1);
    printf("поток 2 завершился с кодом: %ld\n", (long)ec2);

    /* pthread_mutex_destroy — уничтожение мьютекса и освобождение
       связанных с ним ресурсов ОС */
    pthread_mutex_destroy(&mutex);

    printf("программа завершила работу\n");
    return 0;
}
