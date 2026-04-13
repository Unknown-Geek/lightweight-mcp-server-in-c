#include <config.h>
#include "mcp_server.h"
#include "util.h"
#include "openvswitch/poll-loop.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "bridge.h"
#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/vlog.h"

#define PORT 8080
#define MCP_MAX_REQUEST (64 * 1024)

VLOG_DEFINE_THIS_MODULE(mcp_server);

static int server_fd = -1;

static bool
mcp_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool
mcp_parse_content_length(const char *buf, size_t header_len, size_t *lenp)
{
    const char *p = buf;
    const char *headers_end = buf + header_len;
    bool found = false;
    size_t length = 0;

    while (p < headers_end) {
        const char *eol = strstr(p, "\r\n");
        if (!eol || eol > headers_end) {
            break;
        }
        if (eol == p) {
            break;
        }

        if (!strncasecmp(p, "Content-Length:", 15)) {
            const char *v = p + 15;
            char *tail = NULL;
            unsigned long long ull;

            while (v < eol && (*v == ' ' || *v == '\t')) {
                v++;
            }

            errno = 0;
            ull = strtoull(v, &tail, 10);
            if (errno || tail == v) {
                return false;
            }

            while (tail < eol && (*tail == ' ' || *tail == '\t')) {
                tail++;
            }
            if (tail != eol) {
                return false;
            }

            length = (size_t) ull;
            found = true;
            break;
        }

        p = eol + 2;
    }

    *lenp = found ? length : 0;
    return true;
}

static ssize_t
mcp_write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t) n;
    }
    return (ssize_t) off;
}

static void
mcp_send_http_json(int fd, int status, const char *status_text, struct json *json)
{
    char *body = json_to_string(json, JSSF_SORT);
    char header[256];
    int n = snprintf(header, sizeof header,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %"PRIuSIZE"\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, status_text, strlen(body));

    if (n > 0) {
        mcp_write_all(fd, header, (size_t) n);
        mcp_write_all(fd, body, strlen(body));
    }
    free(body);
}

static const struct json *
mcp_json_get(const struct json *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) {
        return NULL;
    }
    return shash_find_data(json_object(obj), key);
}

static struct json *
mcp_make_error(const char *code, const char *message, const struct json *id)
{
    struct json *resp = json_object_create();
    struct json *err = json_object_create();

    json_object_put(resp, "ok", json_boolean_create(false));
    json_object_put_string(err, "code", code);
    json_object_put_string(err, "message", message);
    json_object_put(resp, "error", err);
    if (id) {
        json_object_put(resp, "id", json_clone(id));
    }
    return resp;
}

static struct json *
mcp_make_success(struct json *result, const struct json *id)
{
    struct json *resp = json_object_create();
    json_object_put(resp, "ok", json_boolean_create(true));
    json_object_put(resp, "result", result);
    if (id) {
        json_object_put(resp, "id", json_clone(id));
    }
    return resp;
}

static bool
mcp_dispatch(const char *tool, const struct json *arguments,
             struct json **resultp, char **errorp)
{
    if (!strcmp(tool, "switch.get_ports")) {
        return bridge_mcp_get_ports(arguments, resultp, errorp);
    } else if (!strcmp(tool, "switch.get_flows")) {
        return bridge_mcp_get_flows(arguments, resultp, errorp);
    } else if (!strcmp(tool, "switch.get_port_stats")) {
        return bridge_mcp_get_port_stats(arguments, resultp, errorp);
    }

    *errorp = xasprintf("unsupported tool: %s", tool);
    return false;
}

void mcp_server_init(void)
{
    struct sockaddr_in addr;
    int flags;
    int yes = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        VLOG_ERR("mcp: socket failed: %s", ovs_strerror(errno));
        return;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &yes, sizeof yes) < 0) {
        VLOG_WARN("mcp: setsockopt(SO_REUSEADDR) failed: %s",
                  ovs_strerror(errno));
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *) &addr, sizeof addr) < 0) {
        VLOG_ERR("mcp: bind failed: %s", ovs_strerror(errno));
        goto error;
    }

    if (listen(server_fd, 16) < 0) {
        VLOG_ERR("mcp: listen failed: %s", ovs_strerror(errno));
        goto error;
    }

    flags = fcntl(server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        VLOG_ERR("mcp: failed to set non-blocking listener: %s",
                 ovs_strerror(errno));
        goto error;
    }

    VLOG_INFO("mcp: server listening on port %d", PORT);
    return;

error:
    close(server_fd);
    server_fd = -1;
}

void
mcp_server_run(void)
{
    int client_fd;
    char buffer[MCP_MAX_REQUEST + 1];
    char method[10], path[100];
    size_t used = 0;
    char *header_end;
    size_t header_len;
    size_t content_len;
    char *body;

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            VLOG_WARN("mcp: accept failed: %s", ovs_strerror(errno));
        }
        return;
    }

    if (!mcp_set_nonblocking(client_fd)) {
        VLOG_WARN("mcp: failed to set client socket non-blocking: %s",
                  ovs_strerror(errno));
        close(client_fd);
        return;
    }

    for (;;) {
        ssize_t n;

        if (used == MCP_MAX_REQUEST) {
            struct json *err = mcp_make_error("request_too_large",
                                              "request exceeds max size",
                                              NULL);
            mcp_send_http_json(client_fd, 413, "Payload Too Large", err);
            json_destroy(err);
            close(client_fd);
            return;
        }

        n = read(client_fd, buffer + used, MCP_MAX_REQUEST - used);
        if (n > 0) {
            used += (size_t) n;
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        VLOG_WARN("mcp: read failed: %s", ovs_strerror(errno));
        close(client_fd);
        return;
    }

    if (!used) {
        close(client_fd);
        return;
    }
    buffer[used] = '\0';

    if (sscanf(buffer, "%9s %99s", method, path) != 2) {
        struct json *err = mcp_make_error("bad_request",
                                          "invalid HTTP request line",
                                          NULL);
        mcp_send_http_json(client_fd, 400, "Bad Request", err);
        json_destroy(err);
        close(client_fd);
        return;
    }

    if (strcmp(method, "POST") || strcmp(path, "/mcp")) {
        struct json *err = mcp_make_error("not_found",
                                          "endpoint not found",
                                          NULL);
        mcp_send_http_json(client_fd, 404, "Not Found", err);
        json_destroy(err);
        close(client_fd);
        return;
    }

    header_end = strstr(buffer, "\r\n\r\n");
    if (!header_end) {
        struct json *err = mcp_make_error("bad_request",
                                          "incomplete HTTP headers",
                                          NULL);
        mcp_send_http_json(client_fd, 400, "Bad Request", err);
        json_destroy(err);
        close(client_fd);
        return;
    }

    header_len = (size_t) (header_end + 4 - buffer);
    if (!mcp_parse_content_length(buffer, header_len, &content_len)) {
        struct json *err = mcp_make_error("bad_request",
                                          "invalid Content-Length",
                                          NULL);
        mcp_send_http_json(client_fd, 400, "Bad Request", err);
        json_destroy(err);
        close(client_fd);
        return;
    }

    if (header_len + content_len > used) {
        struct json *err = mcp_make_error("bad_request",
                                          "incomplete HTTP body",
                                          NULL);
        mcp_send_http_json(client_fd, 400, "Bad Request", err);
        json_destroy(err);
        close(client_fd);
        return;
    }

    body = header_end + 4;

    {
        char saved = body[content_len];
        struct json *req;
        const struct json *id;
        const struct json *tool_j;
        const struct json *args_j;
        struct json *result = NULL;
        char *tool_error = NULL;

        body[content_len] = '\0';
        req = json_from_string(body);
        body[content_len] = saved;

        if (req->type == JSON_STRING) {
            struct json *err = mcp_make_error("bad_json", json_string(req), NULL);
            mcp_send_http_json(client_fd, 400, "Bad Request", err);
            json_destroy(err);
            json_destroy(req);
            close(client_fd);
            return;
        }

        id = mcp_json_get(req, "id");
        tool_j = mcp_json_get(req, "tool");
        args_j = mcp_json_get(req, "arguments");

        if (!tool_j || tool_j->type != JSON_STRING) {
            struct json *err = mcp_make_error("bad_request",
                                              "field tool must be string",
                                              id);
            mcp_send_http_json(client_fd, 200, "OK", err);
            json_destroy(err);
            json_destroy(req);
            close(client_fd);
            return;
        }

        if (!args_j) {
            args_j = json_object_create();
        } else if (args_j->type != JSON_OBJECT) {
            struct json *err = mcp_make_error("bad_request",
                                              "field arguments must be object",
                                              id);
            mcp_send_http_json(client_fd, 200, "OK", err);
            json_destroy(err);
            json_destroy(req);
            close(client_fd);
            return;
        }

        if (mcp_dispatch(json_string(tool_j), args_j, &result, &tool_error)) {
            struct json *ok = mcp_make_success(result, id);
            mcp_send_http_json(client_fd, 200, "OK", ok);
            json_destroy(ok);
        } else {
            struct json *err = mcp_make_error(
                "tool_error",
                tool_error ? tool_error : "unknown tool error",
                id);
            mcp_send_http_json(client_fd, 200, "OK", err);
            json_destroy(err);
            free(tool_error);
        }

        if (!mcp_json_get(req, "arguments") && args_j) {
            json_destroy((struct json *) args_j);
        }
        json_destroy(req);
    }

    close(client_fd);
}

void mcp_server_wait(void)
{
    if (server_fd >= 0) {
        poll_fd_wait(server_fd, POLLIN);
    }
}

void mcp_server_close(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        VLOG_INFO("mcp: server stopped");
    }
}