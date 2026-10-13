#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psputility.h>
#include <psputility_netparam.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspsdk.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

PSP_MODULE_INFO("PSP AI Terminal Chat", 0, 0, 3);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
PSP_HEAP_SIZE_KB(-2048);

#define RELAY_HOST "192.168.4.1"
#define RELAY_PORT 8088
#define MAX_PROFILES 10
#define MAX_QUESTION 180
#define HTTP_BUFFER 5120
#define ANSWER_COLS 54
#define ANSWER_PAGE_LINES 16
#define ANSWER_LINES 120

static volatile int running = 1;

static int selected_profile = -1;
static int modules_common_loaded = 0;
static int modules_inet_loaded = 0;
static int net_initialized = 0;
static int inet_initialized = 0;
static int apctl_initialized = 0;
static int online = 0;
static int ap_state = 0;
static int last_error = 0;

static char selected_name[128] = "";
static char ip_addr[16] = "0.0.0.0";

static FILE *logf = NULL;

typedef struct {
    int id;
    char name[128];
    char ssid[128];
} Profile;

static void log_open(void)
{
    logf = fopen("ms0:/PSPAI_NET.LOG", "a");
    if (logf) {
        fprintf(logf, "\n===== PSP AI Terminal v0.3.1 Stability =====\n");
        fflush(logf);
    }
}

static void log_line(const char *text, int value)
{
    if (!logf) return;
    fprintf(logf, "%s: 0x%08X (%d)\n", text, value, value);
    fflush(logf);
}

static void log_text(const char *text)
{
    if (!logf) return;
    fprintf(logf, "%s\n", text);
    fflush(logf);
}

static int exit_callback(int a, int b, void *c)
{
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    SceUID th = sceKernelCreateThread(
        "callback_thread", callback_thread, 0x11, 0xFA0,
        PSP_THREAD_ATTR_USER, NULL
    );
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

static int load_profiles(Profile *items, int max_items)
{
    int count = 0;

    for (int id = 1; id <= MAX_PROFILES && count < max_items; ++id) {
        int chk = sceUtilityCheckNetParam(id);
        if (chk != 0) continue;

        netData name, ssid;
        memset(&name, 0, sizeof(name));
        memset(&ssid, 0, sizeof(ssid));

        int r1 = sceUtilityGetNetParam(id, PSP_NETPARAM_NAME, &name);
        int r2 = sceUtilityGetNetParam(id, PSP_NETPARAM_SSID, &ssid);

        if (r1 != 0 || r2 != 0) continue;

        items[count].id = id;
        strncpy(items[count].name, name.asString, 127);
        strncpy(items[count].ssid, ssid.asString, 127);
        items[count].name[127] = 0;
        items[count].ssid[127] = 0;
        count++;
    }

    return count;
}

static int choose_profile(void)
{
    Profile items[MAX_PROFILES];
    memset(items, 0, sizeof(items));

    int count = load_profiles(items, MAX_PROFILES);

    if (count <= 0) {
        pspDebugScreenClear();
        pspDebugScreenPrintf("No saved Wi-Fi profiles found.\n");
        pspDebugScreenPrintf("Create/test one in XMB first.\n");
        pspDebugScreenPrintf("\nPress O to exit.\n");

        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));

        while (running) {
            sceCtrlReadBufferPositive(&pad, 1);
            if (pad.Buttons & PSP_CTRL_CIRCLE) break;
            sceKernelDelayThread(100000);
        }
        return -1;
    }

    int sel = 0;
    SceCtrlData pad = {0}, old = {0};

    while (running) {
        pspDebugScreenClear();
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenPrintf("PSP AI Terminal v0.3.1 Stability\n");
        pspDebugScreenPrintf("----------------------------------------\n");
        pspDebugScreenPrintf("Choose the profile that passes XMB test.\n\n");

        for (int i = 0; i < count; ++i) {
            pspDebugScreenPrintf(
                "%c #%d %-18.18s SSID: %.18s\n",
                i == sel ? '>' : ' ',
                items[i].id,
                items[i].name[0] ? items[i].name : "(unnamed)",
                items[i].ssid[0] ? items[i].ssid : "(unknown)"
            );
        }

        pspDebugScreenPrintf("\nUP/DOWN select   X diagnose   O exit\n");

        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~old.Buttons;

        if (pressed & PSP_CTRL_UP)
            sel = (sel - 1 + count) % count;

        if (pressed & PSP_CTRL_DOWN)
            sel = (sel + 1) % count;

        if (pressed & PSP_CTRL_CROSS) {
            strncpy(selected_name, items[sel].name, 127);
            selected_name[127] = 0;
            return items[sel].id;
        }

        if (pressed & PSP_CTRL_CIRCLE)
            return -1;

        old = pad;
        sceDisplayWaitVblankStart();
    }

    return -1;
}

static void wait_on_failure(const char *stage, int err)
{
    last_error = err;
    pspDebugScreenClear();
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("PSP AI Terminal v0.3.1 Stability\n");
    pspDebugScreenPrintf("========================================\n");
    pspDebugScreenPrintf("FAILED STAGE:\n%s\n\n", stage);
    pspDebugScreenPrintf("ERROR: 0x%08X (%d)\n", err, err);
    pspDebugScreenPrintf("AP state: %d\n", ap_state);
    pspDebugScreenPrintf("Profile: #%d %.24s\n", selected_profile, selected_name);
    pspDebugScreenPrintf("\nThe app will NOT auto-exit.\n");
    pspDebugScreenPrintf("A log was written to:\nms0:/PSPAI_NET.LOG\n\n");
    pspDebugScreenPrintf("Press O to exit manually.\n");

    log_text(stage);
    log_line("error", err);
    log_line("ap_state", ap_state);

    SceCtrlData pad = {0};
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CIRCLE) break;
        sceKernelDelayThread(100000);
    }
}

static int init_network_stack(void)
{
    int err;

    log_text("STEP 1: sceUtilityLoadNetModule(COMMON)");
    err = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
    log_line("COMMON result", err);
    if (err < 0) return err;
    modules_common_loaded = 1;

    log_text("STEP 2: sceUtilityLoadNetModule(INET)");
    err = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
    log_line("INET result", err);
    if (err < 0) return err;
    modules_inet_loaded = 1;

    /*
     * v0.1.5 exposed 0x80410A0D:
     * SCE_ERROR_NET_APCTL_NOT_INITIALIZED.
     *
     * Do not rely on sceNetInit() / sceNetInetInit() / sceNetApctlInit() here. Initialize the exact three
     * layers explicitly, matching the official PSPSDK netdialog sample.
     */

    log_text("STEP 3: sceNetInit()");
    err = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
    log_line("sceNetInit result", err);
    if (err < 0) return err;
    net_initialized = 1;

    log_text("STEP 4: sceNetInetInit()");
    err = sceNetInetInit();
    log_line("sceNetInetInit result", err);
    if (err < 0) return err;
    inet_initialized = 1;

    log_text("STEP 5: sceNetApctlInit()");
    err = sceNetApctlInit(0x8000, 48);
    log_line("sceNetApctlInit result", err);
    if (err < 0) return err;
    apctl_initialized = 1;

    return 0;
}

static int connect_profile(void)
{
    int err;

    online = 0;
    ap_state = 0;
    strcpy(ip_addr, "0.0.0.0");

    log_text("STEP 6: sceNetApctlConnect(profile)");
    log_line("profile", selected_profile);

    err = sceNetApctlConnect(selected_profile);
    log_line("sceNetApctlConnect result", err);
    if (err != 0) return err;

    int last = -1;

    for (int i = 0; i < 600 && running; ++i) {
        int state = 0;
        err = sceNetApctlGetState(&state);

        if (err != 0) {
            log_line("sceNetApctlGetState error", err);
            return err;
        }

        ap_state = state;

        if (state != last) {
            log_line("AP state changed", state);
            last = state;

            pspDebugScreenClear();
            pspDebugScreenPrintf("PSP AI Terminal v0.3.1 Stability\n");
            pspDebugScreenPrintf("----------------------------------------\n");
            pspDebugScreenPrintf("Profile #%d: %.24s\n", selected_profile, selected_name);
            pspDebugScreenPrintf("Connecting...\n");
            pspDebugScreenPrintf("AP state: %d / 4\n", state);
            pspDebugScreenPrintf("\nWaiting up to 30 seconds.\n");
        }

        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            union SceNetApctlInfo info;
            memset(&info, 0, sizeof(info));

            err = sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info);
            log_line("sceNetApctlGetInfo(IP)", err);

            if (err == 0) {
                strncpy(ip_addr, info.ip, 15);
                ip_addr[15] = 0;
                if (logf) {
                    fprintf(logf, "IP: %s\n", ip_addr);
                    fflush(logf);
                }
            }

            online = 1;
            last_error = 0;
            return 0;
        }

        sceKernelDelayThread(50000);
    }

    return -1;
}

/* PSPAI v0.3: the ESP32 is a TCP bridge, not the AI endpoint.
 * Each POST is a new HTTP/1.0 connection through 192.168.4.1:8088.
 * Use plain ASCII on PSP (the remote model already replies in ASCII).
 */
static char answer_text[HTTP_BUFFER] = "";
static char answer_lines[ANSWER_LINES][ANSWER_COLS + 1];
static int answer_line_count = 0;
static int current_page = 0;
static char last_question[MAX_QUESTION + 1] = "";
static char last_status[144] = "Press X to compose or TRIANGLE for test.";
static const char *kb_rows[] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ0123",
    "456789.,?!",
    " /:-_@'()+"
};
#define KB_ROWS 5
#define KB_COLS 10

static int do_ai_request(const char *question)
{
    char head[500];
    char response[HTTP_BUFFER];
    size_t used = 0;
    int sock = -1;
    int err = -1;
    int response_code = 0;
    const char *body;
    struct sockaddr_in addr;

    answer_text[0] = 0;
    log_text("CHAT begin");
    if (!question || !question[0] || strlen(question) > MAX_QUESTION) {
        snprintf(last_status, sizeof(last_status), "Invalid question length");
        return -1;
    }
    if (!online) {
        snprintf(last_status, sizeof(last_status), "Wi-Fi offline");
        return -1;
    }

    sock = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(last_status, sizeof(last_status), "Socket error: %d", sceNetInetGetErrno());
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RELAY_PORT);
    addr.sin_addr.s_addr = inet_addr(RELAY_HOST);
    log_text("CHAT connect begin");
    err = sceNetInetConnect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        snprintf(last_status, sizeof(last_status), "Connect failed: %d", sceNetInetGetErrno());
        log_line("CHAT connect error", sceNetInetGetErrno());
        sceNetInetClose(sock);
        return -1;
    }

    log_text("CHAT connect OK");
    snprintf(head, sizeof(head),
        "POST /ask HTTP/1.0\r\n"
        "Host: 192.168.4.1:8088\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n\r\n", (unsigned int)strlen(question));

    /* Bound individual sends so HOME exit can eventually complete. */
    SceNetInetTimeval send_timeout = { 4, 0 };
    sceNetInetSetsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    log_text("CHAT sending request");
    /* Guarantee complete writes; ESP32 bridge does not interpret HTTP. */
    const char *parts[2] = { head, question };
    for (int p = 0; p < 2; ++p) {
        int len = (int)strlen(parts[p]), sent = 0;
        while (sent < len && running) {
            int n = sceNetInetSend(sock, parts[p] + sent, len - sent, 0);
            if (n <= 0) {
                snprintf(last_status, sizeof(last_status), "Send failed: %d", sceNetInetGetErrno());
                sceNetInetClose(sock);
                return -1;
            }
            sent += n;
        }
    }

    /* Wait in short interruptible slices: never block indefinitely in recv.
       PSP HOME exit sets running=0; poll resumes within 250 ms. */
    log_text("CHAT request sent; waiting response");
    int waited_ms = 0;
    while (running && used + 1 < sizeof(response)) {
        SceNetInetPollfd watch = { sock, SCE_NET_INET_POLLIN | SCE_NET_INET_POLLHUP, 0 };
        int ready = sceNetInetPoll(&watch, 1, 250);
        if (ready == 0) {
            waited_ms += 250;
            if (waited_ms >= 30000) {
                snprintf(last_status, sizeof(last_status), "AI response timeout (30s)");
                log_text("CHAT timed out after 30s");
                sceNetInetClose(sock);
                return -1;
            }
            continue;
        }
        if (ready < 0 || (watch.revents & (SCE_NET_INET_POLLERR | SCE_NET_INET_POLLNVAL))) {
            snprintf(last_status, sizeof(last_status), "Network poll error: %d", sceNetInetGetErrno());
            log_text("CHAT network poll error");
            sceNetInetClose(sock);
            return -1;
        }
        if (!(watch.revents & (SCE_NET_INET_POLLIN | SCE_NET_INET_POLLHUP))) {
            waited_ms += 250;
            continue;
        }
        int n = sceNetInetRecv(sock, response + used,
                               (int)(sizeof(response) - used - 1), 0);
        if (n < 0) {
            snprintf(last_status, sizeof(last_status), "Receive failed: %d", sceNetInetGetErrno());
            log_line("CHAT receive errno", sceNetInetGetErrno());
            sceNetInetClose(sock);
            return -1;
        }
        if (n == 0) break;
        used += (size_t)n;
        waited_ms = 0;
    }
    log_line("CHAT response raw bytes", (int)used);
    sceNetInetClose(sock);
    response[used] = 0;
    if (used == 0) {
        snprintf(last_status, sizeof(last_status), "Relay returned no data");
        return -1;
    }
    if (sscanf(response, "HTTP/%*s %d", &response_code) != 1) {
        snprintf(last_status, sizeof(last_status), "Malformed HTTP response");
        return -1;
    }
    body = strstr(response, "\r\n\r\n");
    if (!body) {
        snprintf(last_status, sizeof(last_status), "No HTTP body");
        return -1;
    }
    body += 4;
    snprintf(answer_text, sizeof(answer_text), "%s", body);
    if (response_code != 200) {
        snprintf(last_status, sizeof(last_status), "Relay HTTP %d: %.88s", response_code, body);
        log_line("CHAT HTTP error", response_code);
        return -1;
    }
    snprintf(last_status, sizeof(last_status), "AI response OK (%u bytes)", (unsigned int)strlen(body));
    log_line("CHAT response bytes", (int)strlen(body));
    return 0;
}

static void wrap_answer(void)
{
    const char *s = answer_text;
    answer_line_count = 0;
    current_page = 0;
    while (*s && answer_line_count < ANSWER_LINES) {
        char *line = answer_lines[answer_line_count];
        int width = 0;
        while (*s == ' ') ++s;
        if (*s == '\n') { ++s; line[0] = 0; ++answer_line_count; continue; }
        while (*s && *s != '\n' && width < ANSWER_COLS) {
            line[width++] = *s++;
        }
        /* Wrap on the last word boundary whenever possible. */
        if (width == ANSWER_COLS && *s && *s != '\n') {
            int break_at = width - 1;
            while (break_at > 12 && line[break_at] != ' ') --break_at;
            if (break_at > 12) {
                s -= (width - break_at - 1);
                width = break_at;
            }
        }
        line[width] = 0;
        if (*s == '\n') ++s;
        ++answer_line_count;
    }
    if (!answer_line_count) {
        strcpy(answer_lines[0], "(empty reply)");
        answer_line_count = 1;
    }
}

static void draw_chat(void)
{
    pspDebugScreenClear();
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("PSPAI v0.3  |  %s  |  %s\n", ip_addr, RELAY_HOST);
    pspDebugScreenPrintf("--------------------------------------------------------\n");
    pspDebugScreenPrintf("ASK: %.53s\n", last_question[0] ? last_question : "(none)");
    pspDebugScreenPrintf("STATUS: %.51s\n", last_status);
    pspDebugScreenPrintf("--------------------------------------------------------\n");
    if (answer_line_count) {
        int from = current_page * ANSWER_PAGE_LINES;
        int until = from + ANSWER_PAGE_LINES;
        if (until > answer_line_count) until = answer_line_count;
        for (int i = from; i < until; ++i)
            pspDebugScreenPrintf("%s\n", answer_lines[i]);
    } else {
        pspDebugScreenPrintf("\nPSP -> ESP32-S3 -> Mac Relay -> Qwen\n");
        pspDebugScreenPrintf("Ready for a real AI reply.\n");
    }
    pspDebugScreenSetXY(0, 27);
    int pages = (answer_line_count + ANSWER_PAGE_LINES - 1) / ANSWER_PAGE_LINES;
    if (pages < 1) pages = 1;
    pspDebugScreenPrintf("Page %d/%d | UP/DOWN turn page\n", current_page + 1, pages);
    pspDebugScreenPrintf("X Compose | TRIANGLE test | CIRCLE exit\n");
}

static int compose_question(char *dst, size_t cap)
{
    char typed[MAX_QUESTION + 1] = "";
    int count = 0, x = 0, y = 0;
    SceCtrlData pad = {0}, old = {0};
    while (running) {
        pspDebugScreenClear();
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenPrintf("PSPAI - COMPOSE (English ASCII)\n");
        pspDebugScreenPrintf("------------------------------------------\n");
        pspDebugScreenPrintf("Question (%d/%d):\n", count, MAX_QUESTION);
        /* Show tail so long input remains visible on 480px screen. */
        pspDebugScreenPrintf("%.56s\n", count > 56 ? typed + count - 56 : typed);
        pspDebugScreenPrintf("\nD-Pad choose characters. X inserts.\n");
        pspDebugScreenPrintf("SQUARE delete. R space. L clear.\n");
        pspDebugScreenPrintf("START sends. CIRCLE cancels.\n\n");
        for (int j = 0; j < KB_ROWS; ++j) {
            for (int i = 0; i < KB_COLS; ++i) {
                char c = kb_rows[j][i];
                if (i == x && j == y) pspDebugScreenPrintf("[%c]", c);
                else pspDebugScreenPrintf(" %c ", c);
            }
            pspDebugScreenPrintf("\n");
        }
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~old.Buttons;
        if (pressed & PSP_CTRL_LEFT) x = (x + KB_COLS - 1) % KB_COLS;
        if (pressed & PSP_CTRL_RIGHT) x = (x + 1) % KB_COLS;
        if (pressed & PSP_CTRL_UP) y = (y + KB_ROWS - 1) % KB_ROWS;
        if (pressed & PSP_CTRL_DOWN) y = (y + 1) % KB_ROWS;
        if ((pressed & PSP_CTRL_CROSS) && count < MAX_QUESTION) {
            typed[count++] = kb_rows[y][x]; typed[count] = 0;
        }
        if ((pressed & PSP_CTRL_RTRIGGER) && count < MAX_QUESTION) {
            typed[count++] = ' '; typed[count] = 0;
        }
        if ((pressed & PSP_CTRL_SQUARE) && count > 0) typed[--count] = 0;
        if (pressed & PSP_CTRL_LTRIGGER) { count = 0; typed[0] = 0; }
        if (pressed & PSP_CTRL_CIRCLE) return 0;
        if (pressed & PSP_CTRL_START) {
            if (!count) continue;
            while (count > 0 && typed[count - 1] == ' ') typed[--count] = 0;
            if (!count) continue;
            snprintf(dst, cap, "%s", typed);
            return 1;
        }
        old = pad;
        sceKernelDelayThread(90000);
    }
    return 0;
}

static void send_question(const char *question)
{
    snprintf(last_question, sizeof(last_question), "%s", question);
    snprintf(last_status, sizeof(last_status), "Waiting for Mac Relay / AI...");
    answer_line_count = 0;
    current_page = 0;
    draw_chat();
    int ok = do_ai_request(question);
    if (ok == 0) wrap_answer();
    else {
        snprintf(answer_text, sizeof(answer_text), "%s", last_status);
        wrap_answer();
    }
    draw_chat();
}

static void cleanup_network(void)
{
    log_text("CLEANUP begin");


    if (apctl_initialized) {
        int r = sceNetApctlDisconnect();
        log_line("sceNetApctlDisconnect", r);
        sceKernelDelayThread(150000);

        r = sceNetApctlTerm();
        log_line("sceNetApctlTerm", r);
        apctl_initialized = 0;
    }

    if (inet_initialized) {
        int r = sceNetInetTerm();
        log_line("sceNetInetTerm", r);
        inet_initialized = 0;
    }

    if (net_initialized) {
        int r = sceNetTerm();
        log_line("sceNetTerm", r);
        net_initialized = 0;
    }

    if (modules_inet_loaded) {
        int r = sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
        log_line("Unload INET", r);
        modules_inet_loaded = 0;
    }

    if (modules_common_loaded) {
        int r = sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
        log_line("Unload COMMON", r);
        modules_common_loaded = 0;
    }

    log_text("CLEANUP end");
}

int main(int argc, char *argv[])
{
    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    pspDebugScreenInit();
    log_open();
    selected_profile = choose_profile();
    if (selected_profile < 1) {
        if (logf) fclose(logf);
        sceKernelExitGame();
        return 0;
    }
    int err = init_network_stack();
    if (err) {
        wait_on_failure("NETWORK STACK INITIALIZATION", err);
        cleanup_network();
        if (logf) fclose(logf);
        sceKernelExitGame();
        return 0;
    }
    err = connect_profile();
    if (err) {
        wait_on_failure("ACCESS POINT CONNECTION", err);
        cleanup_network();
        if (logf) fclose(logf);
        sceKernelExitGame();
        return 0;
    }
    snprintf(last_status, sizeof(last_status), "Wi-Fi OK, connected to %s", ip_addr);
    draw_chat();
    SceCtrlData pad = {0}, old = {0};
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~old.Buttons;
        if (pressed & PSP_CTRL_CROSS) {
            char question[MAX_QUESTION + 1];
            if (compose_question(question, sizeof(question))) send_question(question);
            draw_chat();
            /* Do not interpret the same held X as another compose request. */
            sceCtrlReadBufferPositive(&pad, 1);
            old = pad;
        } else if (pressed & PSP_CTRL_TRIANGLE) {
            send_question("HELLO PSPAI, SAY YOU ARE ONLINE");
            sceCtrlReadBufferPositive(&pad, 1);
            old = pad;
        } else if (pressed & PSP_CTRL_DOWN) {
            if ((current_page + 1) * ANSWER_PAGE_LINES < answer_line_count) ++current_page;
            draw_chat();
        } else if (pressed & PSP_CTRL_UP) {
            if (current_page > 0) --current_page;
            draw_chat();
        } else if (pressed & PSP_CTRL_CIRCLE) {
            running = 0;
        }
        old = pad;
        sceKernelDelayThread(70000);
    }
    cleanup_network();
    if (logf) { fclose(logf); logf = NULL; }
    sceKernelDelayThread(200000);
    sceKernelExitGame();
    return 0;
}