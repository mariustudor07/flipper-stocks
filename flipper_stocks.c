#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "flipper_http/flipper_http.h"
#include "jsmn/jsmn.h"
#include "credentials.h"

// ─── Config ───────────────────────────────────────────────────────────────────

#define DATA_PATH    STORAGE_EXT_PATH_PREFIX "/apps_data/flipper_stocks/data.txt"
#define CYCLE_MS     20000
#define WORKER_STACK 4096

static const char* TICKERS[] = {"AAPL", "SI=F", "NVDA", "MSFT", "AMZN"};
static const int   N_TICKERS  = 5;

// ─── Types ────────────────────────────────────────────────────────────────────

typedef struct {
    int32_t  o, h, l, c; // price * 100
    uint32_t v;           // volume / 1000
} Candle;

typedef enum {
    StateLoading,
    StateConnecting,
    StateFetching,
    StateDisplay,
    StateError,
} Screen;

typedef struct {
    char    ticker[12];
    char    price_str[14];
    char    change_str[10];
    Candle  candles[6];
    int     n_candles;
    int     sel;
    bool    show_ohlc;
    Screen  screen;
    char    err[48];
    FuriMutex* mutex;
} State;

typedef struct {
    State*            st;
    FuriMessageQueue* iq;
    ViewPort*         vp;
    FuriThread*       wt;
    int               ticker_idx;
    volatile bool     running;
} App;

// ─── Float helpers ────────────────────────────────────────────────────────────

static float stof(const char* s) {
    if(!s || !*s) return 0.0f;
    bool neg = *s == '-'; if(neg) s++;
    float r = 0.0f;
    while(*s >= '0' && *s <= '9') r = r*10.0f + (*s++ - '0');
    if(*s == '.') { s++; float f=0.1f; while(*s >= '0' && *s <= '9'){r += (*s++-'0')*f; f*=0.1f;} }
    return neg ? -r : r;
}

static void cents_to_str(int32_t c, char* buf, int n) {
    bool neg = c < 0; if(neg) c = -c;
    snprintf(buf, n, "%s%ld.%02ld", neg?"-":"", (long)(c/100), (long)(c%100));
}

// ─── UI helpers ───────────────────────────────────────────────────────────────

static void redraw(App* app, Screen s) {
    furi_mutex_acquire(app->st->mutex, FuriWaitForever);
    app->st->screen = s;
    furi_mutex_release(app->st->mutex);
    view_port_update(app->vp);
}

static void set_err(App* app, const char* msg) {
    furi_mutex_acquire(app->st->mutex, FuriWaitForever);
    app->st->screen = StateError;
    strncpy(app->st->err, msg, sizeof(app->st->err)-1);
    furi_mutex_release(app->st->mutex);
    view_port_update(app->vp);
}

// ─── Yahoo Finance parser ─────────────────────────────────────────────────────

static int parse_arr(const char* p, float* out, int max) {
    int n = 0;
    while(n < max && p && *p && *p != ']') {
        while(*p==' '||*p==','||*p=='\n') p++;
        if(*p==']'||!*p) break;
        if(strncmp(p,"null",4)==0){out[n++]=0;p+=4;continue;}
        bool neg=*p=='-'; if(neg)p++;
        float r=0; while(*p>='0'&&*p<='9') r=r*10+(*p++-'0');
        if(*p=='.'){p++;float f=0.1f;while(*p>='0'&&*p<='9'){r+=(*p++-'0')*f;f*=0.1f;}}
        if(*p=='e'||*p=='E'){p++;if(*p=='+'||*p=='-')p++;while(*p>='0'&&*p<='9')p++;}
        out[n++]=neg?-r:r;
    }
    return n;
}

static const char* find_arr(const char* j, const char* key) {
    const char* p = strstr(j, key);
    if(!p) return NULL;
    p += strlen(key);
    while(*p && *p!='[') p++;
    return *p ? p+1 : NULL;
}

static bool parse_yahoo(App* app, const char* json) {
    State* st = app->st;

    const char* quote = strstr(json, "\"quote\":[{");
    if(!quote) return false;

    float opens[6]={0},highs[6]={0},lows[6]={0},closes[6]={0},vols[6]={0};
    int no=0,nh=0,nl=0,nc=0;
    const char* arr;
    arr=find_arr(quote,"\"open\":");    if(arr) no=parse_arr(arr,opens,6);
    arr=find_arr(quote,"\"high\":");    if(arr) nh=parse_arr(arr,highs,6);
    arr=find_arr(quote,"\"low\":");     if(arr) nl=parse_arr(arr,lows,6);
    arr=find_arr(quote,"\"close\":"); if(arr) nc=parse_arr(arr,closes,6);
    arr=find_arr(quote,"\"volume\":"); if(arr) parse_arr(arr,vols,6);

    int n = no<nh?no:nh; if(nl<n)n=nl; if(nc<n)n=nc;
    if(n==0) return false;

    // Try to get live price from meta
    float live = closes[n-1];
    const char* rmp = strstr(json,"\"regularMarketPrice\":");
    if(rmp) {
        rmp += strlen("\"regularMarketPrice\":");
        while(*rmp==' ') rmp++;
        float r = stof(rmp);
        if(r > 0.0f) live = r;
    }

    float prev = n>1 ? closes[n-2] : live;
    float chg  = prev>0.0f ? ((live-prev)/prev*100.0f) : 0.0f;

    furi_mutex_acquire(st->mutex, FuriWaitForever);
    st->n_candles = n;
    for(int i=0;i<n;i++){
        st->candles[i].o=(int32_t)(opens[i]*100);
        st->candles[i].h=(int32_t)(highs[i]*100);
        st->candles[i].l=(int32_t)(lows[i]*100);
        st->candles[i].c=(int32_t)(closes[i]*100);
        st->candles[i].v=(uint32_t)(vols[i]/1000.0f);
    }
    st->candles[n-1].c=(int32_t)(live*100);

    cents_to_str((int32_t)(live*100), st->price_str, sizeof(st->price_str));

    bool neg_chg = chg<0; float ac=neg_chg?-chg:chg;
    snprintf(st->change_str, sizeof(st->change_str), "%s%d.%02d%%",
             neg_chg?"-":"+", (int)ac, (int)((ac-(int)ac)*100+0.5f));

    st->sel=n-1; st->show_ohlc=false; st->screen=StateDisplay;
    furi_mutex_release(st->mutex);
    return true;
}

// ─── Draw ─────────────────────────────────────────────────────────────────────

static int pty(int32_t p, int32_t mn, int32_t rng, int top, int h) {
    if(rng==0) return top+h/2;
    return top+h-1-(int)((float)(p-mn)/rng*h);
}

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    State* st = app->st;
    furi_mutex_acquire(st->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    if(st->screen==StateLoading) {
        canvas_draw_box(canvas,0,0,128,9);
        canvas_set_color(canvas,ColorWhite);
        canvas_draw_str(canvas,2,7,"FLIPPER STOCKS");
        canvas_set_color(canvas,ColorBlack);
        canvas_draw_str_aligned(canvas,64,36,AlignCenter,AlignCenter,"Starting up...");
        furi_mutex_release(st->mutex); return;
    }
    if(st->screen==StateConnecting) {
        canvas_draw_box(canvas,0,0,128,9);
        canvas_set_color(canvas,ColorWhite);
        canvas_draw_str(canvas,2,7,"CONNECTING");
        canvas_set_color(canvas,ColorBlack);
        canvas_draw_str_aligned(canvas,64,32,AlignCenter,AlignCenter,WIFI_SSID);
        canvas_draw_str_aligned(canvas,64,46,AlignCenter,AlignCenter,"Please wait...");
        furi_mutex_release(st->mutex); return;
    }
    if(st->screen==StateFetching) {
        canvas_draw_box(canvas,0,0,128,9);
        canvas_set_color(canvas,ColorWhite);
        canvas_draw_str(canvas,2,7,st->ticker);
        canvas_set_color(canvas,ColorBlack);
        canvas_draw_str_aligned(canvas,64,36,AlignCenter,AlignCenter,"Fetching data...");
        furi_mutex_release(st->mutex); return;
    }
    if(st->screen==StateError) {
        canvas_draw_box(canvas,0,0,128,9);
        canvas_set_color(canvas,ColorWhite);
        canvas_draw_str(canvas,2,7,"ERROR");
        canvas_set_color(canvas,ColorBlack);
        canvas_draw_str_aligned(canvas,64,32,AlignCenter,AlignCenter,st->err);
        canvas_draw_str_aligned(canvas,64,52,AlignCenter,AlignCenter,"[back] to exit");
        furi_mutex_release(st->mutex); return;
    }

    // Chart header
    canvas_draw_box(canvas,0,0,128,9);
    canvas_set_color(canvas,ColorWhite);
    if(st->show_ohlc && st->sel>=0 && st->sel<st->n_candles) {
        Candle* c=&st->candles[st->sel];
        char b[14];
        cents_to_str(c->o,b,sizeof(b)); canvas_draw_str(canvas, 0,7,b);
        cents_to_str(c->h,b,sizeof(b)); canvas_draw_str(canvas,32,7,b);
        cents_to_str(c->l,b,sizeof(b)); canvas_draw_str(canvas,64,7,b);
        cents_to_str(c->c,b,sizeof(b)); canvas_draw_str(canvas,96,7,b);
    } else {
        canvas_draw_str(canvas, 2,7,st->ticker);
        canvas_draw_str(canvas,34,7,st->price_str);
        canvas_draw_str(canvas,86,7,st->change_str);
    }
    canvas_set_color(canvas,ColorBlack);

    if(st->n_candles==0){furi_mutex_release(st->mutex);return;}

    // Scale
    int32_t mnp=st->candles[0].l, mxp=st->candles[0].h;
    uint32_t mxv=st->candles[0].v;
    for(int i=1;i<st->n_candles;i++){
        if(st->candles[i].l<mnp) mnp=st->candles[i].l;
        if(st->candles[i].h>mxp) mxp=st->candles[i].h;
        if(st->candles[i].v>mxv) mxv=st->candles[i].v;
    }
    int32_t rng=mxp-mnp;
    int32_t pad=rng/20; if(pad<50)pad=50;
    mnp-=pad; mxp+=pad; rng=mxp-mnp; if(rng==0)rng=1;

    // Y labels
    char lb[12];
    cents_to_str(mxp,lb,sizeof(lb)); canvas_draw_str(canvas,0,15,lb);
    cents_to_str(mnp+rng/2,lb,sizeof(lb)); canvas_draw_str(canvas,0,30,lb);
    cents_to_str(mnp,lb,sizeof(lb)); canvas_draw_str(canvas,0,46,lb);

    // Axes
    canvas_draw_line(canvas,13,10,13,48);
    canvas_draw_line(canvas,14,48,127,48);
    canvas_draw_line(canvas,14,49,127,49);
    for(int x=18;x<128;x+=6) canvas_draw_dot(canvas,x,29);

    // Candles
    const int ct=10,ch=38,vt=50,vh=13,ox=14,cw=13,gap=4;
    for(int i=0;i<st->n_candles;i++){
        Candle* c=&st->candles[i];
        int bx=ox+i*(cw+gap), cx=bx+cw/2;
        bool up=c->c>=c->o, sel=(i==st->sel&&st->show_ohlc);
        int wt=pty(c->h,mnp,rng,ct,ch), wb=pty(c->l,mnp,rng,ct,ch);
        int32_t bhi=c->c>c->o?c->c:c->o, blo=c->c<c->o?c->c:c->o;
        int bt=pty(bhi,mnp,rng,ct,ch), bb=pty(blo,mnp,rng,ct,ch);
        int bh=bb-bt; if(bh<1)bh=1;

        if(sel) for(int yy=ct;yy<=48;yy+=2) canvas_draw_dot(canvas,cx,yy);
        canvas_draw_line(canvas,cx,wt,cx,wb);
        if(up) canvas_draw_frame(canvas,bx,bt,cw,bh);
        else   canvas_draw_box(canvas,bx,bt,cw,bh);
        if(sel) canvas_draw_frame(canvas,bx-1,bt-1,cw+2,bh+2);

        if(mxv>0){
            int bvh=(int)((float)c->v/mxv*vh); if(bvh<1)bvh=1;
            int vy=vt+vh-bvh;
            if(up) canvas_draw_frame(canvas,bx,vy,cw,bvh);
            else   canvas_draw_box(canvas,bx,vy,cw,bvh);
            if(sel) canvas_draw_frame(canvas,bx-1,vy-1,cw+2,bvh+2);
        }
    }
    canvas_draw_str(canvas,0,62,"VOL");
    canvas_draw_str(canvas,64,62,st->show_ohlc?"[<>][ok]=back":"[ok]=OHLC");

    furi_mutex_release(st->mutex);
}

// ─── Input ────────────────────────────────────────────────────────────────────

static void input_cb(InputEvent* e, void* ctx) {
    furi_message_queue_put(((App*)ctx)->iq, e, 0);
}

// ─── Worker ───────────────────────────────────────────────────────────────────

static bool wait_http(App* app, uint32_t ms) {
    uint32_t w=0;
    while((fhttp.state==SENDING||fhttp.state==RECEIVING) && w<ms && app->running){
        furi_delay_ms(100); w+=100;
    }
    return fhttp.state==IDLE;
}

static int32_t worker(void* ctx) {
    App* app = ctx;
    State* st = app->st;

    // Wait for board to boot then ping it
    redraw(app, StateConnecting);
    furi_delay_ms(3000);

    // Ping to confirm board is alive
    bool board_alive = false;
    for(int attempt = 0; attempt < 5 && !board_alive && app->running; attempt++) {
        flipper_http_ping();
        furi_delay_ms(1000);
        if(fhttp.last_response && strstr(fhttp.last_response, "[PONG]")) {
            board_alive = true;
        }
    }
    if(!app->running) return 0;
    if(!board_alive){ set_err(app, "Board not responding"); return 0; }

    // Save WiFi and connect
    flipper_http_save_wifi(WIFI_SSID, WIFI_PASS);
    // Wait for connection
    uint32_t t=0;
    while(fhttp.state!=IDLE && fhttp.state!=INACTIVE && t<15000 && app->running){
        furi_delay_ms(100); t+=100;
    }
    if(!app->running) return 0;

    // Main fetch loop
    while(app->running) {
        const char* ticker = TICKERS[app->ticker_idx % N_TICKERS];

        furi_mutex_acquire(st->mutex, FuriWaitForever);
        strncpy(st->ticker, ticker, sizeof(st->ticker)-1);
        st->screen = StateFetching;
        furi_mutex_release(st->mutex);
        view_port_update(app->vp);

        // Build URL and set save path
        char url[128];
        snprintf(url, sizeof(url),
            "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=7d",
            ticker);
        snprintf(fhttp.file_path, sizeof(fhttp.file_path), DATA_PATH);
        fhttp.save_received_data = true;

        // Send GET request
        char* headers = jsmn("Content-Type", "application/json");
        bool ok = flipper_http_get_request_with_headers(url, headers);
        free(headers);

        if(!ok || fhttp.state==ISSUE) {
            set_err(app, "Request failed");
            furi_delay_ms(3000);
            app->ticker_idx++;
            continue;
        }

        // Wait for response
        furi_delay_ms(500);
        bool got = wait_http(app, 30000);
        if(!app->running) break;

        if(got) {
            FuriString* data = flipper_http_load_from_file(fhttp.file_path);
            if(data) {
                if(!parse_yahoo(app, furi_string_get_cstr(data)))
                    set_err(app, "Parse failed");
                furi_string_free(data);
            } else {
                set_err(app, "No data received");
            }
        } else {
            set_err(app, "Timeout");
        }

        view_port_update(app->vp);

        // Cycle delay
        for(int i=0; i<CYCLE_MS/100 && app->running; i++) furi_delay_ms(100);
        app->ticker_idx++;
    }
    return 0;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int32_t flipper_stocks_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->st = malloc(sizeof(State));
    memset(app->st, 0, sizeof(State));
    app->st->mutex  = furi_mutex_alloc(FuriMutexTypeNormal);
    app->st->screen = StateLoading;
    app->running    = true;

    app->iq = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->vp = view_port_alloc();
    view_port_draw_callback_set(app->vp, draw_cb, app);
    view_port_input_callback_set(app->vp, input_cb, app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->vp, GuiLayerFullscreen);

    // Init FlipperHTTP (UART to WiFi board)
    if(!flipper_http_init(flipper_http_rx_callback, app)) {
        set_err(app, "FlipperHTTP init failed");
        furi_delay_ms(3000);
    }

    // Create storage directory
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_EXT_PATH_PREFIX "/apps_data/flipper_stocks");
    furi_record_close(RECORD_STORAGE);

    // Start worker
    app->wt = furi_thread_alloc_ex("FSWorker", WORKER_STACK, worker, app);
    furi_thread_start(app->wt);

    // Input loop
    InputEvent e;
    while(app->running) {
        if(furi_message_queue_get(app->iq, &e, 100)==FuriStatusOk) {
            if(e.type==InputTypeShort || e.type==InputTypeRepeat) {
                State* st = app->st;
                furi_mutex_acquire(st->mutex, FuriWaitForever);
                switch(e.key) {
                case InputKeyBack:
                    if(st->show_ohlc) st->show_ohlc=false;
                    else app->running=false;
                    break;
                case InputKeyOk:
                    if(st->screen==StateDisplay) st->show_ohlc=!st->show_ohlc;
                    break;
                case InputKeyLeft:
                    if(st->show_ohlc && st->sel>0) st->sel--;
                    break;
                case InputKeyRight:
                    if(st->show_ohlc && st->sel<st->n_candles-1) st->sel++;
                    break;
                default: break;
                }
                furi_mutex_release(st->mutex);
                view_port_update(app->vp);
            }
        }
    }

    app->running = false;
    if(app->wt) { furi_thread_join(app->wt); furi_thread_free(app->wt); }
    flipper_http_deinit();
    gui_remove_view_port(gui, app->vp);
    furi_record_close(RECORD_GUI);
    view_port_free(app->vp);
    furi_message_queue_free(app->iq);
    furi_mutex_free(app->st->mutex);
    free(app->st);
    free(app);
    return 0;
}