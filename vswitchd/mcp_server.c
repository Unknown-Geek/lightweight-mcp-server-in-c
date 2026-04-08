// mcp_server.c

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 8080

static int server_fd = -1;

void mcp_server_init() {
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    // 🔥 make non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    printf("MCP server started on port %d\n", PORT);
}

void mcp_server_run() {
    int client_fd;
    char buffer[1024];

    client_fd = accept(server_fd, NULL, NULL);

    if (client_fd < 0) {
        return;
    }

    int n = read(client_fd, buffer, sizeof(buffer)-1);
    if (n <= 0) {
        close(client_fd);
        return;
    }

    buffer[n] = '\0';

    printf("MCP Request:\n%s\n", buffer);

    char method[10], path[100];
    sscanf(buffer, "%s %s", method, path);

    printf("Method: %s, Path: %s\n", method, path);

    char *response;

    if (strcmp(method, "POST") == 0 && strcmp(path, "/mcp") == 0) {
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "\r\n"
            "{\"status\": \"ok\"}";
    } else {
        response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "{\"status\": \"Not Found\"}";
    }

    write(client_fd, response, strlen(response));
    close(client_fd);
}

void mcp_server_close(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        printf("MCP server stopped\n");
    }
}