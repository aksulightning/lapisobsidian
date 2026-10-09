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
static struct FontDesc font;
static int fontReady, fontSize, hover=-1, cell=44, left, top, header, footer, lineHeight, signWidth;
static int slotX[64],slotY[64];
static struct { struct Texture tex; char value[192]; } labels[99];
static void Empty(void* s) { (void)s; }
static void Update(void* s,float dt) { (void)s;(void)dt; }
static void Text(int index,const char* value,int x,int y) {
    struct DrawTextArgs args;cc_string text;
    static int lastWidth,lastHeight;
    int desired=Window_Main.Height<420 || Window_Main.Width<480?10:13;
    if(fontReady && (fontSize!=desired || lastWidth!=Window_Main.Width || lastHeight!=Window_Main.Height))LapisGui_ContextLost();
    lastWidth=Window_Main.Width;lastHeight=Window_Main.Height;
    if(!fontReady) { Font_Make(&font,desired,0);fontReady=1;fontSize=desired; }
    if(strcmp(labels[index].value,value) || !labels[index].tex.ID) {
        Gfx_DeleteTexture(&labels[index].tex.ID);
        strncpy(labels[index].value,value,191);labels[index].value[191]=0;
        text=String_FromReadonly(labels[index].value);DrawTextArgs_Make(&args,&text,&font,true);
        /* Keep long item names/sign text inside the viewport; wire text stays intact. */
        while(args.text.length && Drawer2D_TextWidth(&args)>Window_Main.Width-x-4)args.text.length--;
        Drawer2D_MakeTextTexture(&labels[index].tex,&args);
    }
    labels[index].tex.x=x;labels[index].tex.y=y;Texture_Render(&labels[index].tex);
}
void LapisGui_ContextLost(void) {
    int i;
    for(i=0;i<99;i++)Gfx_DeleteTexture(&labels[i].tex.ID);
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
    if(stack.count) { sprintf(count,"%d",stack.count);Text(slot,count,x+(size>24?size-20:1),y+(size>24?size-20:0)); }
}
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
    Text(64,g->window?g->title:"Inventory / crafting / armour",left,top-header+3);
    for(i=0;i<n;i++) {
        if(g->window==0 && i<=4 && (g->refreshMask&(1u<<i)))
            Gfx_Draw2DFlat(slotX[i],slotY[i],cell-2,cell-2,PackedCol_Make(31,49,62,235));
        else Slot(i,g->slots[g->window][i],slotX[i],slotY[i],cell,i==hover);
    }
    if(hover>=0 && hover<n && !(g->window==0 && g->refreshMask && hover<=4)) {
        sprintf(buffer,"Slot %d: %.28s",hover,LapisBlocks_ItemName(g->slots[g->window][hover].item));
        Text(65,buffer,left,top+cell*7+2);
    }
    Text(66,"Click: stack   Right: one",left,top+cell*7+2+lineHeight);
    Text(96,"Shift-click: transfer   1-9: hotbar",left,top+cell*7+2+lineHeight*2);
    Text(97,"Drop: one   Shift+Drop: stack",left,top+cell*7+2+lineHeight*3);
    if(g->refreshMask && !g->window)Text(98,"Updating crafting slots...",left,top+cell*7+2);
    if(g->cursor.count)Slot(67,g->cursor,Pointers[0].x+12,Pointers[0].y+12,cell,1);
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
void LapisGui_RenderHUD(void) {
    struct LapisGameplay* g=LapisBackend_Gameplay();char buffer[160];int i,x,y,size=40;
    if(!g || !LapisBackend_Protocol()->loaded || Game_HideGui)return;
    if(Gui_GetScreen(GUI_PRIORITY_INVENTORY)==&inventoryScreen)return;
    if(size>(Window_Main.Width-16)/9)size=(Window_Main.Width-16)/9;
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
    (void)screen;signWidth=Window_Main.Width-32;if(signWidth>420)signWidth=420;
    left=(Window_Main.Width-signWidth)/2;top=(Window_Main.Height-210)/2;
}
static void SignRender(void* screen,float dt) {
    int i;(void)screen;(void)dt;
    Gfx_Draw2DFlat(left-12,top-10,signWidth+24,220,PackedCol_Make(15,26,37,245));
    Text(80,LapisBackend_Signs()->front?"Edit sign: front":"Edit sign: back",left,top);
    for(i=0;i<4;i++) {
        Gfx_Draw2DFlat(left,top+28+i*30,signWidth,28,i==signLine?PackedCol_Make(93,83,53,255):PackedCol_Make(31,49,62,255));
        Text(81+i,signLines[i][0]?signLines[i]:" ",left+5,top+30+i*30);
    }
    Text(85,"Tab: next line   Enter: save   Escape: cancel",left,top+164);
    Text(86,"Up/Down: choose line   Backspace: erase",left,top+188);
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
