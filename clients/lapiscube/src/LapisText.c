#include "LapisText.h"
static int Unit(struct LapisReader* r) {
    int a=LapisReader_Byte(r),b,c;
    if(a>0 && a<128)return a;
    if(a>=0xC2 && a<=0xDF) {
        b=LapisReader_Byte(r);if((b&0xC0)!=0x80)return -1;
        return ((a&31)<<6)|(b&63);
    }
    if(a>=0xE0 && a<=0xEF) {
        b=LapisReader_Byte(r);c=LapisReader_Byte(r);
        if((b&0xC0)!=0x80 || (c&0xC0)!=0x80 || (a==0xE0 && b<0xA0))return -1;
        return ((a&15)<<12)|((b&63)<<6)|(c&63);
    }
    return -1; /* NUL/control text is not part of the Lapis chat/sign profile. */
}
int LapisText_Nbt(struct LapisReader* r,char* text,int capacity) {
    struct LapisReader body;int length=(int)LapisReader_Big(r,2),cp,low,n=0,bytes;
    if(capacity<1 || r->failed || length>r->size-r->pos)return 0;
    LapisReader_Init(&body,r->data+r->pos,length);r->pos+=length;
    while(body.pos<body.size && !body.failed) {
        cp=Unit(&body);if(cp<32 || cp==127)return 0;
        if(cp>=0xD800 && cp<=0xDBFF) {
            low=Unit(&body);if(low<0xDC00 || low>0xDFFF)return 0;
            cp=0x10000+((cp-0xD800)<<10)+(low-0xDC00);
        } else if(cp>=0xDC00 && cp<=0xDFFF)return 0;
        bytes=cp<128?1:cp<2048?2:cp<65536?3:4;
        if(n+bytes>=capacity)return 0;
        if(bytes==1)text[n++]=(char)cp;
        else {
            if(bytes==4)text[n++]=(char)(0xF0|(cp>>18));
            if(bytes>=3)text[n++]=(char)((bytes==3?0xE0:0x80)|((cp>>12)&63));
            text[n++]=(char)((bytes==2?0xC0:0x80)|((cp>>6)&63));
            text[n++]=(char)(0x80|(cp&63));
        }
    }
    text[n]=0;return LapisReader_Done(&body);
}
int LapisText_Valid(const char* text,int length) {
    int i=0,n,j,cp,minimum,b;
    if(length<0)return 0;
    while(i<length) {
        cp=(unsigned char)text[i++];
        if(cp<128) { if(cp<32 || cp==127)return 0;continue; }
        if(cp>=0xC2 && cp<=0xDF) { cp&=31;n=1;minimum=128; }
        else if(cp>=0xE0 && cp<=0xEF) { cp&=15;n=2;minimum=2048; }
        else if(cp>=0xF0 && cp<=0xF4) { cp&=7;n=3;minimum=65536; }
        else return 0;
        if(n>length-i)return 0;
        for(j=0;j<n;j++) { b=(unsigned char)text[i++];if((b&0xC0)!=0x80)return 0;cp=(cp<<6)|(b&63); }
        if(cp<minimum || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF))return 0;
    }
    return 1;
}
