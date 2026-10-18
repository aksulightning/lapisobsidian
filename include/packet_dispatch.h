/* Frame dispatch and movement validation used by the server loop. */
#ifndef H_PACKET_DISPATCH
#define H_PACKET_DISPATCH
/* The caller must activate a complete packet_input frame. length excludes the
 * packet ID. recv_count <= 0 tells the server loop to disconnect the client. */
void handlePacket(int client_fd, int length, int packet_id, int state);
/* Accepts only movement IDs 0x1D..0x20 under the same frame/error contract. */
void handlePlayerMovement(int client_fd, int length, int packet_id);
#endif
