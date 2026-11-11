#ifndef HTTP_H
#define HTTP_H
#include "nimbus.h"

/* Resposta maxima acumulada (cabecalho + corpo). */
#define HTTP_MAX_RESPONSE (256 * 1024)
#define HTTP_MAX_REDIRECTS 3

typedef struct HttpReq HttpReq;

/* Inicia uma requisicao GET. cb(status, body, len, arg) e chamado ao final;
 * status 0 indica falha de transporte e http_last_error() traz o motivo. */
HttpReq *http_get(const char *url, void (*cb)(int status, const u8 *body,
                  int len, void *arg), void *arg);
void http_poll(void);
void http_cancel(void);
int http_busy(void);
const char *http_last_error(void);

/* Funcoes puras, usadas tambem pelos testes no PC. */
int http_parse_url(const char *url, char *host, int host_size, u16 *port,
                   char *path, int path_size);
int http_parse_ipv4(const char *text, u32 *ip);   /* 1=IPv4, 0=nome, -1=invalido */
int http_find_body(const u8 *data, int len);      /* offset apos o cabecalho, -1 se incompleto */
int http_parse_status(const u8 *data, int len);   /* codigo HTTP ou 0 */
int http_redirect_http(const char *location);     /* 1 se Location pode ser seguido */
int http_build_request(const char *host, u16 port, const char *path,
                       char *out, int cap);

#endif
