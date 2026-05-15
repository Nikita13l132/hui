/* Лабораторная работа 7.1 — Взаимодействие процессов через очередь сообщений POSIX.
   Программа B (принимающая): читает сообщения из очереди POSIX без блокировки.
   При пустой очереди mq_receive() возвращает -1 (EAGAIN) — ждём 1 сек и повторяем. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <mqueue.h>       /* mq_open(), mq_receive(), mq_getattr(),
                             mq_close(), mq_unlink() */
#include <fcntl.h>        /* O_CREAT, O_RDONLY, O_NONBLOCK */
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Константы — должны совпадать с программой A                         */
/* ------------------------------------------------------------------ */
#define MQ_NAME   "/lab7_queue"
#define MQ_MODE   0644
#define MSG_SIZE  256

/* ------------------------------------------------------------------ */
/* Глобальные переменные                                                */
/* ------------------------------------------------------------------ */
static volatile int thread_flag = 0;
static mqd_t mqid = (mqd_t)-1;

/* ------------------------------------------------------------------ */
/* Обработчик SIGINT                                                    */
/* ------------------------------------------------------------------ */
static void sig_handler(int signo)
{
    printf("\nпрограмма B: получен сигнал %d, завершаем работу\n", signo);
    thread_flag = 1;
}

/* ------------------------------------------------------------------ */
/* Поточная функция: приём сообщений из очереди                         */
/* ------------------------------------------------------------------ */
static void *thread_receiver(void *arg)
{
    (void)arg;
    printf("поток получатель начал работу\n");

    /* Получаем mq_msgsize для правильного размера буфера.
       Размер буфера при mq_receive ОБЯЗАН быть >= mq_msgsize очереди,
       иначе вызов вернёт ошибку EMSGSIZE */
    struct mq_attr attr;

    /* mq_getattr — получение атрибутов очереди;
       используем mq_msgsize как размер буфера приёма */
    if (mq_getattr(mqid, &attr) == -1) {
        perror("mq_getattr");
        pthread_exit((void *)-1);
    }
    printf("[получатель] mq_msgsize=%ld, mq_maxmsg=%ld\n",
           attr.mq_msgsize, attr.mq_maxmsg);

    /* Выделяем буфер размером mq_msgsize */
    char *buf = malloc((size_t)attr.mq_msgsize);
    if (buf == NULL) {
        perror("malloc");
        pthread_exit((void *)-1);
    }

    while (thread_flag == 0) {

        memset(buf, 0, (size_t)attr.mq_msgsize);

        /* mq_receive — приём сообщения из очереди без блокировки;
           mqid              — дескриптор очереди;
           buf               — буфер для принятого сообщения;
           attr.mq_msgsize   — размер буфера (должен быть >= mq_msgsize!);
           NULL              — приоритет сообщения не нужен;
           при O_NONBLOCK возвращает число прочитанных байт при успехе,
           -1 (errno=EAGAIN) если очередь пуста — вместо блокировки */
        ssize_t rr = mq_receive(mqid, buf, (size_t)attr.mq_msgsize, NULL);
        if (rr > 0) {
            /* сообщение успешно принято */
            printf("[получатель] принято (%zd байт): '%s'\n", rr, buf);
        } else if (rr == -1) {
            if (errno == EAGAIN) {
                /* очередь пуста — ждём 1 сек и повторяем попытку */
                printf("[получатель] очередь пуста (EAGAIN), повтор через 1 сек\n");
                sleep(1);
            } else {
                perror("mq_receive");
            }
        }
    }

    free(buf);
    printf("поток получатель закончил работу\n");
    pthread_exit((void *)2);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Основная программа                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("программа B начала работу\n");

    /* signal — замена системного обработчика SIGINT на sig_handler */
    signal(SIGINT, sig_handler);

    /* --- Открытие очереди сообщений POSIX для чтения --------------- */

    /* mq_open — открытие очереди POSIX для приёма сообщений;
       MQ_NAME    — имя очереди (то же что в программе A);
       O_CREAT    — создать если не существует (на случай запуска B первой);
       O_RDONLY   — открыть только для чтения (получатель);
       O_NONBLOCK — неблокирующий режим: mq_receive не блокируется
                    при пустой очереди, возвращает -1 (EAGAIN);
       MQ_MODE    — права доступа */
    mqid = mq_open(MQ_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, MQ_MODE, NULL);
    if (mqid == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }
    printf("программа B: очередь '%s' открыта\n", MQ_NAME);

    /* --- Чтение и вывод атрибутов очереди -------------------------- */

    struct mq_attr attr;

    /* mq_getattr — получение текущих атрибутов очереди */
    if (mq_getattr(mqid, &attr) == -1) {
        perror("mq_getattr");
        return 1;
    }
    printf("программа B: атрибуты очереди:\n");
    printf("  mq_flags   = %ld\n", attr.mq_flags);
    printf("  mq_maxmsg  = %ld (макс. число сообщений)\n",  attr.mq_maxmsg);
    printf("  mq_msgsize = %ld (макс. размер сообщения)\n", attr.mq_msgsize);
    printf("  mq_curmsgs = %ld (сообщений в очереди)\n",   attr.mq_curmsgs);

    /* --- Создание потока ------------------------------------------- */

    pthread_t tid;

    /* pthread_create — создание потока-получателя */
    if (pthread_create(&tid, NULL, thread_receiver, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    printf("программа B: для завершения нажмите <Enter>\n");

    /* getchar — блокирующее ожидание нажатия <Enter> */
    getchar();

    thread_flag = 1;

    /* pthread_join — ожидание завершения потока */
    void *ec = NULL;
    pthread_join(tid, &ec);
    printf("поток завершился с кодом: %ld\n", (long)ec);

    /* --- Завершение ------------------------------------------------- */

    /* mq_close — закрытие дескриптора очереди в данном процессе */
    if (mq_close(mqid) == -1) {
        perror("mq_close");
    }

    /* mq_unlink — удаление очереди из файловой системы;
       вызывается в ОБЕИХ программах: та, что завершается второй,
       получит ENOENT (очередь уже удалена первой) — это не ошибка */
    if (mq_unlink(MQ_NAME) == -1) {
        if (errno != ENOENT) {
            perror("mq_unlink");
        }
    }

    printf("программа B завершила работу\n");
    return 0;
}
