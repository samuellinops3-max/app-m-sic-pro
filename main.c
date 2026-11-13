// BR MUSIC - Player para PS3 (PSL1GHT) - mesmo molde do Cobra Arena
// Controles: Cima/Baixo = navegar, X = tocar/pausar, Quadrado = parar, Triangulo = atualizar, START = sair
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <audio/audio.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)
#define MAXSONGS 500

typedef struct { char name[128]; char path[256]; int is_mp3; } Song;
static Song songs[MAXSONGS];
static int songCount = 0;
static int selected = 0;
static int playing = 0;
static int playingIdx = -1;
static int playTime = 0;
static int playDur = 180;
static int scroll = 0;

static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata){
    (void)param; (void)usrdata;
    if(status == SYSUTIL_EXIT_GAME) running = 0;
}
static void init_screen(void){
    void *host = memalign(1024*1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
    videoState st; videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if(videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;
    videoResolution res; videoGetResolution(resid, &res);
    W = res.width; H = res.height;
    videoConfiguration vc; memset(&vc,0,sizeof(vc));
    vc.resolution = resid; vc.format = VIDEO_BUFFER_FORMAT_XRGB; vc.aspect = VIDEO_ASPECT_AUTO; vc.pitch = W*4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64, W*H*4); gcmAddressToOffset(fb[i], &fboff[i]); gcmSetDisplayBuffer(i, fboff[i], W*4, W, H); }
    back=(u32*)memalign(64, W*H*4); memset(back,0,W*H*4);
    gcmResetFlipStatus();
}
static void wait_flip(void){ while(gcmGetFlipStatus()!=0) usleep(200); gcmResetFlipStatus(); }
static void present(void){
    if(!first_flip) wait_flip(); else gcmResetFlipStatus();
    memcpy(fb[cur], back, W*H*4);
    gcmSetFlip(ctx, cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx);
    cur ^= 1; first_flip = 0;
}
static void fill_rect(int x,int y,int w,int h,u32 c){
    int x0=x<0?0:x, y0=y<0?0:y; int x1=x+w, y1=y+h;
    if(x1>(int)W) x1=W; if(y1>(int)H) y1=H;
    for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
    if(r<1) r=1;
    for(int dy=-r;dy<=r;dy++){ int y=cy+dy; if(y<0||y>=(int)H) continue;
        int hw=(int)sqrtf((float)(r*r-dy*dy)); int x0=cx-hw, x1=cx+hw;
        if(x0<0) x0=0; if(x1>=(int)W) x1=W-1; u32 *p=back+y*W; for(int x=x0;x<=x1;x++) p[x]=c; }
}
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0',"###" "#.#" "#.#" "#.#" "###"}, {'1',".#." "##." ".#." ".#." "###"},
    {'2',"###" "..#" "###" "#.." "###"}, {'3',"###" "..#" "###" "..#" "###"},
    {'4',"#.#" "#.#" "###" "..#" "..#"}, {'5',"###" "#.." "###" "..#" "###"},
    {'6',"###" "#.." "###" "#.#" "###"}, {'7',"###" "..#" "..#" "..#" "..#"},
    {'8',"###" "#.#" "###" "#.#" "###"}, {'9',"###" "#.#" "###" "..#" "###"},
    {'A',".#." "#.#" "###" "#.#" "#.#"}, {'B',"##." "#.#" "##." "#.#" "##."},
    {'C',"###" "#.." "#.." "#.." "###"}, {'D',"##." "#.#" "#.#" "#.#" "##."},
    {'E',"###" "#.." "###" "#.." "###"}, {'F',"###" "#.." "###" "#.." "#.."},
    {'G',"###" "#.." "#.#" "#.#" "###"}, {'H',"#.#" "#.#" "###" "#.#" "#.#"},
    {'I',"###" ".#." ".#." ".#." "###"}, {'J',"###" "..#" "..#" "#.#" "###"},
    {'K',"#.#" "#.#" "##." "#.#" "#.#"}, {'L',"#.." "#.." "#.." "#.." "###"},
    {'M',"#.#" "###" "###" "#.#" "#.#"}, {'N',"#.#" "###" "###" "#.#" "#.#"},
    {'O',"###" "#.#" "#.#" "#.#" "###"}, {'P',"###" "#.#" "###" "#.." "#.."},
    {'Q',"###" "#.#" "#.#" ".#." ".##"}, {'R',"##." "#.#" "##." "#.#" "#.#"},
    {'S',"###" "#.." "###" "..#" "###"}, {'T',"###" ".#." ".#." ".#." ".#."},
    {'U',"#.#" "#.#" "#.#" "#.#" "###"}, {'V',"#.#" "#.#" "#.#" "#.#" ".#."},
    {'W',"#.#" "#.#" "###" "###" "#.#"}, {'X',"#.#" "#.#" ".#." "#.#" "#.#"},
    {'Y',"#.#" "#.#" ".#." ".#." ".#."}, {'Z',"###" "..#" ".#." "#.." "###"},
    {' ',"..." "..." "..." "..." "..."}, {'.',"..." "..." "..." "..." "##."},
    {'-', "..." "..." "###" "..." "..."}, {'_', "..." "..." "..." "..." "###"},
    {'/',"..#" "..#" ".#." "#.." "#.."}, {':',"..." "##." "..." "##." "..."},
    {'(',".##" "#.." "#.." "#.." ".##"}, {')',"##." "..#" "..#" "..#" "##."},
    {'!',"##." "##." "##." "..." "##."}, {'?',"###" "..#" ".#." "..." ".#."},
};
static const char *find_glyph(char c){
    if(c>='a'&&c<='z') c-=32;
    for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p;
    return NULL;
}
static int text_width(const char *s,int sc){ return (int)strlen(s)*4*sc - sc; }
static void draw_text(const char *s,int x,int y,int sc,u32 c){
    for(;*s;s++){ const char *g=find_glyph(*s);
        if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') fill_rect(x+k*sc,y+r*sc,sc,sc,c);
        x+=4*sc; }
}
static void draw_text_clip(const char *s,int x,int y,int sc,u32 c,int maxChars){
    char buf[128]; strncpy(buf,s,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    if((int)strlen(buf)>maxChars){ buf[maxChars-3]=0; strcat(buf,"..."); }
    draw_text(buf,x,y,sc,c);
}
static int is_music(const char *n){
    const char *e=strrchr(n,'.'); if(!e) return 0;
    return !strcasecmp(e,".mp3")||!strcasecmp(e,".wav")||!strcasecmp(e,".ogg")||!strcasecmp(e,".flac")||!strcasecmp(e,".m4a");
}
static void scan_folder(const char *folder){
    DIR *d=opendir(folder); if(!d) return;
    struct dirent *ent;
    while((ent=readdir(d))!=NULL && songCount<MAXSONGS){
        if(ent->d_name[0]=='.') continue;
        if(!is_music(ent->d_name)) continue;
        // Evita duplicar as fixas
        int dup=0; for(int i=0;i<songCount;i++){ if(!strcasecmp(songs[i].path, ent->d_name)) dup=1; }
        if(dup) continue;
        Song *s=&songs[songCount];
        strncpy(s->name,ent->d_name,sizeof(s->name)-1); s->name[sizeof(s->name)-1]=0;
        char *dot=strrchr(s->name,'.'); if(dot) *dot=0;
        snprintf(s->path,sizeof(s->path),"%s/%s",folder,ent->d_name);
        s->is_mp3=1; songCount++;
    }
    closedir(d);
}
static void scan_all(void){
    songCount=0; selected=0; scroll=0;
    // MUSICAS DO HTML - FIXAS!
    const char *fixas[][2] = {
        {"Smack That (feat. Eminem)", "Smack That (feat. Eminem).mp3"},
        {"Losing My Religion", "Losing My Religion.mp3"},
        {"Time", "Time.mp3"},
        {"Yeah! (feat. Lil Jon & Ludacris)", "Yeah! (feat. Lil Jon & Ludacris).mp3"},
        {"Yeah! (feat. Lil Jon)", "Yeah! (feat. Lil Jon).mp3"},
    };
    for(int i=0;i<5;i++){
        Song *s=&songs[songCount];
        strncpy(s->name, fixas[i][0], sizeof(s->name)-1); s->name[sizeof(s->name)-1]=0;
        snprintf(s->path, sizeof(s->path), "/dev_hdd0/game/BRMU00001/USRDIR/music/%s", fixas[i][1]);
        s->is_mp3=1; songCount++;
    }
    scan_folder("/dev_hdd0/music");
    scan_folder("/dev_hdd0/MUSIC");
    scan_folder("/dev_hdd0/BRMUSIC");
    scan_folder("/dev_usb000/music");
    scan_folder("/dev_usb000/MUSIC");
}
static void fmt_time(char *out,int sec){ sprintf(out,"%d:%02d",sec/60,sec%60); }
static void render(void){
    float sc=(float)W/1280.0f; int ui=(int)(4*sc+0.5f); if(ui<3) ui=3;
    u32 n=W*H; for(u32 i=0;i<n;i++) back[i]=0x0008080F;
    fill_rect(0,0,W,90,0x000A0A14);
    draw_text("BR", 24, 18, ui+3, 0x00FFFFFF);
    draw_text("MUSIC", 24+text_width("BR ",ui+3), 18, ui+3, 0x001ED760);
    char cnt[64]; sprintf(cnt,"%d MUSICAS", songCount);
    draw_text(cnt, W - text_width(cnt,ui) - 24, 28, ui, 0x00809BBF);
    draw_text("CIMA/BAIXO NAVEGAR  X TOCAR  QUADRADO PARAR  TRIANGULO ATUALIZAR", 24, 68, 2, 0x00666688);
    if(songCount==0){
        draw_text("NENHUMA MUSICA ACHADA!", W/2 - text_width("NENHUMA MUSICA ACHADA!",ui+2)/2, H/2-80, ui+2, 0x00FF4D4D);
    } else {
        int startY=110; int rowH=52; int visible=(H-110-150)/rowH;
        if(selected<scroll) scroll=selected; if(selected>=scroll+visible) scroll=selected-visible+1;
        for(int i=0;i<visible;i++){
            int idx=scroll+i; if(idx>=songCount) break;
            int y=startY+i*rowH;
            if(idx==selected) fill_rect(20,y,W-40,rowH-6,0x001A2A1A);
            else if(idx==playingIdx) fill_rect(20,y,W-40,rowH-6,0x000F1A2A);
            circle(52, y+22, 16, idx==playingIdx?0x001ED760:0x002A2A3A);
            draw_text(idx==playingIdx?"X":">", 46, y+12, 3, 0x00FFFFFF);
            char num[16]; sprintf(num,"%d.",idx+1);
            draw_text(num, 82, y+14, 3, 0x00809BBF);
            draw_text_clip(songs[idx].name, 140, y+14, 3, idx==selected?0x00FFFFFF:0x00CCCCCC, 42);
            if(idx==playingIdx) draw_text("TOCANDO", W-220, y+14, 3, 0x001ED760);
        }
        int py=H-130;
        fill_rect(0,py,W,130,0x0012121E);
        if(playingIdx>=0){
            draw_text_clip(songs[playingIdx].name, 24, py+16, ui+1, 0x00FFFFFF, 36);
            char t1[16], t2[16]; fmt_time(t1,playTime); fmt_time(t2,playDur);
            draw_text(t1, 24, py+58, ui, 0x00AAAAAA);
            int bx=110, bw=W-110-110;
            fill_rect(bx, py+62, bw, 10, 0x00333333);
            int pw = playDur>0 ? (bw*playTime/playDur) : 0;
            if(pw>0) fill_rect(bx, py+62, pw, 10, 0x001ED760);
            draw_text(t2, W-90, py+58, ui, 0x00AAAAAA);
            draw_text(playing?"|| PAUSADO - APERTA X PRA TOCAR":"|> TOCANDO - APERTA X PRA PAUSAR", 24, py+88, ui, playing?0x00FFD23F:0x001ED760);
        } else {
            draw_text("APERTA X NUMA MUSICA PRA TOCAR", 24, py+40, ui+1, 0x00809BBF);
        }
    }
}
int main(int argc,char **argv){
    (void)argc; (void)argv;
    init_screen();
    ioPadInit(7);
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    audioInit();
    srand(12345);
    scan_all();
    padInfo info; padData d;
    int prevUp=0, prevDown=0, prevCross=0, prevSquare=0, prevTri=0, prevStart=0;
    int frame=0;
    while(running){
        sysUtilCheckCallback();
        ioPadGetInfo(&info);
        int up=0, down=0, cross=0, square=0, tri=0, start=0;
        if(info.status[0]){ ioPadGetData(0,&d); up=d.BTN_UP; down=d.BTN_DOWN; cross=d.BTN_CROSS; square=d.BTN_SQUARE; tri=d.BTN_TRIANGLE; start=d.BTN_START; }
        if(up && !prevUp && songCount>0){ selected--; if(selected<0) selected=songCount-1; }
        if(down && !prevDown && songCount>0){ selected++; if(selected>=songCount) selected=0; }
        if(cross && !prevCross && songCount>0){
            if(playingIdx==selected){ playing=!playing; }
            else { playingIdx=selected; playing=1; playTime=0; playDur=120+rand()%180; }
        }
        if(square && !prevSquare){ playing=0; playingIdx=-1; playTime=0; }
        if(tri && !prevTri){ scan_all(); }
        if(start && !prevStart) break;
        prevUp=up; prevDown=down; prevCross=cross; prevSquare=square; prevTri=tri; prevStart=start;
        if(playing && playingIdx>=0){
            if(frame%60==0){ playTime++; if(playTime>=playDur){ playingIdx=(playingIdx+1)%songCount; playTime=0; playDur=120+rand()%180; } }
        }
        frame++;
        render();
        present();
        usleep(16000);
    }
    return 0;
}