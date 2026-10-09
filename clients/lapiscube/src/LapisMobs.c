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
#include <string.h>

/* Original simple geometric models, independently dimensioned and CC0.
   Shared shape families make the first mob pass small; species detail is ongoing. */
#define PARTS 12
struct Shape {
    struct Model model;struct ModelVertex vertices[PARTS*24];struct ModelPart parts[PARTS];
    int count,legs;float width,height,length;PackedCol color;
};
static struct Shape shapes[6];
static struct ModelTex texture={"lapis-entities.png",0,NULL,NULL};
static unsigned seen[LAPIS_MAX_ENTITIES];
static int wireIds[LAPIS_MAX_ENTITIES];
static int registered,lastX,lastZ;
static struct Shape* Current(void) { return (struct Shape*)Models.Active; }
static void Box(struct Shape* s,float x,float y,float z,float w,float h,float d) {
    struct BoxDesc b;
    memset(&b,0,sizeof(b));b.sizeX=b.sizeY=b.sizeZ=1;
    b.x1=x;b.y1=y;b.z1=z;b.x2=x+w;b.y2=y+h;b.z2=z+d;b.rotX=x+w/2;b.rotY=y+h;b.rotZ=z+d/2;
    BoxDesc_BuildBox(&s->parts[s->count++],&b);
}
static void Make(void) {
    struct Shape* s=Current();int i;float x,z;
    s->count=0;
    if(s==&shapes[0]) { /* upright traveller / hostile */
        Box(s,-.22f,.64f,-.14f,.44f,.57f,.28f);Box(s,-.23f,1.21f,-.21f,.46f,.44f,.42f);
        Box(s,-.23f,0,-.13f,.17f,.64f,.26f);Box(s,.06f,0,-.13f,.17f,.64f,.26f);
        Box(s,-.37f,.62f,-.12f,.14f,.58f,.24f);Box(s,.23f,.62f,-.12f,.14f,.58f,.24f);
    } else if(s==&shapes[1]) { /* four-footed grazing animal */
        Box(s,-.38f,.38f,-.5f,.76f,.53f,1.0f);Box(s,-.25f,.72f,-.81f,.5f,.44f,.43f);
        for(i=0;i<4;i++) { x=(i&1)?.20f:-.34f;z=(i&2)?.27f:-.45f;Box(s,x,0,z,.14f,.4f,.18f); }
    } else if(s==&shapes[2]) { /* small bird, bill and wings */
        Box(s,-.23f,.15f,-.23f,.46f,.36f,.46f);Box(s,-.16f,.42f,-.34f,.32f,.31f,.3f);
        Box(s,-.13f,0,-.07f,.07f,.2f,.1f);Box(s,.06f,0,-.07f,.07f,.2f,.1f);
        Box(s,-.1f,.45f,-.46f,.2f,.09f,.15f);Box(s,-.3f,.3f,-.14f,.08f,.15f,.28f);Box(s,.22f,.3f,-.14f,.08f,.15f,.28f);
    } else if(s==&shapes[3]) { /* spider, eight independent limbs */
        Box(s,-.38f,.17f,-.15f,.76f,.38f,.65f);Box(s,-.27f,.23f,-.46f,.54f,.28f,.34f);
        for(i=0;i<8;i++)Box(s,(i&1)?.33f:-.81f,.12f,-.38f+(float)(i/2)*.24f,.48f,.1f,.1f);
    } else if(s==&shapes[5]) {
        Box(s,-.24f,.28f,-.17f,.48f,.7f,.34f);Box(s,-.27f,.98f,-.27f,.54f,.48f,.54f);
        for(i=0;i<4;i++)Box(s,(i&1)?.07f:-.26f,0,(i&2)?.1f:-.26f,.19f,.3f,.19f);
    } else { /* compact item / projectile */ Box(s,-.13f,0,-.13f,.26f,.26f,.26f); }
}
static void Draw(struct Entity* e) {
    struct Shape* s=Current();PackedCol colors[6];int i,j;PackedCol tint=s->color;
    switch(e->ModelBlock) {
    case 25:tint=PackedCol_Make(235,228,205,255);break;
    case 28:tint=PackedCol_Make(112,73,47,255);break;
    case 95:tint=PackedCol_Make(223,150,140,255);break;
    case 106:tint=PackedCol_Make(222,222,210,255);break;
    case 145:tint=PackedCol_Make(91,141,99,255);break;
    case 110:tint=PackedCol_Make(205,205,181,255);break;
    case 119:tint=PackedCol_Make(85,55,53,255);break;
    case 30:tint=PackedCol_Make(92,153,72,255);break;
    }
    memcpy(colors,Models.Cols,sizeof(colors));Model_ApplyTexture(e);Model_LockVB(e,s->count*24);
    for(i=0;i<s->count;i++) {
        for(j=0;j<6;j++)Models.Cols[j]=PackedCol_Tint(colors[j],i==1?PackedCol_Make(225,210,179,255):tint);
        if(i>=2 && i<6)Model_DrawRotate((i&1)?e->Anim.LeftLegX:e->Anim.RightLegX,0,0,&s->parts[i],false);
        else Model_DrawPart(&s->parts[i]);
    }
    memcpy(Models.Cols,colors,sizeof(colors));Model_UnlockVB();Gfx_DrawVb_IndexedTris(s->count*24);
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
    static const char* const names[]={"lapis-person","lapis-grazer","lapis-bird","lapis-spider","lapis-item","lapis-creeper"};int i;
    if(registered)return;
    for(i=0;i<6;i++) {
        struct Shape* s=&shapes[i];struct Model* m=&s->model;
        memset(s,0,sizeof(*s));m->name=names[i];m->vertices=s->vertices;m->defaultTex=&texture;
        m->MakeParts=Make;m->Draw=Draw;m->GetNameY=NameY;m->GetEyeY=EyeY;m->GetCollisionSize=Size;m->GetPickingBounds=Bounds;
        Model_Init(m);m->maxVertices=PARTS*24;m->usesSkin=false;m->usesHumanSkin=false;m->pushes=false;
        s->width=i==3?1.6f:i==1?.8f:.6f;s->height=i==0?1.65f:i==5?1.46f:i==1?1.2f:i==2?.75f:.6f;s->length=i==1?1.5f:s->width;
        s->color=PackedCol_Make(90+i*22,125+i*13,120-i*12,255);Model_Register(m);
    }
    Model_RegisterTexture(&texture);registered=1;
}
void LapisMobs_Clear(void) {
    int i;
    for(i=0;i<LAPIS_MAX_ENTITIES;i++)if(seen[i])Entities_Remove((EntityID)i);
    memset(seen,0,sizeof(seen));lastX=lastZ=0;
}
static int ShapeIndex(int type) {
    if(type==25)return 2;if(type==28 || type==95 || type==106)return 1;if(type==119)return 3;
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
            wireIds[i]=state->id;
        }
        memset(&u,0,sizeof(u));u.flags=LU_HAS_POS|LU_HAS_YAW|LU_HAS_PITCH;
        if(seen[i] && lastX==originX && lastZ==originZ)u.flags|=LU_POS_ABSOLUTE_SMOOTH|LU_ORI_INTERPOLATE;
        Vec3_Set(u.pos,(float)(state->x-originX),(float)state->y,(float)(state->z-originZ));u.yaw=state->yaw+180;u.pitch=state->pitch;
        e->VTABLE->SetLocation(e,&u);e->OnGround=state->ground!=0;
        e->ModelBlock=(BlockID)state->type;
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
