/* Лабораторная работа 8 — Сетевое взаимодействие через сокеты INET TCP.
   Сервер: ждёт подключения клиента, принимает запросы (номер + строка),
   кладёт в очередь, поток передачи берёт запрос из очереди, вызывает
   getpwnam_r() (функция №17), формирует ответ и отправляет клиенту.

   Архитектура потоков сервера:
     main()         — создаёт сокет, bind, listen, запускает поток accept
     thread_accept  — в цикле вызывает accept(); при подключении клиента
                      создаёт thread_recv и thread_send, завершается
     thread_recv    — в цикле вызывает recv(); кладёт запросы в очередь
     thread_send    — в цикле берёт запрос из очереди, вызывает getpwnam_r(),
                      отправляет ответ клиенту через send() */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <pwd.h>            /* getpwnam_r(), struct passwd */
#include <sys/types.h>
#include <sys/socket.h>     /* socket(), bind(), listen(), accept(),
                               send(), recv(), shutdown() */
#include <netinet/in.h>     /* struct sockaddr_in, htons(), INADDR_ANY */
#include <arpa/inet.h>      /* inet_ntoa() */
#include <fcntl.h>          /* fcntl(), F_SETFL, O_NONBLOCK */

/* ------------------------------------------------------------------ */
/* Константы                                                            */
/* ------------------------------------------------------------------ */
#define SERVER_PORT  7000       /* порт сервера */
#define BUF_SIZE     2048        /* размер буфера приёма/передачи */
#define QUEUE_MAX    64         /* максимальный размер очереди запросов */

/* ------------------------------------------------------------------ */
/* Очередь запросов (кольцевой буфер со строками)                      */
/* ------------------------------------------------------------------ */
static char    req_queue[QUEUE_MAX][1024]; /* кольцевой буфер */
static int     req_head  = 0;  /* индекс начала очереди */
static int     req_tail  = 0;  /* индекс конца очереди */
static int     req_count = 0;  /* текущее число элементов */

/* Мьютекс для защиты очереди — общий ресурс потоков recv и send */
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Глобальные переменные                                                */
/* ------------------------------------------------------------------ */
static volatile int stop_flag = 0;  /* флаг завершения всех потоков */
static int listen_fd  = -1;          /* слушающий сокет */
static int client_fd  = -1;          /* сокет соединения с клиентом */

/* ------------------------------------------------------------------ */
/* Обработчик SIGINT                                                    */
/* ------------------------------------------------------------------ */
static void sig_handler(int signo)
{
    printf("\nсервер: получен сигнал %d\n", signo);
    stop_flag = 1;
    /* shutdown разблокирует потоки, заблокированные в recv()/send() */
    if (client_fd  != -1) shutdown(client_fd,  SHUT_RDWR);
    if (listen_fd  != -1) shutdown(listen_fd,  SHUT_RDWR);
}

/* ------------------------------------------------------------------ */
/* Поток приёма запросов от клиента                                     */
/* ------------------------------------------------------------------ */
static void *thread_recv(void *arg)
{
    (void)arg;
    printf("сервер: поток recv начал работу\n");

    char buf[BUF_SIZE];

    while (stop_flag == 0) {

        memset(buf, 0, sizeof(buf));

        /* recv — приём данных от клиента через установленное соединение;
           client_fd — дескриптор сокета соединения с клиентом;
           buf       — буфер для принятых данных;
           sizeof    — размер буфера;
           0         — флаги (стандартный режим);
           возвращает число байт, 0 (клиент отключился) или -1 (ошибка) */
        int rc = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (rc == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* O_NONBLOCK: данных нет — ждём и повторяем */
                sleep(1);
                continue;
            }
            if (stop_flag == 0) perror("recv");
            break;
        } else if (rc == 0) {
            /* клиент закрыл соединение */
            printf("сервер: клиент отключился\n");
            stop_flag = 1;
            break;
        }

        printf("сервер [recv]: получен запрос: '%s'\n", buf);

        /* Кладём запрос в конец очереди под мьютексом */

        /* pthread_mutex_lock — захват мьютекса очереди;
           очередь — общий ресурс с потоком send */
        pthread_mutex_lock(&queue_mutex);
        if (req_count < QUEUE_MAX) {
            strncpy(req_queue[req_tail], buf, BUF_SIZE - 1);
            req_tail = (req_tail + 1) % QUEUE_MAX;
            req_count++;
        } else {
            printf("сервер [recv]: очередь полна, запрос отброшен\n");
        }
        /* pthread_mutex_unlock — освобождение мьютекса */
        pthread_mutex_unlock(&queue_mutex);
    }

    printf("сервер: поток recv завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Поток передачи ответов клиенту                                       */
/* ------------------------------------------------------------------ */
static void *thread_send(void *arg)
{
    (void)arg;
    printf("сервер: поток send начал работу\n");

    char pw_buf[1024];
    char sndbuf[BUF_SIZE];
    char request[BUF_SIZE];

    while (stop_flag == 0) {

        /* pthread_mutex_lock — захват мьютекса для проверки очереди */
        pthread_mutex_lock(&queue_mutex);

        if (req_count > 0) {
            /* берём первый запрос из очереди */
            strncpy(request, req_queue[req_head], BUF_SIZE - 1);
            req_head = (req_head + 1) % QUEUE_MAX;
            req_count--;
            /* pthread_mutex_unlock — освобождаем мьютекс до вызова
               getpwnam_r(), чтобы не держать его во время тяжёлого
               системного вызова */
            pthread_mutex_unlock(&queue_mutex);
        } else {
            /* очередь пуста */
            pthread_mutex_unlock(&queue_mutex);
            sleep(1);
            continue;
        }

        /* Обрабатываем запрос: вызываем getpwnam_r() (функция №17).
           В качестве имени пользователя используем имя текущего
           пользователя сервера — демонстрируем работу функции. */
        struct passwd pwd;
        struct passwd *result = NULL;

        /* getpwuid(getuid()) — получение имени текущего пользователя */
        struct passwd *pw_tmp = getpwuid(getuid());
        if (pw_tmp == NULL) {
            perror("getpwuid");
            snprintf(sndbuf, sizeof(sndbuf), "ошибка getpwuid; запрос=%.64s", request);
        } else {
            /* getpwnam_r — потокобезопасное получение записи из /etc/passwd
               по имени пользователя (функция №17 из таблицы МУ);
               возвращает 0 при успехе, код ошибки при неудаче */
            int rv = getpwnam_r(pw_tmp->pw_name, &pwd,
                                pw_buf, sizeof(pw_buf), &result);
            if (rv != 0 || result == NULL) {
                snprintf(sndbuf, sizeof(sndbuf),
                         "ошибка getpwnam_r; запрос=%.64s", request);
            } else {
                /* Формируем ответ: номер запроса + pw_dir.
                   Ограничиваем каждое поле чтобы гарантированно уложиться
                   в буфер sndbuf (BUF_SIZE байт) */
                char req_short[64];
                strncpy(req_short, request, sizeof(req_short) - 1);
                req_short[sizeof(req_short) - 1] = '\0';
                char dir_short[256];
                strncpy(dir_short, pwd.pw_dir, sizeof(dir_short) - 1);
                dir_short[sizeof(dir_short) - 1] = '\0';
                snprintf(sndbuf, sizeof(sndbuf),
                         "ответ на [%s]: pw_dir='%s'", req_short, dir_short);
            }
        }

        /* send — передача ответа клиенту;
           client_fd — дескриптор сокета соединения;
           sndbuf    — буфер с ответом;
           strlen+1  — длина данных (с нуль-терминатором);
           0         — флаги */
        int sc = send(client_fd, sndbuf, strlen(sndbuf) + 1, 0);
        if (sc == -1) {
            if (stop_flag == 0) perror("send");
            break;
        }
        printf("сервер [send]: отправлен ответ: '%s'\n", sndbuf);
    }

    printf("сервер: поток send завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Поток ожидания подключения клиента                                   */
/* ------------------------------------------------------------------ */
static void *thread_accept(void *arg)
{
    (void)arg;
    printf("сервер: поток accept начал работу, жду клиента...\n");

    while (stop_flag == 0) {

        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        /* accept — ожидание входящего подключения от клиента;
           listen_fd    — дескриптор слушающего сокета;
           &client_addr — сюда будет записан адрес подключившегося клиента;
           &addrlen     — размер структуры адреса;
           возвращает дескриптор нового сокета для обмена данными или -1 */
        int fd = accept(listen_fd,
                        (struct sockaddr *)&client_addr, &addrlen);
        if (fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                sleep(1);
                continue;
            }
            if (stop_flag == 0) perror("accept");
            break;
        }

        client_fd = fd;
        printf("сервер: клиент подключился: ip=%s port=%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        /* Новый сокет наследует O_NONBLOCK от слушающего сокета */

        /* Создаём потоки приёма и передачи и завершаем текущий поток */
        pthread_t tid_recv, tid_send;

        /* pthread_create — создание потока приёма запросов */
        if (pthread_create(&tid_recv, NULL, thread_recv, NULL) != 0) {
            perror("pthread_create recv");
            break;
        }
        /* pthread_detach — поток завершится сам, join не нужен */
        pthread_detach(tid_recv);

        /* pthread_create — создание потока передачи ответов */
        if (pthread_create(&tid_send, NULL, thread_send, NULL) != 0) {
            perror("pthread_create send");
            break;
        }
        pthread_detach(tid_send);

        /* Текущий поток accept завершается — клиент уже подключён */
        pthread_exit(NULL);
    }

    printf("сервер: поток accept завершил работу\n");
    pthread_exit(NULL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Основная программа                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("сервер начал работу\n");

    signal(SIGINT, sig_handler);

    /* socket — создание сокета TCP/INET;
       AF_INET     — семейство адресов: протокол Интернет;
       SOCK_STREAM — тип: потоковый (надёжная передача, TCP);
       0           — протокол по умолчанию (TCP для SOCK_STREAM) */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }
    printf("сервер: сокет создан\n");

    /* setsockopt — установка свойства SO_REUSEADDR;
       позволяет повторно занять порт сразу после завершения программы,
       не ждать таймаута (TIME_WAIT) */
    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        return 1;
    }

    /* fcntl — перевод слушающего сокета в неблокирующий режим;
       F_SETFL    — команда: установить флаги дескриптора;
       O_NONBLOCK — флаг неблокирующего режима:
                    accept() возвращает EAGAIN если клиентов нет */
    if (fcntl(listen_fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        return 1;
    }

    /* Заполняем структуру адреса сервера */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;       /* протокол Интернет */
    server_addr.sin_port        = htons(SERVER_PORT); /* порт в сетевом порядке байт */
    server_addr.sin_addr.s_addr = INADDR_ANY;    /* принимать на всех интерфейсах */

    /* bind — привязка сокета к адресу (ip + порт);
       listen_fd       — дескриптор сокета;
       &server_addr    — структура с адресом сервера;
       sizeof          — размер структуры */
    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        perror("bind");
        return 1;
    }
    printf("сервер: сокет привязан к порту %d\n", SERVER_PORT);

    /* listen — перевод сокета в режим прослушивания входящих соединений;
       listen_fd — дескриптор сокета;
       5         — размер очереди ожидающих подключений */
    if (listen(listen_fd, 5) == -1) {
        perror("listen");
        return 1;
    }
    printf("сервер: слушаю порт %d\n", SERVER_PORT);

    /* Создаём поток ожидания подключения */
    pthread_t tid_accept;

    /* pthread_create — создание потока accept */
    if (pthread_create(&tid_accept, NULL, thread_accept, NULL) != 0) {
        perror("pthread_create accept");
        return 1;
    }
    pthread_detach(tid_accept);

    printf("сервер: для завершения нажмите <Enter>\n");

    /* getchar — ожидание нажатия <Enter> */
    getchar();

    stop_flag = 1;
    if (client_fd  != -1) shutdown(client_fd,  SHUT_RDWR);
    if (listen_fd  != -1) shutdown(listen_fd,  SHUT_RDWR);

    sleep(1); /* даём потокам завершиться */

    /* close — закрытие дескрипторов сокетов */
    if (client_fd != -1) close(client_fd);
    close(listen_fd);

    printf("сервер завершил работу\n");
    return 0;
}
