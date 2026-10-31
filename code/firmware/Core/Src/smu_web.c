/**
 * @file   smu_web.c
 * @brief  内置 HTTP 服务（LwIP netconn），路由见 PRD §6.3
 * @note   - /          页面（smu_webpage.h，由 tools/web2c.py 生成）
 *         - /data.json 实时数据
 *         - /set       设置（参数合成为命令后走 SMU_CmdExecute，保证与串口校验一致）
 *         - /cmd       透传一条命令（POST body）
 *         每个连接处理完即关闭（HTTP/1.0），顺序接受，逻辑简单可靠。
 */
#include "smu_web.h"
#include "smu_board.h"
#include "smu_cmd.h"
#include "smu_cal.h"
#include "smu_types.h"
#include "smu_ver.h"
#include "smu_webpage.h"
#include "smu_fmt.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

extern smu_state_t g_smu;

#define WEB_PORT        80U
#define WEB_RECV_MAX    1536U
#define WEB_CHUNK       1024U

static const char *mode_str(smu_mode_t m)
{
    switch (m) {
    case SMU_MODE_CV:  return "CV";
    case SMU_MODE_CC:  return "CC";
    case SMU_MODE_VM:  return "VM";
    case SMU_MODE_IM:  return "IM";
    default:           return "HIZ";
    }
}

static void http_send(struct netconn *conn, const char *head, const char *ctype,
                      const char *body, uint32_t body_len)
{
    char hdr[192];
    char *p = hdr;
    p = smu_cat(p, "HTTP/1.0 200 OK\r\nContent-Type: ");
    p = smu_cat(p, ctype);
    p = smu_cat(p, "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
    p = smu_cat_uint(p, (unsigned long)body_len);
    p = smu_cat(p, "\r\n\r\n");
    netconn_write(conn, hdr, (uint16_t)(p - hdr), NETCONN_COPY);
    for (uint32_t off = 0; off < body_len; off += WEB_CHUNK) {
        uint32_t len = body_len - off;
        if (len > WEB_CHUNK) len = WEB_CHUNK;
        netconn_write(conn, body + off, (uint16_t)len, NETCONN_COPY);
    }
}

static void send_json(struct netconn *conn)
{
    char body[384];
    char *p = body;
    ip4_addr_t *ip = netif_ip4_addr(netif_default);
    const uint8_t *mac = netif_default->hwaddr;

    p = smu_cat(p, "{\"mode\":\"");
    p = smu_cat(p, mode_str(g_smu.mode));
    p = smu_cat(p, "\",\"vset\":");
    p = smu_cat_fixed(p, g_smu.vset, 3);
    p = smu_cat(p, ",\"iset\":");
    p = smu_cat_fixed(p, g_smu.iset, 3);
    p = smu_cat(p, ",\"vmeas\":");
    p = smu_cat_fixed(p, g_smu.vmeas, 4);
    p = smu_cat(p, ",\"imeas\":");
    p = smu_cat_fixed(p, g_smu.imeas, 4);
    p = smu_cat(p, ",\"vprot\":");
    p = smu_cat_fixed(p, g_smu.vprot, 1);
    p = smu_cat(p, ",\"iprot\":");
    p = smu_cat_fixed(p, g_smu.iprot, 1);
    /* 暴露真实量程（smu_board.h 的 SMU_V_MAX / SMU_I_MAX_MA），前端滑块据此设上限，
     * 避免前端写死 30V 而固件已限制为 15V（母线16V→≤~15V）时滑块与固件不一致。 */
    p = smu_cat(p, ",\"vmax\":");
    p = smu_cat_fixed(p, SMU_V_MAX, 1);
    p = smu_cat(p, ",\"imax\":");
    p = smu_cat_fixed(p, SMU_I_MAX_MA, 1);
    p = smu_cat(p, ",\"outp\":");
    p = smu_cat_uint(p, (unsigned long)g_smu.outp);
    p = smu_cat(p, ",\"stat\":");
    p = smu_cat_uint(p, (unsigned long)g_smu.stat);
    p = smu_cat(p, ",\"err\":");
    p = smu_cat_int(p, (long)g_smu.err);
    p = smu_cat(p, ",\"j18\":");
    p = smu_cat_uint(p, (unsigned long)g_smu.j18);
    p = smu_cat(p, ",\"fw\":\"");
    p = smu_cat(p, SMU_FW_VERSION);
    p = smu_cat(p, "\",\"ip\":\"");
    p = smu_cat_ip(p, (const unsigned char *)ip);   /* ip4_addr 内存布局即 4 字节 */
    p = smu_cat(p, "\",\"mac\":\"");
    p = smu_cat_mac(p, mac);
    p = smu_cat(p, "\"}");
    *p = '\0';
    http_send(conn, NULL, "application/json", body, (uint32_t)(p - body));
}

/* URL 解码 + 取参数值（仅支持简单 %XX 与 +） */
static int param_get(const char *qs, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = qs;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', seglen);
        if (eq && (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            const char *v = eq + 1;
            size_t vlen = seglen - klen - 1U, o = 0;
            for (size_t i = 0; i < vlen && o + 1 < out_len; i++) {
                char c = v[i];
                if (c == '+') c = ' ';
                else if (c == '%' && i + 2 < vlen) {
                    char hex[3] = { v[i+1], v[i+2], 0 };
                    c = (char)strtol(hex, NULL, 16);
                    i += 2;
                }
                out[o++] = c;
            }
            out[o] = '\0';
            return 0;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return -1;
}

/* 把 /set 的参数合成为命令，逐条走命令层（保证校验一致，FR-WEB-4/6） */
static void handle_set(struct netconn *conn, const char *qs)
{
    char v[32], resp[64], last[64] = "OK";
    int rc = 0, any = 0;
    /* 注意：前端网页用 vset/iset 作为电压/电流的设置键（见 smu_webpage.h 中
     * bindPair('vSet','vSlide','vset',...)），而早期后端只认 volt/curr，导致
     * 网页设电压/电流时参数被忽略、走到 any=0 返回 -100("设置失败-100")。
     * 这里两种键名都接受，避免前后端键名不一致。 */
    struct { const char *key; const char *cmd; } map[] = {
        { "mode",  "MODE"  }, { "volt", "VOLT" }, { "vset", "VOLT" },
        { "curr", "CURR" }, { "iset", "CURR" },
        { "vprot", "VOLT:PROT" }, { "iprot", "CURR:PROT" },
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (param_get(qs, map[i].key, v, sizeof(v)) == 0 && v[0]) {
            char line[80];
            char *lp = line;
            lp = smu_cat(lp, map[i].cmd);
            lp = smu_cat_char(lp, ' ');
            lp = smu_cat(lp, v);
            *lp = '\0';
            SMU_CmdExecute(line, resp, sizeof(resp));
            strncpy(last, resp, sizeof(last) - 1); last[sizeof(last)-1] = '\0';
            any = 1;
        }
    }
    if (param_get(qs, "outp", v, sizeof(v)) == 0 && v[0]) {
        char line[32];
        char *lp = line;
        lp = smu_cat(lp, "OUTP ");
        lp = smu_cat(lp, (strcmp(v, "1") == 0) ? "ON" : "OFF");
        *lp = '\0';
        SMU_CmdExecute(line, resp, sizeof(resp));
        strncpy(last, resp, sizeof(last) - 1); last[sizeof(last)-1] = '\0';
        any = 1;
    }
    if (!any) { http_send(conn, NULL, "text/plain", "-100,\"Command error\"\n", 21); return; }
    (void)rc;
    size_t len = strlen(last);
    http_send(conn, NULL, "text/plain", last, (uint32_t)len);
}

static void handle_cmd(struct netconn *conn, const char *body)
{
    char resp[96];
    while (*body == ' ' || *body == '\r' || *body == '\n') body++;
    if (*body == '\0') { http_send(conn, NULL, "text/plain", "-100,\"Command error\"\n", 21); return; }
    SMU_CmdExecute(body, resp, sizeof(resp));
    http_send(conn, NULL, "text/plain", resp, (uint32_t)strlen(resp));
}

static void web_thread(void *arg)
{
    (void)arg;
    struct netconn *listen;
    listen = netconn_new(NETCONN_TCP);
    if (listen == NULL) vTaskDelete(NULL);
    if (netconn_bind(listen, IP_ADDR_ANY, WEB_PORT) != ERR_OK) vTaskDelete(NULL);
    netconn_listen(listen);

    for (;;) {
        struct netconn *conn;
        if (netconn_accept(listen, &conn) != ERR_OK) continue;
        netconn_set_recvtimeout(conn, 500);

        struct netbuf *nb = NULL;
        char req[WEB_RECV_MAX];
        uint32_t total = 0;
        while (total + 1U < sizeof(req)) {
            struct netbuf *n;
            if (netconn_recv(conn, &n) != ERR_OK) break;
            if (nb == NULL) nb = n;
            else { netbuf_chain(nb, n); }
            uint16_t len = netbuf_len(nb);
            if (len > sizeof(req) - 1U) len = (uint16_t)(sizeof(req) - 1U);
            netbuf_copy(nb, req, len);
            total = len;
            break;                                   /* 请求头一次读够 */
        }
        req[total] = '\0';

        if (total > 0) {
            char method[8] = {0}, path[128] = {0};
            if (sscanf(req, "%7s %127s", method, path) >= 2) {
                char *qs = strchr(path, '?');
                if (qs) *qs++ = '\0';
                if (strcmp(method, "GET") == 0 && strcmp(path, "/data.json") == 0) {
                    send_json(conn);
                } else if (strcmp(path, "/set") == 0 && qs) {
                    handle_set(conn, qs);
                } else if (strcmp(path, "/cmd") == 0) {
                    char *body = strstr(req, "\r\n\r\n");
                    handle_cmd(conn, body ? body + 4 : "");
                } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
                    http_send(conn, NULL, "text/html; charset=utf-8",
                              (const char *)smu_webpage, SMU_WEBPAGE_LEN);
                } else {
                    http_send(conn, NULL, "text/plain", "404 Not Found\n", 14);
                }
            }
        }
        if (nb) netbuf_delete(nb);
        netconn_close(conn);
        netconn_delete(conn);
    }
}

void SMU_WebInit(void)
{
    xTaskCreate(web_thread, "web", 1024, NULL, 3, NULL);
}
