/* Optionnal features */
#ifndef __WIN32__
    #ifndef ENABLE_FORK
        #ifndef DISABLE_FORK
            #define ENABLE_FORK
        #endif
    #endif
    #ifndef ENABLE_CHGUSER
        #ifndef DISABLE_CHGUSER
            #define ENABLE_CHGUSER
        #endif
    #endif
#else
    #ifdef ENABLE_FORK
        #warning ENABLE_FORK is not available on Windows
        #undef ENABLE_FORK
    #endif
    #ifdef ENABLE_CHGUSER
        #warning ENABLE_CHGUSER is not available on Windows
        #undef ENABLE_CHGUSER
    #endif
#endif

/* Configuration */
#ifndef MAX_PENDING_REQUESTS
    #define MAX_PENDING_REQUESTS 64
#endif

#ifndef RECV_BUFFER_SIZE
    #define RECV_BUFFER_SIZE 512
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h> // For rand and srand
#include "cache.h"
#ifdef __WIN32__
    #define _WIN32_WINNT 0x0501 /* needed for getaddrinfo(); means WinXP */
    #include <winsock2.h>
    #include <ws2tcpip.h>

    typedef int socklen_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/param.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <signal.h>
    #include <netdb.h>
    #include <errno.h>
    #include <unistd.h>

    typedef int SOCKET;
#endif
#ifdef __WIN32__
#include <windows.h&>
#else
#include <pthread.h>
#include <unistd.h> // For sleep
#endif

volatile sig_atomic_t shutdown_flag = 0;

void handle_signal(int signum) {
    shutdown_flag = 1;
}

#ifdef ENABLE_CHGUSER
    #include <pwd.h>
#endif

int setup_server(int *serv_sock, const char *addr, const char *port);
int serve(int serv_sock, const char *dest);

// Thread function to add IP to cache after a delay
#ifdef __WIN32__
DWORD WINAPI add_to_cache(LPVOID lpParam)
#else
void *add_to_cache(void *arg)
#endif
{
    char *ip = (char *)arg;

#ifdef __WIN32__
    Sleep(3000); // Sleep for 3 seconds (milliseconds)
#else
    sleep(2); // Sleep for 3 seconds
#endif

    // Add the IP to the cache with a dummy value and size
    // The cache_add function handles memory allocation for the key
    if (cache_add(ip, (void *)"1", 1) == 0) {
        fprintf(stderr, "Added %s to cache after delay\n", ip);
    } else {
        fprintf(stderr, "Failed to add %s to cache after delay\n", ip);
    }

#ifdef __WIN32__
    return 0;
#else
    return NULL;
#endif
}
void pack_array(void **array, size_t size)
{
    size_t src, dest = 0;
    for(src = 0; src < size; ++src)
        if(array[src] != NULL)
            array[dest++] = array[src];
    for(; dest < size; ++dest)
        array[dest] = NULL;
}

void my_closesocket(int sock)
{
#ifdef __WIN32__
    closesocket(sock);
#else
    shutdown(sock, SHUT_RDWR);
    close(sock);
#endif
}

void print_help(FILE *f)
{
    fprintf(
            f,
            "Usage: http-redirect [options] <destination>\n"
            "\n"
            "  Starts a very simple HTTP server that will always send back 307 "
            "redirects to\n"
            "the specified destination.\n"
            "  Example:\n"
            "    http-redirect -p 80 http://www.google.com/\n"
            "\n"
            "Recognized options:\n"
            "  -h, --help: print this message and exit\n"
#ifdef ENABLE_FORK
            "  -d, --daemon: use fork() to daemonize\n"
#endif
#ifdef ENABLE_CHGUSER
            "  -u, --user: change to user after binding the socket\n"
#endif
            "  -p, --port <port>: port on which to listen\n");
}

int main(int argc, char **argv)
{
    const char *bind_addr = NULL;
    const char *port = NULL;
    const char *dest = NULL;
#ifdef ENABLE_FORK
    int daemonize = 0;
#endif
#ifdef ENABLE_CHGUSER
    const char *user = NULL;
#endif

    (void)argc; /* unused */
    while(*(++argv) != NULL)
    {
        if(strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0)
        {
            print_help(stdout);
            return 0;
        }
        else if(strcmp(*argv, "-b") == 0 || strcmp(*argv, "--bind") == 0)
        {
            if(bind_addr != NULL)
            {
                fprintf(stderr, "Error: --bind was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --bind\n");
                return 1;
            }
            bind_addr = *argv;
        }
        else if(strcmp(*argv, "-p") == 0 || strcmp(*argv, "--port") == 0)
        {
            if(port != NULL)
            {
                fprintf(stderr, "Error: --port was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --port\n");
                return 1;
            }
            port = *argv;
        }
        else if(strcmp(*argv, "-d") == 0 || strcmp(*argv, "--daemon") == 0)
        {
#ifdef ENABLE_FORK
            daemonize = 1;
#else
            fprintf(stderr, "Error: --daemon is not available\n");
            return 1;
#endif
        }
        else if(strcmp(*argv, "-u") == 0 || strcmp(*argv, "--user") == 0)
        {
#ifdef ENABLE_CHGUSER
            if(user != NULL)
            {
                fprintf(stderr, "Error: --user was passed multiple times\n");
                return 1;
            }
            if(*(++argv) == NULL)
            {
                fprintf(stderr, "Error: missing argument for --user\n");
                return 1;
            }
            user = *argv;
#else
            fprintf(stderr, "Error: --user is not available\n");
            return 1;
#endif
        }
        else
        {
            if(dest != NULL)
            {
                fprintf(stderr, "Error: multiple destinations specified\n");
                return 1;
            }
            dest = *argv;
        }
    }

    if(port == NULL)
        port = "80";

    if(dest == NULL)
    {
        fprintf(stderr, "Error: no destination specified\n");
        return 1;
    }

#ifdef __WIN32__
    {
        /* Initializes WINSOCK */
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(1, 1), &wsa) != 0)
        {
            fprintf(stderr, "Error: can't initialize WINSOCK\n");
            return 3;
        }
    }
#endif
// Initialize cache with capacity 100 and TTL 30 seconds
    if (cache_init(100, 30) != 0) {
        fprintf(stderr, "Failed to initialize cache.\n");
        // Decide how to handle failure - for now, just print error and continue
    }

    {
        int serv_sock;

        /* Poor man's exception handling... */
        int ret = setup_server(&serv_sock, bind_addr, port);
        if(ret != 0)
            return ret;

#ifdef ENABLE_CHGUSER
        if(user != NULL)
        {
            struct passwd *pwd = getpwnam(user);
            if(pwd == NULL)
            {
                fprintf(stderr, "Error: user %s is unknown\n", user);
                return 2;
            }
            if(setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) == -1)
            {
                fprintf(stderr, "Error: can't change user to %s\n", user);
                return 2;
            }
        }
#endif

#ifdef ENABLE_FORK
        if(daemonize)
        {
            switch(fork())
            {
            case -1:
                perror("Error: fork() failed");
                break;
            case 0:
                /* child: go on... */
                fclose(stdin);
                fclose(stdout);
                //fclose(stderr);
                break;
            default:
                /* parent: exit with success */
                return 0;
                break;
            }
        }
#endif

        // Register signal handlers
        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);
#ifndef __WIN32__
        signal(SIGHUP, handle_signal); // Handle hangup signal on non-Windows
#endif

        ret = serve(serv_sock, dest);
        my_closesocket(serv_sock);
        cache_destroy();
        return ret;
    }
#ifdef __WIN32__
    WSACleanup(); // Clean up Winsock
#endif
}

int setup_server(int *serv_sock, const char *addr, const char *port)
{
    int ret;
    struct addrinfo hints, *results, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    ret = getaddrinfo(addr, port, &hints, &results);
    if(ret != 0)
    {
        fprintf(stderr, "Error: getaddrinfo failed: %s\n", gai_strerror(ret));
        return 1;
    }

    for(rp = results; rp != NULL; rp = rp->ai_next)
    {
        *serv_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(*serv_sock == -1)
            continue;

        if(bind(*serv_sock, rp->ai_addr, rp->ai_addrlen) != -1)
            break;

        my_closesocket(*serv_sock);
    }

    freeaddrinfo(results);

    if(rp == NULL)
    {
        fprintf(stderr, "Could not bind to %s:%s\n",
                ((addr == NULL)?"*":addr), port);
        *serv_sock = -1;
        return 2;
    }

    if(listen(*serv_sock, 5) == -1)
    {
        perror("Error: can't listen for incoming connections");
        return 2;
    }

    return 0;
}

char *build_success(size_t *response_size)
{
    const char *header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: 68\r\n"
                         "Server: httpredirect\r\n"
                         "\r\n";
    const char *body = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    size_t header_len = strlen(header);
    size_t body_len = strlen(body);
    *response_size = header_len + body_len;

    char *response = malloc(*response_size + 1);
    if (response == NULL) {
        *response_size = 0;
        return NULL;
    }

    strcpy(response, header);
    strcat(response, body);

    return response;
}

char *build_appleredirect(const char *dest, size_t *response_size)
{
    const char *pattern =
                "HTTP/1.1 307 Temporary Redirect\r\n"
                "Location: http://apple.%s/xxxxxx\r\n"
                "Content-Length: 0\r\n"
                "Server: httpredirect\r\n"
                "\r\n";
    char *response;
    *response_size = strlen(pattern) - 2 + strlen(dest);
    response = malloc(*response_size + 1);
    snprintf(response, *response_size + 1, pattern, dest);

    return response;
}

char *build_redirect(const char *dest, size_t *response_size)
{
    const char *pattern =
                "HTTP/1.1 307 Temporary Redirect\r\n"
                "Location: http://%s/xxxxxx\r\n"
                "Content-Length: 0\r\n"
                "Server: httpredirect\r\n"
                "\r\n";
    char *response;
    *response_size = strlen(pattern) - 2 + strlen(dest);
    response = malloc(*response_size + 1);
    snprintf(response, *response_size + 1, pattern, dest);

    return response;
}

struct Client {
    int sock;
    int state;
    bool is_apple_captive_portal; // true if the request is from an Apple captive portal
};

int serve(int serv_sock, const char *dest)
{
    size_t response_size;
    char *response_data = build_redirect(dest, &response_size);
    size_t apple_response_size;
    char *apple_response_data = build_appleredirect(dest, &apple_response_size);
    size_t success_response_size;
    char *success_response_data = build_success(&success_response_size);
    const char *apple_domain = "captive.apple.com";
    if(response_data == NULL || apple_response_data == NULL || success_response_data == NULL)
    {
        fprintf(stderr, "Error: failed to build response data\n");
        return 3;
    }
    /* find substr "xxxxxx" in response_data and apple_response_data, save position*/
    char *url = strstr(response_data, "xxxxxx");
    char *apple_url = strstr(apple_response_data, "xxxxxx");
    if(url == NULL || apple_url == NULL)
    {
        fprintf(stderr, "Error: failed to find 'xxxxxx' in response data\n");
        free(response_data);
        free(apple_response_data);
        free(success_response_data);
        return 3;
    }
    /* replace "xxxxxx" with the 6 random characters */
    char random_chars[7];
    random_chars[6] = '\0'; // Null-terminate the string
    
    struct Client *connections[MAX_PENDING_REQUESTS];
    size_t i;
    for(i = 0; i < MAX_PENDING_REQUESTS; ++i)
        connections[i] = NULL;

    while(!shutdown_flag) // Check shutdown_flag
    {
        int greatest;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET((SOCKET)serv_sock, &fds);
        greatest = serv_sock;

        for(i = 0; i < MAX_PENDING_REQUESTS && connections[i] != NULL; ++i)
        {
            int s = connections[i]->sock;
            FD_SET((SOCKET)s, &fds);
            if(s > greatest)
                greatest = s;
        }

        // Use select with a timeout or check shutdown_flag after select
        struct timeval tv;
        tv.tv_sec = 1; // Check flag every 1 second
        tv.tv_usec = 0;

        select(greatest + 1, &fds, NULL, NULL, &tv);

        if (shutdown_flag) {
            break; // Exit loop if shutdown_flag is set
        }

        if(FD_ISSET(serv_sock, &fds))
        {
            /* If all connections are taken */
            if(connections[MAX_PENDING_REQUESTS - 1] != NULL)
            {
                my_closesocket(connections[0]->sock);
                free(connections[0]);
                connections[0] = NULL;
                pack_array((void**)connections, MAX_PENDING_REQUESTS);
            }
            /* Accept the new connection */
            {
                struct sockaddr_in clientsin;
                socklen_t size = sizeof(clientsin);
                int sock = accept(serv_sock,
                                  (struct sockaddr*)&clientsin, &size);
                if(sock != -1)
                {
                    for(i = 0; i < MAX_PENDING_REQUESTS; ++i)
                        if(connections[i] == NULL)
                            break;
                    connections[i] = malloc(sizeof(struct Client));
                    connections[i]->sock = sock;
                    connections[i]->state = 0;
                    connections[i]->is_apple_captive_portal = false; // Initialize to false
                }
            }
        }
        else for(i = 0; i < MAX_PENDING_REQUESTS && connections[i] != NULL; ++i)
        {
            int s = connections[i]->sock;
            if(FD_ISSET(s, &fds))
            {
                int *const state = &connections[i]->state;
                int j;

                /* get remote ip addr*/
                struct sockaddr_in clientsin;
                socklen_t size = sizeof(clientsin);
                getpeername(s, (struct sockaddr*)&clientsin, &size);
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(clientsin.sin_addr), ip, INET_ADDRSTRLEN);
                fprintf(stderr,"Client connected from %s\n", ip);

                /* Read stuff */
                static char buffer[RECV_BUFFER_SIZE];
                int len = recv(s, buffer, RECV_BUFFER_SIZE, 0);

                /* dump buffer*/
/*
                for(j = 0; j < len; ++j)
                    fprintf(stdout, "%c", buffer[j]);
                fprintf(stdout,"\n");
*/
                /* scan buffer to Check if the whole http request data includes header "Host", and it's value is Apple captive portal */
                for(j = 0; j < len; ++j) {
                    if (buffer[j] == 'H' && buffer[j + 1] == 'o' && buffer[j + 2] == 's' && buffer[j + 3] == 't' && buffer[j + 4] == ':') {
                        j += 5; // Move past "Host: "
                        while (j < len && buffer[j] == ' ') j++; // Skip spaces
                        if (j < len) {
                            // Check if the host matches the Apple captive portal domain
                            if (strncmp(&buffer[j], apple_domain, strlen(apple_domain)) == 0) {
                                fprintf(stderr, "Detected Apple captive portal for IP: %s\n", ip);
                                connections[i]->is_apple_captive_portal = true;
                            }
                        }
                        break; // No need to check further once we found the Host header
                    }
                }

                for(j = 0; j < len; ++j)
                {
                    if(buffer[j] == '\r')
                    {
                        if(*state == 0 || *state == 2)
                            ++*state;
                        else
                            *state = 1;
                    }
                    else if(buffer[j] == '\n')
                    {
                        if(*state < 2)
                            *state = 2;
                        else
                        {
                            *state = 4;
                            break;
                        }
                    }
                    else
                        *state = 0;
                }
                if(*state == 4)
                {
                    /* 如果connections[i]->is_apple_captive_portal 为true，检查是否存在cache中key为ip地址*/
                    if (connections[i]->is_apple_captive_portal) {
                        size_t value_size;
                        void *value = cache_get(ip, &value_size); // 尝试获取缓存中的值
                        if (value != NULL) { // 如果缓存中存在值，直接返回缓存中的值
                            send(s, success_response_data, success_response_size, 0); // 发送success内容
                        } else {
                            // 如果缓存中不存在值，添加并行任务，3秒后添加到缓存中
                            pthread_t thread;
                            pthread_create(&thread, NULL, add_to_cache, (void *)ip);
                            // 设置线程为分离状态，这样线程结束后会自动释放资源
                            pthread_detach(thread);
                            srand(time(NULL)); // Seed the random number generator
                            int i;
                            for(i = 0; i < 6; ++i)
                                random_chars[i] = 'a' + rand() % 26; // Generate a random lowercase letter
                            memcpy(apple_url, random_chars, 6); // Replace "xxxxxx" with random characters
                            send(s, apple_response_data, apple_response_size, 0);
                        }
                    }
                    else {
                        /* Print redirect */
                        srand(time(NULL)); // Seed the random number generator
                        int i;
                        for(i = 0; i < 6; ++i)
                            random_chars[i] = 'a' + rand() % 26; // Generate a random lowercase letter
                        memcpy(url, random_chars, 6); // Replace "xxxxxx" with random characters
                        send(s, response_data, response_size, 0);
                    }
                }

                /* Client closed the connection OR request complete */
                if(len <= 0 || *state == 4)
                {
                    my_closesocket(s);
                    free(connections[i]);
                    connections[i] = NULL;
                    pack_array((void**)connections, MAX_PENDING_REQUESTS);
                }
            }
        }
    }

    // Cleanup connections before exiting
    for(i = 0; i < MAX_PENDING_REQUESTS; ++i) {
        if (connections[i] != NULL) {
            my_closesocket(connections[i]->sock);
            free(connections[i]);
            connections[i] = NULL;
        }
    }

    free(response_data);
    free(success_response_data);
    free(apple_response_data); // Free apple_response_data
    fprintf(stderr, "Exiting serve loop\n");
    return 0;
}
