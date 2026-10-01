/* Generate a tiny original note-block test melody. Output is dedicated to CC0. */
#include <stdio.h>
int main (void) {
  const unsigned char header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,68};
  const unsigned char notes[] = {60,64,67,72,67,64,62,60};
  FILE *f = fopen("songs/lapis-demo.mid","wbx");
  if (!f) { perror("Creating songs/lapis-demo.mid"); return 1; }
  int ok = fwrite(header,1,sizeof(header),f) == sizeof(header);
  for (unsigned i = 0; ok && i < sizeof(notes); i++) {
    unsigned char event[] = {0,0x90,notes[i],100,96,0x80,notes[i],0};
    ok = fwrite(event,1,sizeof(event),f) == sizeof(event);
  }
  const unsigned char end[] = {0,255,47,0};
  if (fwrite(end,1,sizeof(end),f) != sizeof(end)) ok = 0;
  if (fclose(f)) ok = 0;
  return ok ? 0 : 1;
}
