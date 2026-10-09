#include "LapisGameplay.h"
#include "LapisText.h"
#include <string.h>

struct Writer { cc_uint8 data[512]; int n; };
static void Var(struct Writer* w, cc_uint32 v) { w->n += LapisProtocol_EncodeVarInt(w->data + w->n, v); }
static void Big(struct Writer* w, cc_uint64 v, int bytes) {
    int i;
    for (i = bytes - 1; i >= 0; i--) w->data[w->n++] = (cc_uint8)(v >> (i * 8));
}
static void Float(struct Writer* w, float v) { cc_uint32 bits; memcpy(&bits, &v, 4); Big(w, bits, 4); }
static void Double(struct Writer* w, double v) { cc_uint64 bits; memcpy(&bits, &v, 8); Big(w, bits, 8); }
static void Position(struct Writer* w, int x, int y, int z) {
    Big(w, (((cc_uint64)x & 0x3FFFFFF) << 38) | (((cc_uint64)z & 0x3FFFFFF) << 12) | ((cc_uint64)y & 4095), 8);
}
static int Send(struct LapisProtocol* p, int id, struct Writer* w) {
    return p->state == LAPIS_PLAY && p->loaded && LapisProtocol_Queue(p, id, w->data, w->n);
}
static int Coords(int x, int y, int z) { return x >= -32768 && x <= 32767 && z >= -32768 && z <= 32767 && y >= 0 && y <= 255; }
static int Stack(struct LapisReader* r, struct LapisStack* s) {
    s->item = 0; s->count = LapisReader_Count(r, 64);
    if (s->count) {
        s->item = LapisReader_Count(r, 1415);
        if (!s->item || LapisReader_VarInt(r) || LapisReader_VarInt(r)) return 0;
    }
    return !r->failed;
}
static int NbtText(struct LapisReader* r, char* text, int capacity) {
    if (LapisReader_Byte(r) != 8) return 0;
    return LapisText_Nbt(r,text,capacity);
}
void LapisGameplay_Init(struct LapisGameplay* g) {
    memset(g, 0, sizeof(*g)); g->health = 20; g->food = 20;
}
static void MirrorPlayer(struct LapisGameplay* g, int slot) {
    if (slot < 9 || slot > 44) return;
    g->slots[2][slot+18] = g->slots[0][slot];
    g->slots[12][slot+1] = g->slots[0][slot];
    g->slots[14][slot-6] = g->slots[0][slot];
}
int LapisGameplay_Packet(struct LapisGameplay* g, struct LapisProtocol* p, int id, const cc_uint8* data, int size) {
    struct LapisReader r; struct LapisStack stack; int w, slot, state; float health, saturation;
    char dimension[128],title[128];
    LapisReader_Init(&r, data, size);
    switch (id) {
    case 0x14:
        w = (cc_int32)LapisReader_VarInt(&r); state = LapisReader_Count(&r, 0x7FFFFFFF);
        /* The server also uses the direct player inventory window -2. */
        if (w == -2) w = 0;
        if (w != 0 && w != 2 && w != 12 && w != 14) return 0;
        slot = (int)LapisReader_Big(&r, 2);
        if (slot >= 64 || !Stack(&r, &stack) || !LapisReader_Done(&r)) return 0;
        g->slots[w][slot] = stack; g->stateId[w] = state;
        if(w==0 && slot<=4)g->refreshMask&=~(1u<<slot);
        /* Container views alias the same server-owned player inventory. */
        if(w==2 && slot>=27 && slot<63)g->slots[0][slot-18]=stack;
        if(w==12 && slot>=10 && slot<46)g->slots[0][slot-1]=stack;
        if(w==14 && slot>=3 && slot<39)g->slots[0][slot+6]=stack;
        if(w==0)MirrorPlayer(g,slot);
        break;
    case 0x59:
        if (!Stack(&r, &stack) || !LapisReader_Done(&r)) return 0;
        g->cursor = stack; break;
    case 0x62:
        slot = LapisReader_Byte(&r);
        if (slot > 8 || !LapisReader_Done(&r)) return 0;
        g->selected = slot; break;
    case 0x34:
        w = LapisReader_Count(&r, 15); state = LapisReader_Count(&r, 32);
        if ((w != 2 && w != 12 && w != 14) || w != state || !NbtText(&r, title, sizeof(title)) || !LapisReader_Done(&r)) return 0;
        strcpy(g->title,title);
        g->window = w; g->menu = state;
        memset(g->slots[w], 0, sizeof(g->slots[w]));
        /* Lapis sends no full inventory on Open Screen. These are aliases of
           already-authoritative player slots, never newly created items. */
        for(slot=9;slot<45;slot++)MirrorPlayer(g,slot);
        break;
    case 0x61:
        health = LapisReader_Float(&r); slot = LapisReader_Count(&r, 20); saturation = LapisReader_Float(&r);
        if (health < 0 || health > 20 || saturation < -0.4f || saturation > 131 || !LapisReader_Done(&r)) return 0;
        g->health = health; g->food = slot; g->saturation = saturation; break;
    case 0x39:
        slot = LapisReader_Byte(&r); LapisReader_Float(&r); LapisReader_Float(&r);
        if (!LapisReader_Done(&r)) return 0;
        g->abilities = slot; break;
    case 0x22:
        slot = LapisReader_Byte(&r); health = LapisReader_Float(&r);
        if (!LapisReader_Done(&r)) return 0;
        if (slot == 3) { if (health < 0 || health > 3 || health != (int)health) return 0; p->gamemode = (int)health; }
        break;
    case 0x04:
        state = LapisReader_Count(&r, 0x7FFFFFFF);
        if (!LapisReader_Done(&r)) return 0;
        g->acknowledged = (cc_uint32)state; break;
    case 0x72:
        if (!NbtText(&r, g->message, sizeof(g->message))) return 0;
        if (LapisReader_Byte(&r) > 1 || !LapisReader_Done(&r)) return 0;
        break;
    case 0x4B:
        if(LapisReader_VarInt(&r)!=0)return 0;
        LapisReader_String(&r,dimension,sizeof(dimension));LapisReader_Skip(&r,8);
        slot=LapisReader_Byte(&r);LapisReader_Byte(&r);
        if(LapisReader_Byte(&r)>1 || LapisReader_Byte(&r)>1)return 0;
        w=LapisReader_Byte(&r);
        if(w>1)return 0;
        if(w) { char death[128];LapisReader_String(&r,death,sizeof(death));LapisReader_Skip(&r,8); }
        LapisReader_VarInt(&r);LapisReader_VarInt(&r);LapisReader_Byte(&r);
        if(slot>3 || !LapisReader_Done(&r))return 0;
        strcpy(p->dimension,dimension);p->gamemode=slot;p->loaded=0;p->teleports=0;p->chunks=0;
        LapisGameplay_Init(g);break;
    default: return 2;
    }
    g->revision++; return 1;
}
int LapisGameplay_Move(struct LapisProtocol* p, double x, double y, double z, float yaw, float pitch, int ground) {
    struct Writer w; w.n = 0;
    if (!(x >= -32768 && x < 32768 && z >= -32768 && z < 32768 && y >= -64 && y <= 320 &&
          yaw >= -360 && yaw <= 360 && pitch >= -90 && pitch <= 90)) return 0;
    Double(&w, x); Double(&w, y); Double(&w, z); Float(&w, yaw); Float(&w, pitch); Big(&w, ground ? 1 : 0, 1);
    return Send(p, 0x1E, &w);
}
static void Sequence(struct Writer* w, struct LapisGameplay* g) {
    g->sequence = (g->sequence + 1) & 0x7FFFFFFF; Var(w, g->sequence);
}
int LapisGameplay_Dig(struct LapisGameplay* g, struct LapisProtocol* p, int action, int x, int y, int z, int face) {
    struct Writer w; w.n = 0;
    if (action < 0 || action > 6 || !Coords(x,y,z) || face < 0 || face > 5) return 0;
    Var(&w, (cc_uint32)action); Position(&w, x,y,z); Big(&w, (cc_uint64)face, 1); Sequence(&w, g);
    return Send(p, 0x28, &w);
}
int LapisGameplay_UseOn(struct LapisGameplay* g, struct LapisProtocol* p, int x, int y, int z, int face, float dx, float dy, float dz) {
    struct Writer w; w.n = 0;
    if (!Coords(x,y,z) || face < 0 || face > 5 || !(dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1 && dz >= 0 && dz <= 1)) return 0;
    Var(&w, 0); Position(&w, x,y,z); Var(&w, (cc_uint32)face);
    Float(&w, dx); Float(&w, dy); Float(&w, dz); Big(&w, 0, 2); Sequence(&w, g);
    return Send(p, 0x3F, &w);
}
int LapisGameplay_Use(struct LapisGameplay* g, struct LapisProtocol* p, float yaw, float pitch) {
    struct Writer w; w.n = 0;
    if (!(yaw >= -360 && yaw <= 360 && pitch >= -90 && pitch <= 90)) return 0;
    Var(&w, 0); Sequence(&w, g); Float(&w, yaw); Float(&w, pitch); return Send(p, 0x40, &w);
}
int LapisGameplay_Select(struct LapisProtocol* p, int slot) {
    struct Writer w; w.n = 0;
    if (slot < 0 || slot > 8) return 0;
    Big(&w, (cc_uint64)slot, 2); return Send(p, 0x34, &w);
}
static int ValidSlot(int window,int slot) {
    if(window!=0 && window!=2 && window!=12 && window!=14)return 0;
    return slot>=0 && slot<(window==2?63:window==14?39:46);
}
static int ClickMode(struct LapisGameplay* g,struct LapisProtocol* p,int slot,int button,int mode) {
    struct Writer w; w.n = 0;
    if (!ValidSlot(g->window,slot) && slot!=-999) return 0;
    if(g->window!=0 && g->window!=2 && g->window!=12 && g->window!=14)return 0;
    Var(&w, (cc_uint32)g->window); Var(&w, (cc_uint32)g->stateId[g->window]);
    Big(&w, (cc_uint16)slot, 2); Big(&w, (cc_uint64)button, 1); Var(&w, (cc_uint32)mode);
    /* Zero predictions and an absent HashedSlot. Lapis computes all mutations. */
    Var(&w, 0); Big(&w, 0, 1); return Send(p, 0x11, &w);
}
int LapisGameplay_Click(struct LapisGameplay* g,struct LapisProtocol* p,int slot,int right,int shift) {
    return ClickMode(g,p,slot,right?1:0,shift?1:0);
}
int LapisGameplay_Swap(struct LapisGameplay* g,struct LapisProtocol* p,int slot,int hotbar) {
    if(!ValidSlot(g->window,slot) || hotbar<0 || hotbar>8)return 0;
    return ClickMode(g,p,slot,hotbar,2);
}
int LapisGameplay_DropSlot(struct LapisGameplay* g,struct LapisProtocol* p,int slot,int entireStack) {
    if(!ValidSlot(g->window,slot))return 0;
    return ClickMode(g,p,slot,entireStack?1:0,4);
}
int LapisGameplay_Refresh(struct LapisGameplay* g,struct LapisProtocol* p) {
    struct Writer w;struct LapisStack stack;int i;w.n=0;
    if(g->window!=0 || g->health<=0 || p->gamemode==3)return 0;
    /* inventory_packets.c: a hotbar slot swapped with itself does nothing.
       Listed cached cells are untrusted snapshots; sync_window sends their
       actual contents. Only the four visible player-grid cells are requested;
       no assumption is made about successful return of all 3x3 ingredients. */
    Var(&w,0);Var(&w,(cc_uint32)g->stateId[0]);Big(&w,36,2);Big(&w,0,1);Var(&w,2);Var(&w,4);
    for(i=1;i<=4;i++) {
        stack=g->slots[0][i];Big(&w,(cc_uint64)i,2);Big(&w,stack.count?1:0,1);
        if(stack.count) { Var(&w,(cc_uint32)stack.item);Var(&w,(cc_uint32)stack.count);Var(&w,0);Var(&w,0); }
    }
    Big(&w,0,1);
    if(!Send(p,0x11,&w))return 0;
    g->refreshMask=31;return 1;
}
int LapisGameplay_Close(struct LapisGameplay* g, struct LapisProtocol* p) {
    struct Writer w; int ok; w.n = 0;
    Big(&w, (cc_uint64)g->window, 1); ok = Send(p, 0x12, &w);
    if(ok) { g->window=0;g->refreshMask=0; }return ok;
}
int LapisGameplay_Chat(struct LapisProtocol* p, const char* text, int length) {
    struct Writer w; int command; w.n = 0;
    if (length < 1 || length > 224 || !LapisText_Valid(text,length)) return 0;
    command = text[0] == '/';
    if (command) { text++; length--; if (!length) return 0; }
    Var(&w, (cc_uint32)length); memcpy(w.data+w.n, text, (size_t)length); w.n += length;
    if (!command) { memset(w.data+w.n, 0, 22); w.n += 22; }
    return Send(p, command ? 0x06 : 0x08, &w);
}
int LapisGameplay_Attack(struct LapisProtocol* p, int entity) {
    struct Writer w; w.n = 0;
    Var(&w,0);if(!Send(p,0x3C,&w))return 0; /* Main-hand animation is a separate server request. */
    w.n=0;
    Var(&w, (cc_uint32)entity); Var(&w, 1); Big(&w, 0, 1); return Send(p, 0x19, &w);
}
int LapisGameplay_Respawn(struct LapisProtocol* p) {
    struct Writer w; w.n = 0; Var(&w, 0); return Send(p, 0x0B, &w);
}
