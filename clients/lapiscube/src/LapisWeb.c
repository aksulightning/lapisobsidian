#include "Core.h"
#ifdef CC_BUILD_WEB
/* Small bridge from browser controls to the same native input/gameplay paths. */
#include "Input.h"
#include "InputHandler.h"
#include "Event.h"
#include "Gui.h"
#include "Window.h"
#include "Inventory.h"
#include "LapisBackend.h"
#include "LapisGui.h"
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE void LapisWeb_Reset(void) { Input_Clear(); }
EMSCRIPTEN_KEEPALIVE void LapisWeb_Action(int action,int down) {
    static const int keys[]={ 'W','A','S','D',CCKEY_SPACE,CCKEY_LCTRL,CCKEY_LSHIFT,
        CCMOUSE_L,CCMOUSE_R,'B','G','T',CCKEY_ESCAPE,CCKEY_ENTER,'Z','Q','E' };
    if(action>=0 && action<(int)(sizeof(keys)/sizeof(keys[0]))) Input_SetNonRepeatable(keys[action],down);
    else if(action>=20 && action<29 && down) Inventory_SetSelectedIndex(action-20);
}
EMSCRIPTEN_KEEPALIVE void LapisWeb_Look(float dx,float dy) {
    if(!Gui.InputGrab) Event_RaiseRawMove(&PointerEvents.RawMoved,dx,dy);
}
EMSCRIPTEN_KEEPALIVE void LapisWeb_Pointer(int x,int y,int down) {
    Pointer_SetPosition(0,x,y);Input_SetNonRepeatable(CCMOUSE_L,down);
}
EMSCRIPTEN_KEEPALIVE void LapisWeb_ClickMode(int mode) { LapisGui_TouchClickMode(mode); }
EMSCRIPTEN_KEEPALIVE int LapisWeb_State(void) {
    return (LapisBackend_Protocol()->loaded?1:0) | (Gui.InputGrab?2:0) |
        (LapisBackend_Gameplay()->health<=0?4:0);
}
#endif
