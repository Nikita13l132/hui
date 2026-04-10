/* Программа 3 — критический ресурс с НЕБЛОКИРУЮЩИМ мьютексом (trylock).
   Вместо блокирующего pthread_mutex_lock используется
   pthread_mutex_trylock в цикле опроса.
   Преимущество: поток может проверять флаг завершения даже когда
   мьютекс занят — это позволяет корректно завершить программу
   даже если мьютекс захвачен кем-то извне (например, из main()).
   Ожидаемый вывод (в норме): 1111111111 2222222222 ... */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* Идентификатор мьютекса — глобальный */
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

        /* Цикл неблокирующего захвата мьютекса.
           pthread_mutex_trylock — попытка захватить мьютекс без блокировки:
             возвращает 0      — мьютекс захвачен успешно;
             возвращает EBUSY  — мьютекс занят, поток НЕ блокируется.
           Преимущество перед lock: внутри цикла можно проверять флаг
           завершения и корректно выйти из потока даже если мьютекс занят. */
        while (1) {
            int rv = pthread_mutex_trylock(&mutex);
            if (rv == 0) {
                break; /* мьютекс захвачен — входим в КУ */
            }
            /* мьютекс занят — выводим сообщение для наглядности */
            printf("[поток 1] мьютекс занят: %s\n", strerror(rv));
            fflush(stdout);

            /* Проверяем флаг завершения, пока ждём мьютекс.
               Это ключевое преимущество trylock перед lock:
               при lock поток был бы заблокирован и не мог бы
               проверить флаг и выйти. */
            if (args->flag != 0) {
                printf("\nпоток 1 закончил работу\n");
                fflush(stdout);
                pthread_exit((void *)1);
            }

            sleep(1); /* небольшая задержка перед следующей попыткой */
        }

        /* === ВХОД В КРИТИЧЕСКИЙ УЧАСТОК === */
        for (int i = 0; i < 10; i++) {
            putchar(args->sym);  /* вывод символа на экран (критический ресурс) */
            fflush(stdout);
            sleep(1);
        }
        /* === ВЫХОД ИЗ КРИТИЧЕСКОГО УЧАСТКА === */

        /* pthread_mutex_unlock — освобождение мьютекса */
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

        /* Цикл неблокирующего захвата мьютекса (аналогично потоку 1) */
        while (1) {
            /* pthread_mutex_trylock — неблокирующая попытка захватить мьютекс */
            int rv = pthread_mutex_trylock(&mutex);
            if (rv == 0) {
                break; /* мьютекс захвачен */
            }
            printf("[поток 2] мьютекс занят: %s\n", strerror(rv));
            fflush(stdout);

            /* Проверка флага завершения во время ожидания мьютекса */
            if (args->flag != 0) {
                printf("\nпоток 2 закончил работу\n");
                fflush(stdout);
                pthread_exit((void *)2);
            }

            sleep(1);
        }

        /* === ВХОД В КРИТИЧЕСКИЙ УЧАСТОК === */
        for (int i = 0; i < 10; i++) {
            putchar(args->sym);
            fflush(stdout);
            sleep(1);
        }
        /* === ВЫХОД ИЗ КРИТИЧЕСКОГО УЧАСТКА === */

        /* pthread_mutex_unlock — освобождение мьютекса */
        pthread_mutex_unlock(&mutex);

        sleep(1); /* работа ВНЕ критического участка */
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
       NULL — атрибуты по умолчанию */
    int rv = pthread_mutex_init(&mutex, NULL);
    if (rv != 0) {
        perror("pthread_mutex_init");
        return 1;
    }

    /* Демонстрация преимущества trylock перед lock:
       захватим мьютекс из main() — потоки не смогут войти в КУ,
       но благодаря trylock они не заблокируются намертво,
       а смогут увидеть флаг завершения и корректно выйти.
       В программе 2 (с lock) потоки были бы заблокированы навсегда
       и завершить программу по <Enter> было бы невозможно. */
    pthread_mutex_lock(&mutex); /* захватываем мьютекс из main() */
    printf("мьютекс захвачен из main() — потоки будут опрашивать trylock\n");

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

    /* getchar — блокирующее ожидание <Enter>;
       в это время потоки крутятся в trylock-цикле и выводят
       сообщения "мьютекс занят", демонстрируя неблокирующее ожидание */
    getchar();

    printf("клавиша нажата\n");

    /* Освобождаем мьютекс, захваченный из main() */
    pthread_mutex_unlock(&mutex);

    /* Устанавливаем флаги завершения */
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

    /* pthread_mutex_destroy — уничтожение мьютекса */
    pthread_mutex_destroy(&mutex);

    printf("программа завершила работу\n");
    return 0;
}
