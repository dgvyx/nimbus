#include <stdio.h>
#include "../kernel/http.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            return 1; \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while (0)

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int test_url_basics(void)
{
    char host[64], path[200];
    u16 port;
    TEST_ASSERT(http_parse_url("http://10.0.2.2:8000/", host, sizeof(host),
                               &port, path, sizeof(path)), "URL com IP e porta");
    TEST_ASSERT(same(host, "10.0.2.2"), "host do IP");
    TEST_ASSERT(port == 8000, "porta 8000");
    TEST_ASSERT(same(path, "/"), "caminho /");

    TEST_ASSERT(http_parse_url("http://example.com", host, sizeof(host),
                               &port, path, sizeof(path)), "URL sem caminho");
    TEST_ASSERT(same(host, "example.com"), "host DNS");
    TEST_ASSERT(port == 80, "porta padrao 80");
    TEST_ASSERT(same(path, "/"), "caminho padrao /");

    TEST_ASSERT(http_parse_url("HTTP://Example.COM:8080/a/b?x=1#frag",
                               host, sizeof(host), &port, path, sizeof(path)),
                "URL maiuscula com query e fragmento");
    TEST_ASSERT(same(host, "Example.COM"), "host preserva caixa");
    TEST_ASSERT(port == 8080, "porta 8080");
    TEST_ASSERT(same(path, "/a/b?x=1"), "fragmento removido");

    TEST_ASSERT(http_parse_url("http://example.com?q=1", host, sizeof(host),
                               &port, path, sizeof(path)), "URL so com query");
    TEST_ASSERT(same(path, "/?q=1"), "query vira /?q=1");

    TEST_ASSERT(http_parse_url("http://example.com#top", host, sizeof(host),
                               &port, path, sizeof(path)), "URL so com fragmento");
    TEST_ASSERT(same(path, "/"), "fragmento sem caminho usa /");
    return 0;
}

static int test_url_errors(void)
{
    char host[8], path[8];
    u16 port;
    TEST_ASSERT(!http_parse_url("https://example.com/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita https");
    TEST_ASSERT(!http_parse_url("http://", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita host vazio");
    TEST_ASSERT(!http_parse_url("http://:8000/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita host vazio com porta");
    TEST_ASSERT(!http_parse_url("http://host:0/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita porta 0");
    TEST_ASSERT(!http_parse_url("http://host:70000/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita porta > 65535");
    TEST_ASSERT(!http_parse_url("http://host:abc/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita porta nao numerica");
    TEST_ASSERT(!http_parse_url("http://host:/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita porta vazia");
    TEST_ASSERT(!http_parse_url("http://[::1]/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita IPv6");
    TEST_ASSERT(!http_parse_url("http://host/a b", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita espaco no caminho");
    TEST_ASSERT(!http_parse_url("http://verylonghostname/", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita host que nao cabe");
    TEST_ASSERT(!http_parse_url("http://host/12345678", host, sizeof(host),
                                &port, path, sizeof(path)), "rejeita caminho que nao cabe");
    return 0;
}

static int test_ipv4(void)
{
    u32 ip;
    TEST_ASSERT(http_parse_ipv4("10.0.2.2", &ip) == 1, "IPv4 valido");
    TEST_ASSERT(ip == 0x0a000202u, "IPv4 valor numerico");
    TEST_ASSERT(http_parse_ipv4("255.255.255.255", &ip) == 1, "IPv4 limite");
    TEST_ASSERT(ip == 0xffffffffu, "IPv4 limite numerico");
    TEST_ASSERT(http_parse_ipv4("example.com", &ip) == 0, "nome DNS");
    TEST_ASSERT(http_parse_ipv4("999.1.1.1", &ip) == -1, "IPv4 com octeto invalido");
    TEST_ASSERT(http_parse_ipv4("1.2.3", &ip) == -1, "IPv4 com 3 partes");
    TEST_ASSERT(http_parse_ipv4("1.2.3.4.5", &ip) == -1, "IPv4 com 5 partes");
    TEST_ASSERT(http_parse_ipv4("1..2.3", &ip) == -1, "IPv4 com parte vazia");
    return 0;
}

static int test_body(void)
{
    static const u8 crlf[] = "HTTP/1.0 200 OK\r\nX: y\r\n\r\n<html>x</html>";
    static const u8 lf[] = "HTTP/1.0 200 OK\nX: y\n\nBODY";
    static const u8 partial[] = "HTTP/1.0 200 OK\r\nX: y\r\n";
    TEST_ASSERT(http_find_body(crlf, sizeof(crlf) - 1) == 25, "corpo apos CRLFCRLF");
    TEST_ASSERT(same((const char *)crlf + http_find_body(crlf, sizeof(crlf) - 1),
                     "<html>x</html>"), "conteudo do corpo CRLF");
    TEST_ASSERT(http_find_body(lf, sizeof(lf) - 1) == 22, "corpo apos LFLF");
    TEST_ASSERT(same((const char *)lf + http_find_body(lf, sizeof(lf) - 1), "BODY"),
                "conteudo do corpo LF");
    TEST_ASSERT(http_find_body(partial, sizeof(partial) - 1) == -1, "cabecalho incompleto");
    return 0;
}

static int test_status(void)
{
    static const u8 ok[] = "HTTP/1.0 200 OK\r\n\r\n";
    static const u8 redirect[] = "HTTP/1.1 302 Found\r\nLocation: http://x/\r\n\r\n";
    static const u8 html[] = "<html>200</html>";
    TEST_ASSERT(http_parse_status(ok, sizeof(ok) - 1) == 200, "status 200");
    TEST_ASSERT(http_parse_status(redirect, sizeof(redirect) - 1) == 302, "status 302");
    TEST_ASSERT(http_parse_status(html, sizeof(html) - 1) == 0, "HTML nao e HTTP");
    TEST_ASSERT(http_parse_status((const u8 *)"HTTP/1.0 20", 12) == 0, "status truncado");
    return 0;
}

static int test_redirect(void)
{
    TEST_ASSERT(http_redirect_http("http://example.com/x") == 1, "Location http");
    TEST_ASSERT(http_redirect_http("HTTP://example.com/x") == 1, "Location HTTP maiusculo");
    TEST_ASSERT(http_redirect_http("https://example.com/x") == 0, "Location https");
    TEST_ASSERT(http_redirect_http("/relativo") == 0, "Location relativo");
    TEST_ASSERT(http_redirect_http(0) == 0, "Location nulo");
    return 0;
}

static int test_request(void)
{
    char out[512];
    int n = http_build_request("10.0.2.2", 8000, "/", out, sizeof(out));
    TEST_ASSERT(n == 58, "tamanho do request com porta");
    TEST_ASSERT(same(out, "GET / HTTP/1.0\r\nHost: 10.0.2.2:8000\r\n"
                         "Connection: close\r\n\r\n"), "request com porta");

    n = http_build_request("example.com", 80, "/a?b=1", out, sizeof(out));
    TEST_ASSERT(n > 0, "request sem porta");
    TEST_ASSERT(same(out, "GET /a?b=1 HTTP/1.0\r\nHost: example.com\r\n"
                         "Connection: close\r\n\r\n"), "Host sem porta 80");

    TEST_ASSERT(http_build_request("host", 80, "/", out, 20) == 0,
                "buffer pequeno falha");
    return 0;
}

int main(void)
{
    if (test_url_basics()) return 1;
    if (test_url_errors()) return 1;
    if (test_ipv4()) return 1;
    if (test_body()) return 1;
    if (test_status()) return 1;
    if (test_redirect()) return 1;
    if (test_request()) return 1;
    printf("http parser: todos os testes passaram\n");
    return 0;
}
