#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <unistd.h>

#define OLD_IDS_CAP 512

static bool log_enabled = false;
#define LOG(msg, ...) 
  do {
    if (log_enabled)
      fprintf(stderr, msg, __VA_ARGS__);
  } while(0)

// Utility macros
#define cstring_len(s) (sizeof(s)-1) // to retrieve string lengths
#define roundup_4(n) (((n) + 3) & -4) // Wayland protocol messages must be 4-byte aligned. Pads message lengths
#define clamp(lower, x, upper)
  ((x) >= (lower) ? ((x) <= (upper) ? (x) : (upper)) : (lower))

static uint32_t wayland_current_id = 1;

static const uint32_t wayland_display_object_id = 1;
static const uint16_t wayland_wl_registry_event_global = 0;
static const uint16_t wayland_shm_pool_event_format = 0;
static const uint16_t wayland_wl_buffer_event_release = 0;
static const uint16_t wayland_xdg_wm_base_event_ping = 0;
static const uint16_t wayland_xdg_toplevel_event_configure = 0;
static const uint16_t wayland_xdg_toplevel_event_close = 1;
static const uint16_t wayland_xdg_surface_event_configure = 0;
static const uint16_t wayland_wl_seat_event_capabilities = 0;
static const uint16_t wayland_wl_seat_event_capabilities_pointer = 1;
static const uint16_t wayland_wl_seat_event_capabilities_keyboard = 2;
static const uint16_t wayland_wl_seat_event_name = 1;
static const uint16_t wayland_wl_pointer_event_enter = 0;
static const uint16_t wayland_wl_pointer_event_leave = 1;
static const uint16_t wayland_wl_pointer_event_motion = 2;
static const uint16_t wayland_wl_pointer_event_button = 3;
static const uint16_t wayland_wl_pointer_event_frame = 5;
static const uint16_t wayland_wl_seat_get_pointer_opcode = 0;
static const uint16_t wayland_wl_display_get_registry_opcode = 1;
static const uint16_t wayland_wl_registry_bind_opcode = 0;
static const uint16_t wayland_wl_compositor_create_surface_opcode = 0;
static const uint16_t wayland_xdg_wm_base_pong_opcode = 3;
static const uint16_t wayland_xdg_surface_ack_configure_opcode = 4;
static const uint16_t wayland_wl_shm_create_pool_opcode = 0;
static const uint16_t wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
static const uint16_t wayland_wl_shm_pool_create_buffer_opcode = 0;
static const uint16_t wayland_wl_buffer_destroy_opcode = 0;
static const uint16_t wayland_xdg_surface_get_toplevel_opcode = 1;
static const uint16_t wayland_wl_surface_attach_opcode = 1;
static const uint16_t wayland_wl_surface_frame_opcode = 3;
static const uint16_t wayland_wl_surface_commit_opcode = 6;
static const uint16_t wayland_wl_surface_damage_buffer_opcode = 9;
static const uint16_t wayland_wl_display_error_event = 0;
static const uint16_t wayland_wl_display_delete_id_event = 1;
static const uint16_t wayland_wl_callback_done_event = 0;
static const uint32_t wayland_format_xrgb8888 = 1;
static const uint32_t wayland_header_size = 8;
static const uint32_t color_channels = 4;

typedef enum state_state_t state_state_t;

enum state_state_t {
  STATE_NONE,
  STATE_SURFACE_SURF  ACE_SETUP,
  STATE_SURFACE_FIRST_FRAME_RENDERED,
};

typedef struct entity_t entity_t;
struct entity_t {
  float x,y;
};

typedef struct state_t state_t;
struct state_t {
  uint32_t wl_registry;
  uint32_t wl_shm;
  uint32_t wl_shm_pool;
  uint32_t old_wl_buffers[OLD_IDS_CAP];
  uint32_t old_wl_buffers_len;
  uint32_t old_wl_callbacks[OLD_IDS_CAP];
  uint32_t old_wl_callbacks_len;
  uint32_t xdg_wm_base;
  uint32_t xdg_surface;
  uint32_t wl_compositor;
  uint32_t wl_seat;
  uint32_t wl_pointer;
  uint32_t wl_callback;
  uint32_t wl_surface;
  uint32_t xdg_toplevel;
  uint32_t stride;
  uint32_t w;
  uint32_t h;
  uint32_t shm_pool_size;
  int shm_fd;
  volatile uint8_t *shm_pool_data;

  float pointer_x;
  float pointer_y;
  uint32_t pointer_button_state;

  entity_t *entities;
  uint64_t entities_len;

  state_state_t state;
}

static inline double wayland_fixed_to_double(uint32_t f) {
  union {
    double d;
    int64_t i;
  } u;

  u.i = ((1023LL + 44LL) << 52) + (1LL << 51) + (int32_t)f;

  return u.d - (3LL << 43);
}

static bool is_old_id(uint32_T *old_ids, uint32_t *old_ids_len,  uint32_t id) {
  uint32_t new_len = (*old_ids_len + 1) % OLD_IDS_CAP;
  old_ids[new_len] = id;
  *old_ids_len = new_len;
}

/*
* We first must connect to the wayand compositor. Where X11 can run over network via TCP/IP, Wayland runs locally 
* and uses a UNIX domain socket. 
*
* Why not use wl_display_connect from libwayland-client and avoid this low level logic? 
* Because I'm a stud. I don't take no shit. I smoke my stogie anywhere I want. I don't have to hide like you.
*/
static int wayland_display_connect() {
  char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR"); // Wayland sockets live under the runtime directory.
  if (xdg_runtime_dir == NULL) {
    return EINVAL; // if unreachable, cannot connect to compositor. fail.
  }

  uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);

  // Build base path
  struct sockaddr_un addr = {.sun_family = AF_UNIX}; // standard struct for unix domain sockets. AF_UNIX instructs this is a local socket
  assert(xdg_runtime_dir_len <= cstring_len(addr.sun_path)); // addr.sun_path is the actual socket path. Ensure we don't overflow
  uint64_t socket_path_len = 0;

  memcpy(addr.sun_path, xdg_runtime_dir, xdg_runtime_dir_len);
  socket_path_len += xdg_runtime_dir_len;

  addr.sun_path[socket_path_len++] = '/';

  // Append display name to base path
  char *wayland_display = getenv("WAYLAND_DISPLAY"); // which socket file to connect to?
  if (wayland_display == NULL) {
    char wayland_display_default[] = "wayland-0"; // if not set, just default to 0
    uint64_t wayland_display_default_len = cstring_len(wayland_display_default);

    memcpy(addr.sun_path + socket_path_len, wayland_display_default, wayland_display_default_len);
    socket_path_len += wayland_display_default_len;
  } else {
    uint64_t wayland_display_len = strlen(wayland_display);
    memcpy(addr.sun_path + socket_path_len, wayland_display, wayland_display_len);
    socket_path_len += wayland_display_len;
  }

  // Create a stream socket
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
    exit(errno);
  }

  // With our wayland compositor path we built and the socket defined, attempt to connect.
  // If successful, compositor accepts and the file descriptor becomes communication channel.
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    exit(errno);
  }
  
  // return file descriptor! We can now send & receive requests & events from the compositor.
  return fd;
}


/*
* Create a registry so we can query compositor.
* For wayland object creation, we'll just need to send a message followed by a unique id of our own creation.
* Message struture takes an id of the resource we call the method on (4 bytes); the opcode of the method (2 bytes); size of msg (2 bytes)
*/


/*
* @brief Writes a 32-bit message to send to wayland compositor. 
*
* @param *buf Pointer to the byte buffer to which we'll write command.
* @param *buf_size Pointer to the number of how many bytes we've written so far.
* @param buf_cap Max capacity of the buffer.
* @param x The 32-bit value we're writing.
*
* @return void
*/
static void buf_write_u32(char *buf, uint64_t *buf_size, uint64_t buf_cap, uint32_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap); // assert we have space for message
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0); // assert there is exactly 32-bits of space

  // Note the pointer at the beginning: We dereference this arg so we can actually write to the location.
  *(uint32_t *)(buf + *buf_size) = x; // Moves pointer forward by buf_size to free space, and write
  *buf_size += sizeof(x); // Increase buffer size by size of x.
}


/*
* @brief Writes a 16-bit message to send to wayland compositor.
*
* @param *buf Pointer to the byte buffer to which we'll write message to pass to compositor.
* @param *buf_size Pointer to the number of how many bytes we've written so far.
* @param buf_cap Max capacity of the buffer.
* @param x The 16-bit message we're writing.
*
* @return void
*/
static void buf_write_u16(char *buf, uint64_t *buf_size, uint64_t buf_cap, uint16_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap); // Assert we have enough space in our buffer to write x
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0) // exactly 16 bits;

  *(uint16_t *)(buf + *buf_size) = x; // dereference and cast to 16-bit int, increment to next free space and write x.
  buf_size += sizeof(x);
}

/*
* @brief Writes a constructed string argument into our message buffer in the format Wayland expects.
*        Our buffer is max 32-bit. Wayland expects messages 4 bytes long (32 bits). We can of course pad this.
*        Why do we use memcpy? With other methods, we are clearly expecting 32 or 16 bits. Strings can be of arbitrary length, however.
*        Differs from integers since where we could normally just drop them in place, we need to copy entire chunks of bytes. Hence memcpy.
*
* @param *buf Pointer to the start of our message buffer
* @param *buf_size Pointer to our current write offset
* @param buf_cap Max size of our buffer. Used to ensure we don't overflow
* @param *src Pointer to the string source.
* @param src_len Length of our string 
*/
static void buf_write_string(char *buf, uint64_t *buf_size, uint64_t buf_cap, char *src, uint32_t src_len) {
  assert(*buf_size + src_len <= buf_cap); // check for overflow before writing.
  buf_write_u32(buf, buf_size, buf_cap, src_len); // writes a 32-bit string at the current offset.
  memcpy(buf + *buf_size, src, roundup_4(src_len)); // Copy entire string, of arbitrary length.
  *buf_size += roundup_4(src_len);
}

static uint32_t buf_read_u32(char **buf, uint64_t *buf_size) {
  assert(*buf_size >= sizeof(uint32_t)); // check for overflow before doing anything else.
  assert((size_t)*buf % sizeof(uint32_t) == 0); E

  uint32_t res = *(uint32_t *)(*buf);
  *buf += sizeof(res);
  *buf_size -= sizeof(res);

  return res;
}

static uint16_t buf_read_u16(char **buf, uint64_t *buf_size) {

  assert((size_t)*buf % sizeof(uint16_t) == 0);
 
  uint16_t res = *(uint16_t *)(*buf);
  *buf += sizeof(res);
  *buf_size -= sizeof(res);

  return res;
}

static void buf_read_n(char **buf, uint64_t *buf_size, char *dst, uint64_t n) {
  assert(*buf_size >= n);

  memcpy(dst, *buf, n);

  *buf += n;
  *buf_size -= n;
}

static uint32_t wayland_wl_display_get_registry(int fd) {
  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_display_object_id);
  buf_wite_u16(msg, &msg_size, sizeof(msg), wayland_wl_display_get_registry_opcode);

  uint16_t msg_annouced_size = wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);
  if ((int64_t)msg_size != send(fd, msg, msg_size, MSG_DONTWAIT))
    exit(errno)

  printf("-> wl_display@%u.get_registry: wl_registry=%u\n", 
        wayland_display_object_id, wayland_current_id);

  return wayland_current_id;
}

static uint32_t wayland_wl_registry_bind(int fd, uint32_t registry, uint32_t name, char *interfaces, uint32_t interface_len, uint32_t version) {
  uint64_t msg_size = 0;
  char msg[512] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), registry);
  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_registry_bind_opcode);

  uint16_t msg_annouced_size = wayland_header_size + sizeof(name) + sizeof(interface_len) + roundup_4(interfce_len) + sizeof(version) + sizeof(wayland_current_id);
  assert(roundup_4(msg_annouced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_annouced_size);
  buf_write_u32(msg, &msg_size, sizeof(msg), name);
  buf_write_string(msg, &msg_size, sizeof(msg), interface, interface_len);
  buf_write_u32(msg, &msg_size, sizeof(msg), version);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  assert(msg_size == roundup_4(msg_size));

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  LOG("-> wl_registry@%u.bind: name=%u interface=%. *s version=%u "
      "wayland_current_id=%u\n",
      registry, name, interfce_len, interface, version, wayland_current_id);

  return wayland_current_id;
}

static uint32_t wayland_wl_comopsitor_create_surface(int fd, state_t *state) {
  assert(state->wl_compositor > 0);
  
  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_compositor);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_comopsitor_create_surface_opcode);

  uint16_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id);
  assert(msg_annouced_size == roundup_4(msg_annouced_size));
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);
  
  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);
  if ((int64_t)msg_size != size(fd, msg, msg_size, 0))
    exit(errno);

  LOG("->wl_compositor@%u.create_surface: wl_surface%u\n",
      state->wl_compositor, wayland_current_id);

  return wayland_current_id;
}

static void wayland_xdg_wm_base_pong(int fd, state_t *state, uint32_t ping) {
  assert(state->xdg_wm_base > 0);
  assert(state->wal_surface > 0);
}
