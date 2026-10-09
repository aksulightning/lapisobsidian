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

static struct Screen inventoryScreen;
static struct FontDesc font;
static int fontReady, hover=-1, cell=44, left, top;
static int slotX[64],slotY[64];
static struct { struct Texture tex; char value[192]; } labels[80];
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
    for(i=0;i<80;i++)Gfx_DeleteTexture(&labels[i].tex.ID);
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
    if(g->health<=0)Text(69,"You died. Press Enter to respawn.",20,Window_Main.Height/2);
}
