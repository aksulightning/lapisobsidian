/* Handshake, status, login and configuration packet codecs. */
#include "procedures.h"
#include "packets.h"
#include "tools.h"
#include "plates.h"
#include "registries.h"
#include "varnum.h"
#include "server_config.h"
#include "protocol.h"
#include "registry.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>

// S->C Status Response (server list ping)
int sc_statusResponse (int client_fd) {

  char header[] = "{"
    "\"version\":{\"name\":\"" LAPIS_MINECRAFT_VERSION "\",\"protocol\":" LAPIS_STRINGIFY(LAPIS_PROTOCOL_VERSION) "},"
    "\"description\":{\"text\":\"";
  char footer[] = "\"}}";

  /* Config text is validated UTF-8; escape JSON syntax without heap allocation. */
  char escaped[CONFIG_MOTD_MAX*2]; size_t motd_len = 0;
  for (size_t i = 0; i < CONFIG_MOTD_MAX && server_config.motd[i]; i++) {
    char c = server_config.motd[i];
    if (c == '"' || c == '\\') escaped[motd_len++] = '\\';
    escaped[motd_len++] = c;
  }
  uint16_t string_len = (uint16_t)(sizeof(header)+sizeof(footer)+motd_len-2);

  writeVarInt(client_fd, 1 + string_len + sizeVarInt(string_len));
  writeByte(client_fd, 0x00);

  writeVarInt(client_fd, string_len);
  send_all(client_fd, header, sizeof(header) - 1);
  send_all(client_fd, escaped, (ssize_t)motd_len);
  send_all(client_fd, footer, sizeof(footer) - 1);

  return 0;
}

// C->S Handshake
int cs_handshake (int client_fd) {
  printf("Received Handshake:\n");

  int protocol = readVarInt(client_fd);
  printf("  Protocol version: %d\n", protocol);
  readString(client_fd);
  if (recv_count == -1) return 1;
  printf("  Server address: %s\n", recv_buffer);
  printf("  Server port: %u\n", readUint16(client_fd));
  int intent = readVarInt(client_fd);
  if ((intent != STATE_STATUS && intent != STATE_LOGIN) ||
      (intent == STATE_LOGIN && protocol != LAPIS_PROTOCOL_VERSION)) {
    recv_count = 0;
    return 1;
  }
  printf("  Intent: %d\n\n", intent);
  setClientState(client_fd, intent);

  return 0;
}

// C->S Login Start
int cs_loginStart (int client_fd, uint8_t *uuid, char *name) {
  printf("Received Login Start:\n");

  int n=readVarInt(client_fd);
  if(n<1 || n>16 || recv_all(client_fd,name,(size_t)n,false)!=n || !auth_name_valid(name,(size_t)n)) { recv_count=0; return 1; }
  if(n<16) memset(name+n,0,(size_t)(16-n));
  if(recv_all(client_fd,uuid,16,false)!=16) { recv_count=0; return 1; }

  return 0;
}

// S->C Login Success
int sc_loginSuccess (int client_fd, uint8_t *uuid, char *name) {
  printf("Sending Login Success...\n\n");

  uint8_t name_length = player_name_length(name);
  writeVarInt(client_fd, 1 + 16 + sizeVarInt(name_length) + name_length + 1);
  writeVarInt(client_fd, 0x02);
  send_all(client_fd, uuid, 16);
  writeVarInt(client_fd, name_length);
  send_all(client_fd, name, name_length);
  writeVarInt(client_fd, 0);

  return 0;
}

int cs_clientInformation (int client_fd) {
  int tmp;
  printf("Received Client Information:\n");
  readString(client_fd);
  if (recv_count == -1) return 1;
  printf("  Locale: %s\n", recv_buffer);
  tmp = readByte(client_fd);
  if (recv_count == -1) return 1;
  printf("  View distance: %d\n", tmp);
  tmp = readVarInt(client_fd);
  if (recv_count == -1) return 1;
  printf("  Chat mode: %d\n", tmp);
  tmp = readByte(client_fd);
  if (recv_count == -1) return 1;
  if (tmp) printf("  Chat colors: on\n");
  else printf("  Chat colors: off\n");
  tmp = readByte(client_fd);
  if (recv_count == -1) return 1;
  printf("  Skin parts: %d\n", tmp);
  tmp = readVarInt(client_fd);
  if (recv_count == -1) return 1;
  if (tmp) printf("  Main hand: right\n");
  else printf("  Main hand: left\n");
  tmp = readByte(client_fd);
  if (recv_count == -1) return 1;
  if (tmp) printf("  Text filtering: on\n");
  else printf("  Text filtering: off\n");
  tmp = readByte(client_fd);
  if (recv_count == -1) return 1;
  if (tmp) printf("  Allow listing: on\n");
  else printf("  Allow listing: off\n");
  tmp = readVarInt(client_fd);
  if (recv_count == -1) return 1;
  printf("  Particles: %d\n\n", tmp);
  return 0;
}

// S->C Clientbound Known Packs
int sc_knownPacks (int client_fd) {
  printf("Sending Server's Known Packs\n\n");
  static const char *const parts[] = {"minecraft", "core", LAPIS_MINECRAFT_VERSION};
  uint32_t size = 2;
  for (size_t i = 0; i < 3; i ++) size += (uint32_t)strlen(parts[i]) + 1;
  writeVarInt(client_fd, size);
  writeByte(client_fd, 0x0e);
  writeByte(client_fd, 1);
  for (size_t i = 0; i < 3; i ++) {
    size_t length = strlen(parts[i]);
    writeVarInt(client_fd, (uint32_t)length);
    send_all(client_fd, parts[i], (ssize_t)length);
  }
  return 0;
}

// C->S Serverbound Known Packs
int cs_knownPacks (int client_fd) {
  /* The compact snapshot omits NBT only for this exact built-in pack. */
  if (readVarInt(client_fd) != 1) return 1;
  const char *expected[] = { "minecraft", "core", LAPIS_MINECRAFT_VERSION };
  for (size_t i = 0; i < 3; i ++) {
    readString(client_fd);
    if (recv_count <= 0 || (size_t)recv_count != strlen(expected[i]) ||
        strcmp((const char *)recv_buffer, expected[i]) != 0) {
      fputs("Lapis Obsidian requires the matching Minecraft core pack\n", stderr);
      return 1;
    }
  }
  return 0;
}

// C->S Serverbound Plugin Message
int cs_pluginMessage (int client_fd) {
  printf("Received Plugin Message:\n");
  readString(client_fd);
  if (recv_count == -1) return 1;
  printf("  Channel: \"%s\"\n", recv_buffer);
  if (strcmp((char *)recv_buffer, "minecraft:brand") == 0) {
    readString(client_fd);
    if (recv_count == -1) return 1;
    printf("  Brand: \"%s\"\n", recv_buffer);
  }
  printf("\n");
  return 0;
}

// S->C Clientbound Plugin Message
int sc_sendPluginMessage (int client_fd, const char *channel, const uint8_t *data, size_t data_len) {
  printf("Sending Plugin Message\n\n");
  int channel_len = (int)strlen(channel);

  writeVarInt(client_fd, 1 + sizeVarInt(channel_len) + channel_len + sizeVarInt(data_len) + data_len);
  writeByte(client_fd, 0x01);

  writeVarInt(client_fd, channel_len);
  send_all(client_fd, channel, channel_len);

  writeVarInt(client_fd, data_len);
  send_all(client_fd, data, data_len);

  return 0;
}

// S->C Finish Configuration
int sc_finishConfiguration (int client_fd) {
  writeVarInt(client_fd, 1);
  writeVarInt(client_fd, 0x03);
  return 0;
}

// S->C Registry Data (multiple packets) and Update Tags (configuration, multiple packets)
int sc_registries (int client_fd) {

  printf("Sending Registries\n\n");
  send_all(client_fd, registries_bin, sizeof(registries_bin));

  printf("Sending Tags\n\n");
  send_all(client_fd, tags_bin, sizeof(tags_bin));

  return 0;

}
