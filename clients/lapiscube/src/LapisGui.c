#include "LapisGui.h"
#include "LapisBackend.h"
#include "LapisBlocks.h"
#include "Gui.h"
#include "Graphics.h"
#include "Drawer2D.h"
#include "TexturePack.h"
#include "Window.h"
#include "Input.h"
#include "InputHandler.h"
#include "Inventory.h"
#include "Game.h"
#include "String_.h"
#include <stdio.h>
#include <string.h>

static struct Screen inventoryScreen, signScreen;
static char signLines[4][97];
static int signLine,signDirty[4];
static struct FontDesc font;
static int fontReady, hover=-1, cell=44, left, top;
static int slotX[64],slotY[64];
static struct { struct Texture tex; char value[192]; } labels[96];
static void Empty(void* s) { (void)s; }
static void Update(void* s,float dt) { (void)s;(void)dt; }
static void Text(int index,const char* value,int x,int y) {
    struct DrawTextArgs args;cc_string text;
    if(!fontReady) { Font_Make(&font,13,0);fontReady=1; }
    if(strcmp(labels[index].value,value) || !labels[index].tex.ID) {
        Gfx_DeleteTexture(&labels[index].tex.ID);
        strncpy(labels[index].value,value,191);labels[index].value[191]=0;
        text=String_FromReadonly(labels[index].value);DrawTextArgs_Make(&args,&text,&font,true);
        Drawer2D_MakeTextTexture(&labels[index].tex,&args);
    }
    labels[index].tex.x=x;labels[index].tex.y=y;Texture_Render(&labels[index].tex);
}
void LapisGui_ContextLost(void) {
    int i;
    for(i=0;i<96;i++)Gfx_DeleteTexture(&labels[i].tex.ID);
    if(fontReady)Font_Free(&font);fontReady=0;
}
static void Icon(int item,int x,int y,int size) {
    struct Texture tex;int atlas,icon=LapisBlocks_ItemIcon(item);
    if(!item || icon<0 || !Atlas1D.Count)return;
    tex.uv=Atlas1D_TexRec((TextureLoc)icon,1,&atlas);tex.ID=Atlas1D.TexIds[atlas];
    tex.x=x;tex.y=y;tex.width=size;tex.height=size;Texture_Render(&tex);
}
static void Slot(int slot,struct LapisStack stack,int x,int y,int size,int active) {
    char count[24];
    Gfx_Draw2DFlat(x,y,size-2,size-2,active?PackedCol_Make(118,101,64,235):PackedCol_Make(31,49,62,235));
    Icon(stack.item,x+3,y+3,size-10);
    if(stack.count) { sprintf(count,"%d",stack.count);Text(slot,count,x+size-20,y+size-20); }
}
static int Slots(void) {
    int window=LapisBackend_Gameplay()->window;
    return window==2?63:window==14?39:46;
}
static void Layout(void* s) {
    int i,w=LapisBackend_Gameplay()->window,row,col;(void)s;
    cell=Window_Main.Width>=700 && Window_Main.Height>=500?48:36;
    left=(Window_Main.Width-cell*9)/2;top=(Window_Main.Height-cell*7)/2;
    if(top<36)top=36;
    for(i=0;i<Slots();i++) {
        if(w==2) { row=i/9;col=i%9; }
        else if(w==12) {
            if(!i) { col=5;row=1; }
            else if(i<=9) { col=(i-1)%3+1;row=(i-1)/3; }
            else { col=(i-10)%9;row=(i-10)/9+3; }
        } else if(w==14) {
            if(i<3) { col=i==2?5:3;row=i==2?1:i*2; }
            else { col=(i-3)%9;row=(i-3)/9+3; }
        } else {
            if(!i) { col=6;row=1; }
            else if(i<=4) { col=(i-1)%2+3;row=(i-1)/2; }
            else if(i<=8) { col=(i-5)%2;row=(i-5)/2; }
            else if(i==45) { col=8;row=1; }
            else { col=(i-9)%9;row=(i-9)/9+3; }
        }
        slotX[i]=left+col*cell;slotY[i]=top+row*cell;
    }
}
static void Render(void* s,float dt) {
    struct LapisGameplay* g=LapisBackend_Gameplay();char buffer[192];int i,n=Slots();
    (void)s;(void)dt;
    Gfx_Draw2DFlat(left-12,top-32,cell*9+22,cell*7+82,PackedCol_Make(15,26,37,240));
    Text(64,g->window?g->title:"Inventory / crafting / armour",left,top-26);
    for(i=0;i<n;i++)Slot(i,g->slots[g->window][i],slotX[i],slotY[i],cell,i==hover);
    if(hover>=0 && hover<n) {
        sprintf(buffer,"Slot %d: %.28s",hover,LapisBlocks_ItemName(g->slots[g->window][hover].item));
        Text(65,buffer,left,top+cell*7+2);
    }
    Text(66,"Left: stack   Right: one   Shift: transfer",left,top+cell*7+24);
    if(g->cursor.count)Slot(67,g->cursor,Pointers[0].x+12,Pointers[0].y+12,cell,1);
}
static int PointerMove(void* s,int id,int x,int y) {
    int i;(void)s;(void)id;hover=-1;
    for(i=0;i<Slots();i++)if(x>=slotX[i] && x<slotX[i]+cell && y>=slotY[i] && y<slotY[i]+cell)hover=i;
    return true;
}
static void Click(int right) {
    if(hover>=0 && hover<Slots())
        LapisGameplay_Click(LapisBackend_Gameplay(),LapisBackend_Protocol(),hover,right,Input_IsShiftPressed());
}
static int PointerDown(void* s,int id,int x,int y) {
    PointerMove(s,id,x,y);Click(0);return true;
}
static int KeyDown(void* s,int key,struct InputDevice* device) {
    (void)s;
    if(key==CCMOUSE_R) { Click(1);return true; }
    if(InputBind_Claims(BIND_INVENTORY,key,device) || key==CCKEY_ESCAPE) { LapisGui_Close();return true; }
    return true;
}
static int KeyChar(void* s,char key) { (void)s;(void)key;return true; }
static int KeyText(void* s,const cc_string* text) { (void)s;(void)text;return true; }
static int Scroll(void* s,float amount) { (void)s;(void)amount;return true; }
static void Free(void* s) {
    (void)s;LapisGameplay_Close(LapisBackend_Gameplay(),LapisBackend_Protocol());
}
static const struct ScreenVTABLE table={
    Empty,Update,Free,Render,Empty,KeyDown,Screen_InputUp,KeyChar,KeyText,
    PointerDown,Screen_PointerUp,PointerMove,Scroll,Layout,Empty,Empty,NULL
};
void LapisGui_ShowInventory(void) {
    if(!LapisBackend_Protocol() || !LapisBackend_Protocol()->loaded)return;
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&inventoryScreen) { Layout(&inventoryScreen);return; }
    inventoryScreen.VTABLE=&table;inventoryScreen.grabsInput=true;inventoryScreen.closable=true;
    hover=-1;Gui_Add(&inventoryScreen,GUI_PRIORITY_INVENTORY);
}
void LapisGui_Close(void) {
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&inventoryScreen)Gui_Remove(&inventoryScreen);
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&signScreen)Gui_Remove(&signScreen);
}
void LapisGui_RenderHUD(void) {
    struct LapisGameplay* g=LapisBackend_Gameplay();char buffer[160];int i,x,y,size=40;
    if(!g || !LapisBackend_Protocol()->loaded || Game_HideGui)return;
    x=(Window_Main.Width-9*size)/2;y=Window_Main.Height-size-6;
    Gfx_Draw2DFlat(Window_Main.Width/2-5,Window_Main.Height/2,11,1,PackedCol_Make(245,245,230,255));
    Gfx_Draw2DFlat(Window_Main.Width/2,Window_Main.Height/2-5,1,11,PackedCol_Make(245,245,230,255));
    for(i=0;i<9;i++)Slot(70+i,g->slots[0][36+i],x+i*size,y,size,i==Inventory.SelectedIndex);
    sprintf(buffer,"Health %.0f/20    Food %d/20    %s",g->health,g->food,LapisBlocks_ItemName(g->slots[0][36+Inventory.SelectedIndex].item));
    Text(68,buffer,x,y-26);
    {
        float progress=LapisBackend_DigProgress();int mid=Window_Main.Width/2;
        if(progress>=0) {
            Gfx_Draw2DFlat(mid-42,Window_Main.Height/2+18,84,8,PackedCol_Make(15,26,37,235));
            Gfx_Draw2DFlat(mid-40,Window_Main.Height/2+20,(int)(80*progress),4,PackedCol_Make(131,194,205,255));
        }
    }
    {
        struct LapisSign* sign=LapisBackend_TargetSign();int line;char text[192];cc_string converted;
        if(sign && !Gui.InputGrab) {
            Gfx_Draw2DFlat(12,32,330,214,PackedCol_Make(15,26,37,225));
            for(line=0;line<8;line++) {
                converted=String_Init(text,0,191);
                String_AppendUtf8(&converted,sign->lines[line/4][line%4],(int)strlen(sign->lines[line/4][line%4]));text[converted.length]=0;
                Text(87+line,text,20,56+line*22);
            }
            Text(95,"Sign: front / back",20,34);
        }
    }
    if(g->health<=0)Text(69,"You died. Press Enter to respawn.",20,Window_Main.Height/2);
}

static void SignLayout(void* screen) {
    (void)screen;left=(Window_Main.Width-420)/2;top=(Window_Main.Height-230)/2;
}
static void SignRender(void* screen,float dt) {
    int i;(void)screen;(void)dt;
    Gfx_Draw2DFlat(left-12,top-12,444,246,PackedCol_Make(15,26,37,245));
    Text(80,LapisBackend_Signs()->front?"Edit sign: front":"Edit sign: back",left,top);
    for(i=0;i<4;i++) {
        Gfx_Draw2DFlat(left,top+32+i*34,420,30,i==signLine?PackedCol_Make(93,83,53,255):PackedCol_Make(31,49,62,255));
        Text(81+i,signLines[i][0]?signLines[i]:" ",left+5,top+35+i*34);
    }
    Text(85,"Tab: next line   Enter: save   Escape: cancel",left,top+184);
    Text(86,"Up/Down: choose line   Backspace: erase",left,top+208);
}
static int SignDown(void* screen,int key,struct InputDevice* device) {
    struct LapisSigns* signs=LapisBackend_Signs();char lines[4][97];int i,n,side=signs->front?0:1;
    cc_uint8 utf8[291];cc_string value;(void)screen;(void)device;
    if(key==CCKEY_ESCAPE) { LapisGui_Close();return true; }
    if(key==CCKEY_TAB || key==CCKEY_DOWN)signLine=(signLine+1)%4;
    if(key==CCKEY_UP)signLine=(signLine+3)%4;
    if(key==CCKEY_BACKSPACE) { n=(int)strlen(signLines[signLine]);if(n) { signLines[signLine][n-1]=0;signDirty[signLine]=1; } }
    if(key==CCKEY_ENTER) {
        for(i=0;i<4;i++) {
            /* Preserve untouched Unicode, even outside the engine's CP437 font. */
            if(!signDirty[i]) { strcpy(lines[i],signs->edit.lines[side][i]);continue; }
            value=String_FromReadonly(signLines[i]);n=String_EncodeUtf8(utf8,&value);
            if(n>96)return true;
            memcpy(lines[i],utf8,(size_t)n);lines[i][n]=0;
        }
        if(LapisSigns_Submit(signs,LapisBackend_Protocol(),lines))LapisGui_Close();
    }
    return true;
}
static int SignChar(void* screen,char key) {
    int n,bytes;cc_uint8 utf8[291];cc_string value;(void)screen;
    if((unsigned char)key<32 || key==127)return true;
    n=(int)strlen(signLines[signLine]);if(n>=96)return true;
    signLines[signLine][n]=key;signLines[signLine][n+1]=0;
    value=String_FromReadonly(signLines[signLine]);bytes=String_EncodeUtf8(utf8,&value);
    if(bytes>96)signLines[signLine][n]=0;
    else signDirty[signLine]=1;
    return true;
}
static int SignText(void* screen,const cc_string* value) {
    int i;for(i=0;i<value->length;i++)SignChar(screen,value->buffer[i]);return true;
}
static int SignPointer(void* screen,int id,int x,int y) {
    (void)screen;(void)id;
    if(x>=left && x<left+420 && y>=top+32 && y<top+168)signLine=(y-top-32)/34;
    return true;
}
static void SignFree(void* screen) { (void)screen;LapisBackend_Signs()->editing=0; }
static int SignMove(void* screen,int id,int x,int y) { (void)screen;(void)id;(void)x;(void)y;return true; }
static const struct ScreenVTABLE signTable={
    Empty,Update,SignFree,SignRender,Empty,SignDown,Screen_InputUp,SignChar,SignText,
    SignPointer,Screen_PointerUp,SignMove,Scroll,SignLayout,Empty,Empty,NULL
};
void LapisGui_ShowSign(void) {
    struct LapisSigns* signs=LapisBackend_Signs();cc_string value;int i,side=signs->front?0:1;
    if(!signs->editing)return;
    LapisGui_Close();signs->editing=1;signLine=0;
    for(i=0;i<4;i++) {
        signDirty[i]=0;
        value=String_Init(signLines[i],0,96);
        String_AppendUtf8(&value,signs->edit.lines[side][i],(int)strlen(signs->edit.lines[side][i]));signLines[i][value.length]=0;
    }
    signScreen.VTABLE=&signTable;signScreen.grabsInput=true;signScreen.closable=true;
    Gui_Add(&signScreen,GUI_PRIORITY_INVENTORY);
}
