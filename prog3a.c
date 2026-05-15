/* Лабораторная работа 7.1 — Взаимодействие процессов через очередь сообщений POSIX.
   Программа A (передающая): вызывает getpwnam_r() (функция №17),
   передаёт поле pw_dir в очередь сообщений POSIX без блокировки (O_NONBLOCK).
   Программа B (prog3b.c): принимает сообщения из той же очереди.

   Особенность O_NONBLOCK:
   - mq_send()    при полной очереди не блокируется, возвращает -1 (EAGAIN)
   - mq_receive() при пустой очереди не блокируется, возвращает -1 (EAGAIN)
   Поэтому результат каждого вызова обязательно анализируется. */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>       /* signal(), SIGINT */
#include <errno.h>        /* errno, EAGAIN */
#include <pthread.h>
#include <pwd.h>          /* getpwnam_r(), struct passwd */
#include <mqueue.h>       /* mq_open(), mq_send(), mq_getattr(), mq_setattr(),
                             mq_close(), mq_unlink(), struct mq_attr */
#include <fcntl.h>        /* O_CREAT, O_WRONLY, O_NONBLOCK */
#include <sys/stat.h>     /* mode_t */

/* ------------------------------------------------------------------ */
/* Константы                                                            */
/* ------------------------------------------------------------------ */

/* Имя очереди POSIX — должно начинаться с '/' */
#define MQ_NAME   "/lab7_queue"

/* Права доступа к очереди */
#define MQ_MODE   0644

/* Размер буфера сообщения — должен быть >= mq_msgsize очереди */
#define MSG_SIZE  256

/* ------------------------------------------------------------------ */
/* Глобальные переменные                                                */
/* ------------------------------------------------------------------ */
static volatile int thread_flag = 0;  /* флаг завершения потока */
static mqd_t mqid = (mqd_t)-1;        /* дескриптор очереди сообщений */

/* ------------------------------------------------------------------ */
/* Обработчик SIGINT                                                    */
/* ------------------------------------------------------------------ */

/* sig_handler — при Ctrl+C устанавливает флаг завершения;
   поток выйдет из цикла на следующей итерации */
static void sig_handler(int signo)
{
    printf("\nпрограмма A: получен сигнал %d, завершаем работу\n", signo);
    thread_flag = 1;
}

/* ------------------------------------------------------------------ */
/* Поточная функция: отправка сообщений в очередь                       */
/* ------------------------------------------------------------------ */
static void *thread_sender(void *arg)
{
    (void)arg;
    printf("поток отправитель начал работу\n");

    char pw_buf[1024];  /* вспомогательный буфер для getpwnam_r() */
    char msg[MSG_SIZE]; /* буфер передаваемого сообщения */

    while (thread_flag == 0) {

        struct passwd pwd;
        struct passwd *result = NULL;

        /* getpwuid(getuid()) — получение имени текущего пользователя
           по числовому uid; надёжнее getlogin() в нетерминальных сессиях */
        struct passwd *pw_tmp = getpwuid(getuid());
        if (pw_tmp == NULL) {
            perror("getpwuid");
            sleep(1);
            continue;
        }

        /* getpwnam_r — потокобезопасное получение записи из /etc/passwd
           по имени пользователя (функция №17 из таблицы МУ);
           username — имя пользователя для поиска;
           &pwd     — структура для результата;
           pw_buf   — вспомогательный буфер для строковых полей;
           sizeof   — размер вспомогательного буфера;
           &result  — при успехе *result == &pwd, иначе NULL;
           возвращает 0 при успехе, код ошибки при неудаче */
        int rv = getpwnam_r(pw_tmp->pw_name, &pwd,
                            pw_buf, sizeof(pw_buf), &result);
        if (rv != 0 || result == NULL) {
            fprintf(stderr, "getpwnam_r: ошибка\n");
            sleep(1);
            continue;
        }

        /* Формируем сообщение из поля pw_dir (домашний каталог) */
        snprintf(msg, sizeof(msg), "%s", pwd.pw_dir);

        /* mq_send — отправка сообщения в очередь без блокировки;
           mqid      — дескриптор очереди;
           msg       — указатель на буфер с сообщением;
           strlen+1  — длина сообщения (вместе с нуль-терминатором);
           0         — приоритет сообщения (0 — наименьший);
           при O_NONBLOCK возвращает -1 (errno=EAGAIN) если очередь полна,
           вместо блокирующего ожидания освобождения места */
        int sr = mq_send(mqid, msg, strlen(msg) + 1, 0);
        if (sr == -1) {
            if (errno == EAGAIN) {
                /* очередь полна — не блокируемся, ждём 1 сек и повторяем */
                printf("[отправитель] очередь полна (EAGAIN), повтор через 1 сек\n");
                sleep(1);
            } else {
                perror("mq_send");
            }
        } else {
            /* отправка прошла успешно */
            printf("[отправитель] отправлено: '%s'\n", msg);
            sleep(1); /* задержка 1 с между отправками */
        }
    }

    printf("поток отправитель закончил работу\n");
    pthread_exit((void *)1);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Основная программа                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("программа A начала работу\n");

    /* signal — замена системного обработчика SIGINT на sig_handler */
    signal(SIGINT, sig_handler);

    /* --- Создание очереди сообщений POSIX --------------------------- */

    /* Атрибуты очереди по умолчанию (NULL) — ядро задаёт сам.
       Сначала создаём с NULL, затем читаем и меняем атрибуты. */

    /* mq_open — создание очереди сообщений POSIX для записи;
       MQ_NAME     — имя очереди (должно начинаться с '/');
       O_CREAT     — создать если не существует;
       O_WRONLY    — открыть только для записи (отправитель);
       O_NONBLOCK  — неблокирующий режим: mq_send не блокируется
                     при полной очереди, возвращает -1 (EAGAIN);
       MQ_MODE     — права доступа к очереди;
       NULL        — атрибуты по умолчанию (mq_maxmsg и mq_msgsize
                     берутся из /proc/sys/fs/mqueue/);
       возвращает дескриптор очереди или (mqd_t)-1 при ошибке */
    mqid = mq_open(MQ_NAME, O_CREAT | O_WRONLY | O_NONBLOCK, MQ_MODE, NULL);
    if (mqid == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }
    printf("программа A: очередь '%s' открыта\n", MQ_NAME);

    /* --- Чтение и вывод атрибутов очереди по умолчанию ------------- */

    struct mq_attr attr;

    /* mq_getattr — получение текущих атрибутов очереди;
       mqid  — дескриптор очереди;
       &attr — структура для записи атрибутов:
         mq_flags   — флаги очереди (O_NONBLOCK если установлен);
         mq_maxmsg  — максимальное число сообщений в очереди;
         mq_msgsize — максимальный размер одного сообщения в байтах;
         mq_curmsgs — текущее число сообщений в очереди */
    if (mq_getattr(mqid, &attr) == -1) {
        perror("mq_getattr");
        return 1;
    }
    printf("программа A: атрибуты очереди по умолчанию:\n");
    printf("  mq_flags   = %ld\n", attr.mq_flags);
    printf("  mq_maxmsg  = %ld (макс. число сообщений)\n",  attr.mq_maxmsg);
    printf("  mq_msgsize = %ld (макс. размер сообщения)\n", attr.mq_msgsize);
    printf("  mq_curmsgs = %ld (сообщений в очереди)\n",   attr.mq_curmsgs);

    /* --- Изменение атрибутов: увеличиваем mq_maxmsg ----------------- */

    /* mq_setattr — изменение атрибутов очереди;
       из всех атрибутов изменить можно только mq_flags (O_NONBLOCK);
       mq_maxmsg и mq_msgsize задаются только при создании очереди
       и через mq_setattr не меняются — для их изменения надо
       пересоздать очередь с нужными атрибутами в mq_open */
    struct mq_attr new_attr;
    memset(&new_attr, 0, sizeof(new_attr));
    new_attr.mq_flags   = attr.mq_flags;   /* сохраняем флаги */
    new_attr.mq_maxmsg  = attr.mq_maxmsg;  /* maxmsg не меняется через setattr */
    new_attr.mq_msgsize = attr.mq_msgsize; /* msgsize не меняется через setattr */

    struct mq_attr old_attr;
    /* mq_setattr — устанавливаем атрибуты (здесь демонстрируем вызов);
       &new_attr — новые атрибуты; &old_attr — сюда запишутся старые */
    if (mq_setattr(mqid, &new_attr, &old_attr) == -1) {
        perror("mq_setattr");
    } else {
        printf("программа A: mq_setattr выполнен (mq_maxmsg и mq_msgsize\n");
        printf("  изменяются только при создании очереди через mq_open)\n");
    }

    /* --- Создание потока ------------------------------------------- */

    pthread_t tid;

    /* pthread_create — создание потока-отправителя */
    if (pthread_create(&tid, NULL, thread_sender, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    printf("программа A: для завершения нажмите <Enter>\n");

    /* getchar — блокирующее ожидание нажатия <Enter> */
    getchar();

    thread_flag = 1;

    /* pthread_join — ожидание завершения потока */
    void *ec = NULL;
    pthread_join(tid, &ec);
    printf("поток завершился с кодом: %ld\n", (long)ec);

    /* --- Завершение ------------------------------------------------- */

    /* mq_close — закрытие дескриптора очереди в данном процессе;
       не удаляет очередь из системы */
    if (mq_close(mqid) == -1) {
        perror("mq_close");
    }

    /* mq_unlink — удаление очереди из файловой системы;
       вызывается в ОБЕИХ программах: та, которая завершается второй,
       получит ошибку ENOENT (очередь уже удалена первой), что не
       является критической ошибкой */
    if (mq_unlink(MQ_NAME) == -1) {
        if (errno != ENOENT) {
            perror("mq_unlink");
        }
    }

    printf("программа A завершила работу\n");
    return 0;
}
