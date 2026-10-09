#include "LapisMobs.h"
#include "LapisBlocks.h"
#include "Model.h"
#include "Entity.h"
#include "Graphics.h"
#include "Event.h"
#include "String_.h"
#include "ExtMath.h"
#include "Game.h"
#include "Picking.h"
#include "Platform.h"
#include "Options.h"
#include <string.h>

/* Original simple geometric models, independently dimensioned and CC0.
   Species use independent proportions, details and coloured geometry. */
#define PARTS 20
struct Shape {
    struct Model model;struct ModelVertex vertices[PARTS*24];struct ModelPart parts[PARTS];
    int count; int motion[PARTS]; PackedCol tint[PARTS];float width,height,length;PackedCol color;
};
static struct Shape shapes[10];
static struct ModelTex texture={"lapis-entities.png",0,NULL,NULL};
static unsigned seen[LAPIS_MAX_ENTITIES];
static int wireIds[LAPIS_MAX_ENTITIES];
static int registered,lastX,lastZ,effects;
static struct { unsigned hurt,swing; cc_uint64 hurtAt,swingAt; int mainHand,sheep,fuse,dead; } visual[LAPIS_MAX_ENTITIES];
static cc_uint64 Clock(void) { return Stopwatch_ElapsedMicroseconds(0,Stopwatch_Measure())/1000; }
static void Detail(struct Shape* s,int r,int g,int b) { s->tint[s->count-1]=PackedCol_Make(r,g,b,255); }
static void Legs(struct Shape* s,int first,int end) {
    int i;for(i=first;i<end;i++)s->motion[i]=(i&1)?1:2;
}
static struct Shape* Current(void) { return (struct Shape*)Models.Active; }
static void Box(struct Shape* s,float x,float y,float z,float w,float h,float d) {
    struct BoxDesc b;
    memset(&b,0,sizeof(b));b.sizeX=b.sizeY=b.sizeZ=1;
    b.x1=x;b.y1=y;b.z1=z;b.x2=x+w;b.y2=y+h;b.z2=z+d;b.rotX=x+w/2;b.rotY=y+h;b.rotZ=z+d/2;
    BoxDesc_BuildBox(&s->parts[s->count++],&b);
}
static void Make(void) {
    struct Shape* s=Current();int i,kind=(int)(s-shapes);float x,z;
    s->count=0;memset(s->motion,0,sizeof(s->motion));memset(s->tint,0,sizeof(s->tint));
    if(kind==0 || kind==8) { /* traveller / broad-shouldered, outstretched zombie */
        Box(s,-.22f,.64f,-.14f,.44f,.57f,.28f);Box(s,-.23f,1.21f,-.21f,.46f,.44f,.42f);
        Box(s,-.23f,0,-.13f,.17f,.64f,.26f);Box(s,.06f,0,-.13f,.17f,.64f,.26f);Legs(s,2,4);
        if(kind==8) {
            Box(s,-.4f,1.00f,-.60f,.17f,.18f,.59f);Box(s,.23f,1.00f,-.60f,.17f,.18f,.59f);
        } else {
            Box(s,-.37f,.62f,-.12f,.14f,.58f,.24f);Box(s,.23f,.62f,-.12f,.14f,.58f,.24f);
            s->motion[4]=3;s->motion[5]=4;
        }
        if(kind==8) { s->tint[0]=PackedCol_Make(62,83,107,255);s->tint[2]=s->tint[3]=s->tint[0]; }
        Box(s,-.16f,1.48f,-.224f,.08f,.055f,.02f);Detail(s,35,40,32);
        Box(s,.08f,1.48f,-.224f,.08f,.055f,.02f);Detail(s,35,40,32);
    } else if(kind==1 || kind==6 || kind==7) {
        if(kind==6) { /* squat pig, snout and triangular impression from stepped ears */
            Box(s,-.36f,.22f,-.45f,.72f,.42f,.92f);Box(s,-.24f,.34f,-.71f,.48f,.37f,.37f);
        } else {
            Box(s,-.30f,.38f,-.47f,.60f,.43f,.92f);Box(s,-.23f,.69f,-.77f,.46f,.38f,.38f);
        }
        for(i=0;i<4;i++) { x=(i&1)?.17f:-.31f;z=(i&2)?.25f:-.42f;Box(s,x,0,z,.14f,kind==6?.25f:.4f,.17f); }
        Legs(s,2,6);
        if(kind==1) {
            Box(s,-.32f,1.02f,-.66f,.09f,.22f,.09f);Detail(s,218,195,140);
            Box(s,.23f,1.02f,-.66f,.09f,.22f,.09f);Detail(s,218,195,140);
            Box(s,-.32f,.77f,-.36f,.3f,.055f,.48f);Detail(s,219,209,179);
            Box(s,-.20f,.71f,-.82f,.40f,.16f,.08f);Detail(s,184,145,112);
        } else if(kind==6) {
            Box(s,-.15f,.40f,-.80f,.3f,.15f,.12f);Detail(s,179,97,104);
            Box(s,-.25f,.66f,-.64f,.11f,.14f,.15f);Box(s,.14f,.66f,-.64f,.11f,.14f,.15f);
        } else { /* removable wool coat, distinguishable from cow even when sheared */
            Box(s,-.39f,.31f,-.54f,.78f,.65f,1.05f);Detail(s,225,223,202);
            Box(s,-.33f,.84f,-.70f,.17f,.08f,.16f);Box(s,.16f,.84f,-.70f,.17f,.08f,.16f);
        }
    } else if(kind==2) {
        Box(s,-.23f,.15f,-.23f,.46f,.36f,.46f);Box(s,-.16f,.42f,-.34f,.32f,.31f,.3f);
        Box(s,-.13f,0,-.07f,.07f,.2f,.1f);Box(s,.06f,0,-.07f,.07f,.2f,.1f);Legs(s,2,4);
        Box(s,-.1f,.45f,-.46f,.2f,.09f,.15f);Detail(s,219,163,57);
        Box(s,-.3f,.3f,-.14f,.08f,.15f,.28f);Box(s,.22f,.3f,-.14f,.08f,.15f,.28f);
        Box(s,-.04f,.72f,-.24f,.08f,.12f,.20f);Detail(s,191,67,60);
    } else if(kind==3) {
        Box(s,-.38f,.17f,-.15f,.76f,.38f,.65f);Box(s,-.27f,.23f,-.46f,.54f,.28f,.34f);
        for(i=0;i<8;i++)Box(s,(i&1)?.33f:-.81f,.12f,-.38f+(float)(i/2)*.24f,.48f,.1f,.1f);
        Legs(s,2,10);
        Box(s,-.2f,.36f,-.48f,.12f,.08f,.03f);Detail(s,202,81,48);
        Box(s,.08f,.36f,-.48f,.12f,.08f,.03f);Detail(s,202,81,48);
    } else if(kind==5) {
        Box(s,-.24f,.28f,-.17f,.48f,.7f,.34f);Box(s,-.27f,.98f,-.27f,.54f,.48f,.54f);
        for(i=0;i<4;i++)Box(s,(i&1)?.07f:-.26f,0,(i&2)?.1f:-.26f,.19f,.3f,.19f);Legs(s,2,6);
        Box(s,-.19f,1.23f,-.29f,.38f,.045f,.03f);Detail(s,29,62,54);
        Box(s,-.09f,1.08f,-.29f,.18f,.07f,.03f);Detail(s,29,62,54);
    } else if(kind==9) { /* narrow skeleton, visible ribs and an independently built bow */
        Box(s,-.07f,.58f,-.07f,.14f,.65f,.14f);Box(s,-.19f,1.22f,-.16f,.38f,.38f,.32f);
        Box(s,-.18f,0,-.06f,.085f,.63f,.12f);Box(s,.095f,0,-.06f,.085f,.63f,.12f);Legs(s,2,4);
        Box(s,-.31f,.66f,-.06f,.09f,.53f,.12f);Box(s,.22f,.66f,-.06f,.09f,.53f,.12f);
        s->motion[4]=3;s->motion[5]=4;
        for(i=0;i<3;i++)Box(s,-.24f,.83f+(float)i*.13f,-.09f,.48f,.055f,.18f);
        Box(s,-.125f,1.43f,-.18f,.085f,.10f,.025f);Detail(s,48,52,49);
        Box(s,.04f,1.43f,-.18f,.085f,.10f,.025f);Detail(s,48,52,49);
        /* Parts 11..14 are hidden until the server equips an item. */
        Box(s,-.37f,.60f,-.37f,.055f,.58f,.07f);Detail(s,150,102,57);
        Box(s,-.36f,.53f,-.33f,.055f,.08f,.24f);Detail(s,150,102,57);
        Box(s,-.36f,1.18f,-.33f,.055f,.08f,.24f);Detail(s,150,102,57);
        Box(s,-.355f,.58f,-.1f,.015f,.64f,.015f);Detail(s,222,211,172);
    } else { Box(s,-.13f,0,-.13f,.26f,.26f,.26f); }
}
static void Draw(struct Entity* e) {
    struct Shape* s=Current();PackedCol colors[6],tint;int i,j,slot=-1,vertices=0;
    cc_uint64 now=Clock();float angle;int hurt=0,swing=0;
    for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(seen[i] && e==&NetPlayers_List[i].Base) { slot=i;break; }
    if(slot>=0) {
        hurt=effects && visual[slot].hurt && now-visual[slot].hurtAt<350;
        swing=effects && visual[slot].swing && now-visual[slot].swingAt<300;
    }
    memcpy(colors,Models.Cols,sizeof(colors));Model_ApplyTexture(e);Model_LockVB(e,s->count*24);
    for(i=0;i<s->count;i++) {
        if(slot>=0 && s==&shapes[7] && i==6 && (visual[slot].sheep&16))continue;
        if(s==&shapes[9] && i>=11 && (slot<0 || visual[slot].mainHand!=858))continue;
        tint=s->tint[i]?s->tint[i]:s->color;
        if(hurt || (effects && slot>=0 && visual[slot].dead))tint=PackedCol_Make(244,83,73,255);
        else if(effects && slot>=0 && visual[slot].fuse==1 && (now/120)%2)tint=PackedCol_Make(244,244,208,255);
        for(j=0;j<6;j++)Models.Cols[j]=PackedCol_Tint(colors[j],tint);
        angle=s->motion[i]==1 || s->motion[i]==3?e->Anim.LeftLegX:e->Anim.RightLegX;
        if(swing && s->motion[i]>=3)angle=-1.7f*Math_SinF((float)(now-visual[slot].swingAt)*MATH_PI/300);
        if(s->motion[i])Model_DrawRotate(angle,0,0,&s->parts[i],false);
        else Model_DrawPart(&s->parts[i]);
        vertices+=24;
    }
    memcpy(Models.Cols,colors,sizeof(colors));Model_UnlockVB();Gfx_DrawVb_IndexedTris(vertices);
}
static float NameY(struct Entity* e) { return ((struct Shape*)e->Model)->height+.2f; }
static float EyeY(struct Entity* e) { return ((struct Shape*)e->Model)->height*.9f; }
static void Size(struct Entity* e) {
    struct Shape* s=(struct Shape*)e->Model;Vec3_Set(e->Size,s->width,s->height,s->length);
}
static void Bounds(struct Entity* e) {
    struct Shape* s=(struct Shape*)e->Model;
    Vec3_Set(e->ModelAABB.Min,-s->width/2,0,-s->length/2);Vec3_Set(e->ModelAABB.Max,s->width/2,s->height,s->length/2);
}
void LapisMobs_Init(void) {
    static const char* const names[]={"lapis-person","lapis-grazer","lapis-bird","lapis-spider","lapis-item","lapis-creeper","lapis-pig","lapis-sheep","lapis-zombie","lapis-skeleton"};int i;
    effects=Options_GetBool("lapis-entity-effects",true);
    if(registered)return;
    for(i=0;i<10;i++) {
        struct Shape* s=&shapes[i];struct Model* m=&s->model;
        memset(s,0,sizeof(*s));m->name=names[i];m->vertices=s->vertices;m->defaultTex=&texture;
        m->MakeParts=Make;m->Draw=Draw;m->GetNameY=NameY;m->GetEyeY=EyeY;m->GetCollisionSize=Size;m->GetPickingBounds=Bounds;
        Model_Init(m);m->maxVertices=PARTS*24;m->usesSkin=false;m->usesHumanSkin=false;m->pushes=false;
        s->width=i==3?1.62f:i==4?.3f:.8f;
        s->height=(i==0 || i==8)?1.65f:i==9?1.60f:i==5?1.46f:i==1?1.24f:i==7?1.1f:i==6?.82f:i==2?.85f:.6f;
        s->length=(i==1 || i==6 || i==7)?1.65f:i==8?1.2f:i==9?.8f:i==2?.92f:s->width;
        {
            static const int colors[10][3]={{146,169,185},{112,73,47},{235,228,205},{85,55,53},{184,162,89},
                {92,153,72},{223,150,140},{155,142,120},{91,141,99},{205,205,181}};
            s->color=PackedCol_Make(colors[i][0],colors[i][1],colors[i][2],255);
        }
        Model_Register(m);
    }
    Model_RegisterTexture(&texture);registered=1;
}
void LapisMobs_Clear(void) {
    int i;
    for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(seen[i])Entities_Remove((EntityID)i);
    memset(seen,0,sizeof(seen));memset(visual,0,sizeof(visual));lastX=lastZ=0;
}
static int ShapeIndex(int type) {
    if(type==25)return 2;if(type==28)return 1;if(type==95)return 6;if(type==106)return 7;if(type==119)return 3;
    if(type==145)return 8;if(type==110)return 9;
    if(type==30)return 5;
    if(type==69 || type==6 || type==50)return 4;return 0;
}
void LapisMobs_Update(struct LapisEntities* entities,int originX,int originZ) {
    int i;struct LocationUpdate u;cc_string name;struct LapisEntity* state;struct Entity* e;
    for(i=0;i<LAPIS_MAX_ENTITIES;i++) {
        state=&entities->list[i];
        if(!state->used) { if(seen[i])Entities_Remove((EntityID)i);seen[i]=0;continue; }
        if(seen[i]==state->revision && lastX==originX && lastZ==originZ)continue;
        e=&NetPlayers_List[i].Base;
        if(!seen[i] || !Entities.List[i] || wireIds[i]!=state->id) {
            if(seen[i])Entities_Remove((EntityID)i);
            NetPlayer_Init(&NetPlayers_List[i]);Entities.List[i]=e;e->SkinFetchState=SKIN_FETCH_COMPLETED;
            name=String_FromReadonly(shapes[ShapeIndex(state->type)].model.name);Entity_SetModel(e,&name);
            Event_RaiseInt(&EntityEvents.Added,i);
            wireIds[i]=state->id;memset(&visual[i],0,sizeof(visual[i]));
        }
        memset(&u,0,sizeof(u));u.flags=LU_HAS_POS|LU_HAS_YAW|LU_HAS_PITCH;
        if(seen[i] && lastX==originX && lastZ==originZ)u.flags|=LU_POS_ABSOLUTE_SMOOTH|LU_ORI_INTERPOLATE;
        Vec3_Set(u.pos,(float)(state->x-originX),(float)state->y,(float)(state->z-originZ));u.yaw=state->yaw+180;u.pitch=state->pitch;
        e->VTABLE->SetLocation(e,&u);e->OnGround=state->ground!=0;
        e->ModelBlock=(BlockID)state->type;
        if(visual[i].hurt!=state->hurt)visual[i].hurtAt=Clock();
        if(visual[i].swing!=state->swing)visual[i].swingAt=Clock();
        visual[i].hurt=state->hurt;visual[i].swing=state->swing;
        visual[i].mainHand=state->mainHand;visual[i].sheep=state->sheep;visual[i].fuse=state->fuse;visual[i].dead=state->dead;
        seen[i]=state->revision;
    }
    lastX=originX;lastZ=originZ;
}
int LapisMobs_Target(struct LapisEntities* e) {
    struct Entity* player=&Entities.CurPlayer->Base;int i=Entities_GetClosest(player);Vec3 delta;
    float distance,blockDistance;
    if(i<0 || i>=LAPIS_MAX_ENTITIES || !e->list[i].used)return -1;
    Vec3_Sub(&delta,&Entities.List[i]->Position,&player->Position);distance=Vec3_LengthSquared(&delta);
    if(distance>20.25f)return -1;
    if(Game_SelectedPos.valid) {
        Vec3_Sub(&delta,&Game_SelectedPos.intersect,&Game_SelectedPos.origin);blockDistance=Vec3_LengthSquared(&delta);
        if(blockDistance+.5f<distance)return -1;
    }
    return i;
}
