/* Shared label/slot rendering and the survival HUD; owns cached GPU text. */
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

static struct FontDesc font;
static int fontReady, fontSize;
static struct { struct Texture tex; char value[192]; } labels[99];
void LapisGui_Text(int index,const char* value,int x,int y) {
    struct DrawTextArgs args;cc_string text;
    static int lastWidth,lastHeight;
    int desired=Window_Main.Height<420 || Window_Main.Width<480?10:13;
    /* Cached textures include viewport-dependent clipping, so resize invalidates
     * unchanged labels too, even when the font size remains the same. */
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
/* Unknown items or an unavailable atlas draw no icon; count/slot UI still works. */
static void Icon(int item,int x,int y,int size) {
    struct Texture tex;int atlas,icon=LapisBlocks_ItemIcon(item);
    if(!item || icon<0 || !Atlas1D.Count)return;
    tex.uv=Atlas1D_TexRec((TextureLoc)icon,1,&atlas);tex.ID=Atlas1D.TexIds[atlas];
    tex.x=x;tex.y=y;tex.width=size;tex.height=size;Texture_Render(&tex);
}
/* slot selects a text-cache entry, not an inventory lookup; stack is supplied
 * by the caller so HUD and container rendering share the same primitive. */
static void SlotContents(int slot,struct LapisStack stack,int x,int y,int size) {
    char count[24];
    Icon(stack.item,x+3,y+3,size-10);
    if(stack.count) { sprintf(count,"%d",stack.count);LapisGui_Text(slot,count,x+(size>24?size-20:1),y+(size>24?size-20:0)); }
}
/* Draw a fallback slot background plus contents; active controls emphasis only. */
void LapisGui_Slot(int slot,struct LapisStack stack,int x,int y,int size,int active) {
    Gfx_Draw2DFlat(x,y,size-2,size-2,active?PackedCol_Make(118,101,64,235):PackedCol_Make(31,49,62,235));
    SlotContents(slot,stack,x,y,size);
}
/* Return false when GUI textures are unavailable so HUD can draw flat slots. */
static int HotbarBackground(int x,int y,int size) {
    struct Texture tex;int border=(size+19)/20;
    tex.ID=Gui.ClassicTexture?Gui.GuiClassicTex:Gui.GuiTex;
    if(!tex.ID)return false;
    /* Gui loads only the top half of the 256x256 source sheet. */
    Tex_SetRect(tex,x-border,y-border,9*size+2*border,size+2*border);
    Tex_SetUV(tex,0,0,182/256.0f,22/128.0f);
    Texture_Render(&tex);
    Tex_SetRect(tex,x+Inventory.SelectedIndex*size-2*border,y-2*border,size+4*border,size+4*border);
    Tex_SetUV(tex,0,22/128.0f,24/256.0f,46/128.0f);
    Texture_Render(&tex);
    return true;
}
/* Each icon represents two points. Clipping the foreground preserves half
   hearts/food and fractional health without changing authoritative values. */
static void Vitals(float value,int food,int x,int y,int step) {
    struct Texture tex;int i,width,extent=step-1;
    float fill;
    if(value<0)value=0;if(value>20)value=20;
    tex.ID=Gui.ClassicTexture?Gui.GuiClassicTex:Gui.GuiTex;
    for(i=0;i<10;i++) {
        fill=(value-(float)(i*2))/2;if(fill<0)fill=0;if(fill>1)fill=1;
        width=(int)(extent*fill+.5f);
        if(tex.ID) {
            Tex_SetRect(tex,x+i*step,y,extent,extent);
            Tex_SetUV(tex,(food?32:0)/256.0f,48/128.0f,(food?48:16)/256.0f,64/128.0f);
            Texture_Render(&tex);
            if(!width)continue;
            Tex_SetRect(tex,x+i*step,y,width,extent);
            Tex_SetUV(tex,(food?48:16)/256.0f,48/128.0f,((food?48:16)+16*fill)/256.0f,64/128.0f);
            Texture_Render(&tex);
        } else {
            Gfx_Draw2DFlat(x+i*step,y,extent,extent,PackedCol_Make(33,34,40,240));
            if(width)Gfx_Draw2DFlat(x+i*step,y,width,extent,food?PackedCol_Make(208,148,72,255):PackedCol_Make(212,68,76,255));
        }
    }
}
/* Read backend state without predicting inventory/vitals. Cache IDs 68..78 and
 * 87..95 separate HUD labels from inventory and sign-editor text. */
void LapisGui_RenderHUD(void) {
    struct LapisGameplay* g=LapisBackend_Gameplay();char buffer[160];int i,x,y,textured,size=40;
    if(!g || !LapisBackend_Protocol()->loaded || Game_HideGui)return;
    if(LapisGui_InventoryOpen())return;
    if(size>(Window_Main.Width-16)/9)size=(Window_Main.Width-16)/9;
    x=(Window_Main.Width-9*size)/2;y=Window_Main.Height-size-6;
    Gfx_Draw2DFlat(Window_Main.Width/2-5,Window_Main.Height/2,11,1,PackedCol_Make(245,245,230,255));
    Gfx_Draw2DFlat(Window_Main.Width/2,Window_Main.Height/2-5,1,11,PackedCol_Make(245,245,230,255));
    textured=HotbarBackground(x,y,size);
    for(i=0;i<9;i++) {
        if(textured)SlotContents(70+i,g->slots[0][36+i],x+i*size,y,size);
        else LapisGui_Slot(70+i,g->slots[0][36+i],x+i*size,y,size,i==Inventory.SelectedIndex);
    }
    Vitals(g->health,0,x,y-size/2-7,size*4/10);
    Vitals((float)g->food,1,x+size*9-size*4/10*10,y-size/2-7,size*4/10);
    sprintf(buffer,"%s",LapisBlocks_ItemName(g->slots[0][36+Inventory.SelectedIndex].item));
    LapisGui_Text(68,buffer,x,y-size/2-32);
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
                LapisGui_Text(87+line,text,20,56+line*22);
            }
            LapisGui_Text(95,"Sign: front / back",20,34);
        }
    }
    if(g->health<=0)LapisGui_Text(69,"You died. Press Enter to respawn.",20,Window_Main.Height/2);
}
