/* Лабораторная работа 8 — Сетевое взаимодействие через сокеты INET TCP.
   Клиент: подключается к серверу, посылает запросы с номером, принимает
   ответы (поле pw_dir из getpwnam_r на стороне сервера).

   Архитектура потоков клиента:
     main()         — создаёт сокет, запускает поток connect
     thread_connect — в цикле пытается подключиться к серверу; при успехе
                      создаёт thread_send и thread_recv, завершается
     thread_send    — в цикле формирует запрос (номер) и отправляет серверу
     thread_recv    — в цикле принимает ответы от сервера */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>     /* socket(), connect(), send(), recv(), shutdown() */
#include <netinet/in.h>     /* struct sockaddr_in, htons() */
#include <arpa/inet.h>      /* inet_addr() */
#include <fcntl.h>          /* fcntl(), F_SETFL, O_NONBLOCK */

/* ------------------------------------------------------------------ */
/* Константы                                                            */
/* ------------------------------------------------------------------ */
#define SERVER_IP    "127.0.0.1"  /* адрес сервера */
#define SERVER_PORT  7000          /* порт сервера */
#define BUF_SIZE     2048

/* ------------------------------------------------------------------ */
/* Глобальные переменные                                                */
/* ------------------------------------------------------------------ */
static volatile int stop_flag = 0;
static int sock_fd = -1;           /* сокет соединения с сервером */

/* ------------------------------------------------------------------ */
/* Обработчик SIGINT                                                    */
/* ------------------------------------------------------------------ */
static void sig_handler(int signo)
{
    printf("\nклиент: получен сигнал %d\n", signo);
    stop_flag = 1;
    if (sock_fd != -1) shutdown(sock_fd, SHUT_RDWR);
}

/* ------------------------------------------------------------------ */
/* Поток передачи запросов серверу                                      */
/* ------------------------------------------------------------------ */
static void *thread_send(void *arg)
{
    (void)arg;
    printf("клиент: поток send начал работу\n");

    char sndbuf[BUF_SIZE];
    int req_num = 1; /* последовательно увеличивающийся номер запроса */

    while (stop_flag == 0) {

        /* Формируем запрос: номер + имя пользователя */
        snprintf(sndbuf, sizeof(sndbuf), "запрос №%d", req_num++);

        /* send — передача запроса серверу;
           sock_fd   — дескриптор сокета соединения;
           sndbuf    — буфер с запросом;
           strlen+1  — длина данных (с нуль-терминатором);
           0         — флаги */
        int sc = send(sock_fd, sndbuf, strlen(sndbuf) + 1, 0);
        if (sc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleep(1);
                continue;
            }
            if (stop_flag == 0) perror("send");
            stop_flag = 1;
            break;
        }
        printf("клиент [send]: отправлен запрос: '%s'\n", sndbuf);
        sleep(1); /* интервал 1 с между запросами */
    }

    printf("клиент: поток send завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Поток приёма ответов от сервера                                      */
/* ------------------------------------------------------------------ */
static void *thread_recv(void *arg)
{
    (void)arg;
    printf("клиент: поток recv начал работу\n");

    char rcvbuf[BUF_SIZE];

    while (stop_flag == 0) {

        memset(rcvbuf, 0, sizeof(rcvbuf));

        /* recv — приём ответа от сервера;
           sock_fd  — дескриптор сокета соединения;
           rcvbuf   — буфер для принятых данных;
           sizeof   — размер буфера;
           0        — флаги;
           возвращает число байт, 0 (сервер отключился) или -1 (ошибка) */
        int rc = recv(sock_fd, rcvbuf, sizeof(rcvbuf) - 1, 0);
        if (rc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleep(1);
                continue;
            }
            if (stop_flag == 0) perror("recv");
            stop_flag = 1;
            break;
        } else if (rc == 0) {
            /* сервер закрыл соединение */
            printf("клиент: сервер отключился\n");
            stop_flag = 1;
            break;
        }
        printf("клиент [recv]: получен ответ: '%s'\n", rcvbuf);
    }

    printf("клиент: поток recv завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Поток установления соединения с сервером                             */
/* ------------------------------------------------------------------ */
static void *thread_connect(void *arg)
{
    (void)arg;
    printf("клиент: поток connect начал работу, подключаюсь к серверу...\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);

    /* inet_addr — преобразование IP-адреса из текстового вида в двоичный
       в сетевом порядке байт */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    while (stop_flag == 0) {

        /* connect — установление соединения с сервером;
           sock_fd      — дескриптор сокета клиента;
           &server_addr — структура с адресом сервера (ip + порт);
           sizeof       — размер структуры;
           при O_NONBLOCK возвращает -1 (EINPROGRESS) пока соединение
           устанавливается, или -1 (ECONNREFUSED) если сервер недоступен;
           возвращает 0 при успешном установлении соединения */
        int result = connect(sock_fd,
                             (struct sockaddr *)&server_addr,
                             sizeof(server_addr));
        if (result == -1) {
            if (errno == EISCONN) {
                /* соединение уже установлено (повторный вызов) */
                result = 0;
            } else if (errno == EINPROGRESS || errno == EALREADY) {
                /* соединение устанавливается — ждём */
                sleep(1);
                continue;
            } else {
                perror("connect");
                sleep(1);
                continue;
            }
        }

        /* Соединение установлено */
        printf("клиент: подключён к серверу %s:%d\n",
               SERVER_IP, SERVER_PORT);

        pthread_t tid_send, tid_recv;

        /* pthread_create — создание потока передачи запросов */
        if (pthread_create(&tid_send, NULL, thread_send, NULL) != 0) {
            perror("pthread_create send");
            break;
        }
        pthread_detach(tid_send);

        /* pthread_create — создание потока приёма ответов */
        if (pthread_create(&tid_recv, NULL, thread_recv, NULL) != 0) {
            perror("pthread_create recv");
            break;
        }
        pthread_detach(tid_recv);

        /* Текущий поток завершается — соединение установлено */
        pthread_exit(NULL);
    }

    printf("клиент: поток connect завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Основная программа                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("клиент начал работу\n");

    signal(SIGINT, sig_handler);

    /* socket — создание сокета TCP/INET;
       AF_INET     — протокол Интернет;
       SOCK_STREAM — потоковый тип (TCP);
       0           — протокол по умолчанию */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return 1;
    }
    printf("клиент: сокет создан\n");

    /* fcntl — перевод сокета в неблокирующий режим;
       connect() не будет блокироваться при недоступном сервере */
    if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        return 1;
    }

    pthread_t tid_connect;

    /* pthread_create — создание потока установления соединения */
    if (pthread_create(&tid_connect, NULL, thread_connect, NULL) != 0) {
        perror("pthread_create connect");
        return 1;
    }
    pthread_detach(tid_connect);

    printf("клиент: для завершения нажмите <Enter>\n");

    /* getchar — ожидание нажатия <Enter> */
    getchar();

    stop_flag = 1;
    if (sock_fd != -1) shutdown(sock_fd, SHUT_RDWR);

    sleep(1); /* даём потокам завершиться */

    /* close — закрытие дескриптора сокета */
    close(sock_fd);

    printf("клиент завершил работу\n");
    return 0;
}
