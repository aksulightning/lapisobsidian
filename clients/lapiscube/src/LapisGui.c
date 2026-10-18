/* Inventory and sign editor screens, layout and input handling. */
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
static int touchClickMode;
void LapisGui_TouchClickMode(int mode) { touchClickMode=mode>=0 && mode<=2?mode:0; }
static char signLines[4][97];
static int signLine,signDirty[4];
static int hover=-1, cell=44, left, top, header, footer, lineHeight, signWidth;
static int slotX[64],slotY[64];
static void Empty(void* s) { (void)s; }
static void Update(void* s,float dt) { (void)s;(void)dt; }
static int Slots(void) {
    int window=LapisBackend_Gameplay()->window;
    return window==2?63:window==14?39:46;
}
static void Layout(void* s) {
    int i,w=LapisBackend_Gameplay()->window,row,col;(void)s;
    lineHeight=Window_Main.Height<420 || Window_Main.Width<480?15:21;
    header=lineHeight+10;footer=lineHeight*4+8;
    cell=(Window_Main.Width-24)/9;
    if(cell>(Window_Main.Height-header-footer-12)/7)cell=(Window_Main.Height-header-footer-12)/7;
    if(cell>48)cell=48;if(cell<16)cell=16;
    left=(Window_Main.Width-cell*9)/2;
    top=(Window_Main.Height-cell*7-header-footer)/2+header;
    hover=-1;
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
    Gfx_Draw2DFlat(left-12,top-header,cell*9+24,cell*7+header+footer,PackedCol_Make(15,26,37,255));
    LapisGui_Text(64,g->window?g->title:"Inventory / crafting / armour",left,top-header+3);
    for(i=0;i<n;i++) {
        if(g->window==0 && i<=4 && (g->refreshMask&(1u<<i)))
            Gfx_Draw2DFlat(slotX[i],slotY[i],cell-2,cell-2,PackedCol_Make(31,49,62,235));
        else LapisGui_Slot(i,g->slots[g->window][i],slotX[i],slotY[i],cell,i==hover);
    }
    if(hover>=0 && hover<n && !(g->window==0 && g->refreshMask && hover<=4)) {
        sprintf(buffer,"Slot %d: %.28s",hover,LapisBlocks_ItemName(g->slots[g->window][hover].item));
        LapisGui_Text(65,buffer,left,top+cell*7+2);
    }
    LapisGui_Text(66,"Click: stack   Right: one",left,top+cell*7+2+lineHeight);
    LapisGui_Text(96,"Shift-click: transfer   1-9: hotbar",left,top+cell*7+2+lineHeight*2);
    LapisGui_Text(97,"Drop: one   Shift+Drop: stack",left,top+cell*7+2+lineHeight*3);
    if(g->refreshMask && !g->window)LapisGui_Text(98,"Updating crafting slots...",left,top+cell*7+2);
    if(g->cursor.count)LapisGui_Slot(67,g->cursor,Pointers[0].x+12,Pointers[0].y+12,cell,1);
}
static int PointerMove(void* s,int id,int x,int y) {
    int i;(void)s;(void)id;hover=-1;
    for(i=0;i<Slots();i++)if(x>=slotX[i] && x<slotX[i]+cell && y>=slotY[i] && y<slotY[i]+cell)hover=i;
    return true;
}
static void Click(int right) {
    struct LapisGameplay* g=LapisBackend_Gameplay();
    if(!g->window && g->refreshMask && hover>=0 && hover<=4)return;
    if(hover>=0 && hover<Slots())
        LapisGameplay_Click(LapisBackend_Gameplay(),LapisBackend_Protocol(),hover,right,Input_IsShiftPressed() || touchClickMode==2);
}
static int PointerDown(void* s,int id,int x,int y) {
    PointerMove(s,id,x,y);Click(touchClickMode==1);return true;
}
static int KeyDown(void* s,int key,struct InputDevice* device) {
    struct LapisGameplay* g=LapisBackend_Gameplay();struct LapisProtocol* p=LapisBackend_Protocol();(void)s;
    if(hover>=0 && hover<Slots() && !(g->window==0 && g->refreshMask && hover<=4)) {
        if(key>=CCKEY_1 && key<=CCKEY_9) { LapisGameplay_Swap(g,p,hover,key-CCKEY_1);return true; }
        if(InputBind_Claims(BIND_DROP_BLOCK,key,device)) { LapisGameplay_DropSlot(g,p,hover,Input_IsShiftPressed());return true; }
    }
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
    if(!LapisBackend_Gameplay()->window)LapisGameplay_Refresh(LapisBackend_Gameplay(),LapisBackend_Protocol());
    inventoryScreen.VTABLE=&table;inventoryScreen.grabsInput=true;inventoryScreen.closable=true;
    hover=-1;Gui_Add(&inventoryScreen,GUI_PRIORITY_INVENTORY);
}
void LapisGui_Close(void) {
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&inventoryScreen)Gui_Remove(&inventoryScreen);
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&signScreen)Gui_Remove(&signScreen);
}
static void SignLayout(void* screen) {
    (void)screen;signWidth=Window_Main.Width-32;if(signWidth>420)signWidth=420;
    left=(Window_Main.Width-signWidth)/2;top=(Window_Main.Height-210)/2;
}
static void SignRender(void* screen,float dt) {
    int i;(void)screen;(void)dt;
    Gfx_Draw2DFlat(left-12,top-10,signWidth+24,220,PackedCol_Make(15,26,37,245));
    LapisGui_Text(80,LapisBackend_Signs()->front?"Edit sign: front":"Edit sign: back",left,top);
    for(i=0;i<4;i++) {
        Gfx_Draw2DFlat(left,top+28+i*30,signWidth,28,i==signLine?PackedCol_Make(93,83,53,255):PackedCol_Make(31,49,62,255));
        LapisGui_Text(81+i,signLines[i][0]?signLines[i]:" ",left+5,top+30+i*30);
    }
    LapisGui_Text(85,"Tab: next line   Enter: save   Escape: cancel",left,top+164);
    LapisGui_Text(86,"Up/Down: choose line   Backspace: erase",left,top+188);
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
    int i;
#ifdef CC_BUILD_WEB
    signLines[signLine][0]=0;signDirty[signLine]=1;
#endif
    for(i=0;i<value->length;i++)SignChar(screen,value->buffer[i]);return true;
}
static int SignPointer(void* screen,int id,int x,int y) {
    (void)screen;(void)id;
    if(x>=left && x<left+signWidth && y>=top+28 && y<top+148) {
        signLine=(y-top-28)/30;
#ifdef CC_BUILD_WEB
        {
            struct OpenKeyboardArgs args;cc_string text=String_FromReadonly(signLines[signLine]);
            OpenKeyboardArgs_Init(&args,&text,KEYBOARD_TYPE_TEXT);
            args.placeholder="Sign line";OnscreenKeyboard_Open(&args);
        }
#endif
    }
    return true;
}
static void SignFree(void* screen) {
    (void)screen;LapisBackend_Signs()->editing=0;
#ifdef CC_BUILD_WEB
    OnscreenKeyboard_Close();
#endif
}
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

int LapisGui_InventoryOpen(void) {
    return Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&inventoryScreen;
}
