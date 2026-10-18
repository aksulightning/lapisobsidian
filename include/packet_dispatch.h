/* Frame dispatch and movement validation used by the server loop. */


#ifndef H_PACKET_DISPATCH
#define H_PACKET_DISPATCH
void handlePacket(int client_fd, int length, int packet_id, int state);
void handlePlayerMovement(int client_fd, int length, int packet_id);
#endif
