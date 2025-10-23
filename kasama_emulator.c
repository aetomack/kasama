/* wayland_assignment.c
 *
 * ------------------------------------------------------------
 *
 * Instructions:
 *  - Each function below includes a descriptive comment explaining the
 *    expected behavior, inputs, outputs, and hints. Implement the bodies
 *    according to the comments and the Wayland protocol semantics.
 *  - Keep your implementation portable: avoid non-portable behavior unless
 *    the function explicitly requires POSIX (sockets, mmap, etc.).
 *  - Use helper buffer read/write functions provided in this file when
 *    marshaling or unmarshaling Wayland messages.
 *
 * Compile / run (example):
 *   gcc -std=c11 -D_POSIX_C_SOURCE=200112L -o wayland_assignment wayland_assignment.c
 *
 * Note: This is a teaching skeleton; it intentionally omits many details
 * of a production Wayland client (error handling, dynamic object registry
 * replication, full protocol support). The aim is to help you implement
 * the missing parts as part of a CS assignment toward a terminal emulator.
 */

#define _POSIX_C_SOURCE 200112L

#include <wayland-client.h> // add -lwayland-client to compilation command
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>

static bool log_enabled = true;

// standard log macro
#define LOG(msg, ...)
  do {
    if (log_enabled)
      fprintf(stderr, msg, __VA_ARGS__);
  } while (0)

static const uint32_t wayland_display_object_id = 1;
static const uint16_t wayland_wl_registry_event_global = 0;
static const uint16_t wayland_shm_pool_event_format = 0;
static const uint16_t wayland_wl_buffer_event_release = 0;
static const uint16_t wayland_xdg_wm_base_event_ping = 0;
static const uint16_t wayland_xdg_toplevel_event_configure = 0;
static const uint16_t wayland_xdg_toplevel_event_close = 1;
static const uint16_t wayland_xdg_surface_event_configure = 0;
static const uint16_t wayland_wl_display_get_registry_opcode = 1;
static const uint16_t wayland_wl_registry_bind_opcode = 0;
static const uint16_t wayland_wl_compositor_create_surface_opcode = 0;
static const uint16_t wayland_xdg_wm_base_pong_opcode = 3;
static const uint16_t wayland_xdg_surface_ack_configure_opcode = 4;
static const uint16_t wayland_wl_shm_create_pool_opcode = 0;
static const uint16_t wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
static const uint16_t wayland_wl_shm_pool_create_buffer_opcode = 0;
static const uint16_t wayland_wl_surface_attach_opcode = 1;
static const uint16_t wayland_xdg_surface_get_toplevel_opcode = 1;
static const uint16_t wayland_wl_surface_commit_opcode = 6;
static const uint16_t wayland_wl_display_error_event = 0;
static const uint32_t wayland_format_xrgb8888 = 1;
static const uint32_t wayland_header_size = 8;
static const uint32_t color_channels = 4;

/* Helpful constants for this assignment */
#define OLD_IDS_CAP 256U
#define WAYLAND_SOCKET_ENV "WAYLAND_DISPLAY"
#define DEFAULT_WAYLAND_SOCKET "wayland-0"
#define roundup_4(n) (((n)+3) & -4)
#define cstring_len(s) (sizeof(s) - 1)
#define clamp(lower, x, upper) ((x) >= (lower) ? ((x) <= (upper)) : (lower))

/* Forward type declarations copied from the original source */
typedef enum state_state_t state_state_t;
typedef struct entity_t entity_t;
typedef struct state_t state_t;

/* Basic entity used for rendering examples in this assignment */
struct entity_t {
  float x, y;
};

/* Simplified client state structure. Expand as you implement functions. */
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
  uint32_t xdg_toplevel;

  uint32_t wl_surface;
  uint32_t wl_buffer;
  uint32_t wl_seat;

  uint32_t width;
  uint32_t height;
  uint32_t shm_pool_size;
  int shm_fd;
  volatile uint8_t *shm_pool_data;

  float pointer_x;
  float pointer_y;
  uint32_t pointer_button_state;

  entity_t *entities;
  uint64_t entities_len;

  state_state_t state;
};

/* ------------------------ Helper serialization utils --------------------- */

/* Convert a 24.8 fixed-point Wayland value to double.
 * - f: 32-bit fixed-point value used in many Wayland protocol fields.
 * Returns: double representation.
 *
 * Hint: Wayland uses 24.8 fixed format: upper 24 bits integer, lower 8 bits fraction.
 */
static inline double wayland_fixed_to_double(uint32_t f) {
  // This method came from wayland-util.h docs
  (void)f; // ignores compiler errors if not using variable
  union { // use a union since we're mutating and storing the object at the same memory locations
    double d;
    uint64_t i;
  } u;

  u.i = ((1023LL + 44LL) << 52) + (1LL << 51) + f;
  return u.d - (3LL << 43);
}

/* Check/set helpers for tracking old ids */
static bool is_old_id(uint32_t *old_ids, uint32_t id) {
  /* Return true if id is present in old_ids */
  (void)old_ids; (void)id;

  for (uint32_t i = 0; i < OLD_IDS_CAP; i++) {
    if (old_ids[i] == id)
      return true;
  }
  return false;
}

static void store_old_id(uint32_t *old_ids, uint32_t *old_ids_len, uint32_t id) {
  /* Add id into array, if room; avoid duplicates */
  (void)old_ids; (void)old_ids_len; (void)id;
  uint32_t newLen = (*old_ids_len + 1) & OLD_IDS_CAP;
  olds_ids[newLen] = id;
  *old_ids_len = newLen;
}

/* Connect to the Wayland socket defined by WAYLAND_DISPLAY env var or default.
 * - Returns an open fd on success, or -1 on failure with errno set.
 *
 * Hints:
 *  - Use a UNIX domain socket of type SOCK_STREAM.
 *  - The socket path is /run/user/<uid>/wayland-<n> in many systems, but the
 *    environment variable WAYLAND_DISPLAY gives the basename. For this
 *    assignment, use abstract sockets (if you prefer) or search common
 *    locations. Keep implementation simple: use getenv(WAYLAND_SOCKET_ENV)
 *    and connect to a UNIX domain socket with that name.
 */
static int wayland_display_connect() {
  /*: implement connection logic */
  char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime_dir=NULL) {
    return EINVAL;
  }

  uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);
  
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  assert(xdg_runtIme_dir_len <= cstring_len(addr.sun_path));
  uint64_t socket_path_len  = 0;

  memcpy(addr.sun_path, xdg_runtime_dir, xdg_runtime_dir_len);
  socket_path_len += xdg_runtime_dir_len;

  addr.sun_path[socket_path_len++] = '/';

  char *wayland_display = getenv("WAYLAND_DISPLAY");
  uint64_t wayland_display_len = strlen(wayland_display);
  memcyp(addr.sun_path + socket_path_len, wayland_display, wayland_display_len);

  socket_path_len += wayland_display_len;

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (fd == -1)
      exit(errno);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))==-1)
      exit(errno);

  return -1;
}

/* Buffer write helpers for message marshaling. These functions append to a
 * character buffer while maintaining size and capacity. Use
 * these helpers to prepare Wayland messages to write to the socket.
 */

static void buf_write_u32(char *buf, uint64_t *buf_size, uint64_t buf_cap, uint32_t x) {
  /* write a 32-bit little-endian value into buf, update buf_size. */
  (void)buf; (void)buf_size; (void)buf_cap; (void)x;
  assert(*buf_size + sizeof(x) <= buf_cap); // first general check
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0); // checks if exactly enough room
  *(uint32_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static void buf_write_u16(char *buf, uint64_t *buf_size, uint64_t buf_cap, uint16_t x) {
  /* write a 16-bit little-endian value into buf, update buf_size. */
  (void)buf; (void)buf_size; (void)buf_cap; (void)x;
  assert(*buf_size + sizeof(x) <= buf_cap);
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0);
  *(uint16_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static void buf_write_string(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                             char *src, uint32_t src_len) {
  /* Write a null-terminated string with 4-byte padding as Wayland expects. */
  (void)buf; (void)buf_size; (void)buf_cap; (void)src; (void)src_len;
  assert(*buf_size + src_len <= buf_cap);
  
  buf_write_u32(buf, buf_size, buf_cap, src_len);
  memcpy(buf + *buf_size, src, roudnup_4(src_len);
  *buf_size += roundup_4(src_len);
}

/* Buffer read helpers: advance the buffer pointer and parse values. These are
 * small convenience functions to unpack incoming messages. Make sure to check
 * bounds (buf_size) before reading bytes.
 */
static uint32_t buf_read_u32(char **buf, uint64_t *buf_size) {
  (void)buf; (void)buf_size;
  /* read 4 bytes as little-endian u32, advance *buf by 4, decrement *buf_size */
  assert(*buf_size >= sizeof(uint32_t));
  assert((size_t)*buf % sizeof(uint32_t) == 0);

  uint32_t res = *(uint32_t *)(*buf);
  *buf += sizeof(res);
  *buf_size = sizeof(res);
  return res;
}

static uint16_t buf_read_u16(char **buf, uint64_t *buf_size) {
  (void)buf; (void)buf_size;
  /* Read 2 bytes as little-endian u16, advance *buf by 2, decrement *buf_size */
  assert(*buf_size >= sizeof(uint16_t));
  assert((size_t)*buf % sizeof(uint16_t) == 0);
  
  uint16_t res = *(uint16_t *)(*buf);
  *buf += sizeof(res);
  *buf_size = sizeof(res);
  return res;
}

static void buf_read_n(char **buf, uint64_t *buf_size, char *dst, uint64_t n) {
  (void)buf; (void)buf_size; (void)dst; (void)n;
  /* Copy n bytes from *buf into dst and advance pointer. */
  assert(*buf_size >= n);
  // copy n bytes from src to dst
  memcpy(dst, *buf, n);
  *buf += n;
  *buf_size -= n;
}

/* ---------------- Wayland message helper stubs (marshalling helpers) -------- */

/* The following are simplified stubs that mirror the original file's function
 * signatures. For the assignment, implement them to send the appropriate
 * Wayland requests to create pools, buffers, surfaces, and to bind registry
 * interfaces. The comments give the expected purpose and parameters.
 */

/* Example: obtain the wl_registry object by asking the display for 'get_registry' */
static uint32_t wayland_wl_display_get_registry(int fd) {
  /* Build and send a wl_display.get_registry request over fd.
   * Return an object id assigned locally for the registry, or 0 on failure.
   *
   * Hints:
   *  - You will need to marshal the message into a buffer using buf_write_*.
   *  - Wayland messages use 4-byte alignment for total length and padding.
   */

  (void)fd;
  uint64_t msg_size = 0;
  char msg[128] = "";

  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_display_object_id);
  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_display_get_registry_opcode);

  uint16_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  wayland_current_id++;

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_current_id);

  if((int64_t)msg_size != send(fd, msg, msg_size, MSG_DONTWAIT))
    exit(errno);

  return wayland_current_id;
}

static uint32_t wayland_wl_registry_bind(int fd, uint32_t registry, uint32_t name,
                                         char *interface, uint32_t interface_len, uint32_t version) {
  /* Send wl_registry.bind to bind an advertised global. Return local object id. */
  (void)fd; (void)registry; (void)name; (void)interface; (void)interface_len; (void)version;
  uint64_t msg_size = 0;
  char msg[512] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), registry);
  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_display_get_registry_opcode);
  
  uint16_t msg_announced_size = wayland_header_size + sizeof(name) + sizeof(interface_len) + roundup_4(inferface_len) + sizeof(version) + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_annoucned_size);

  buf_write_u32(msg, &msg_size, sizeof(msg), name);
  buf_write_string(msg, &msg_size, sizeof(msg), interface, interface_len);
  buf_write_u32(msg, &msg_size, sizeof(msg), version);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  assert(msg_size == roundup_4(msg_size));
  
  if((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  return wayland_current_id;
}

/* Create a shm pool object associated with a file descriptor backing shared memory */
static uint32_t wayland_wl_shm_create_pool(int fd, state_t *state) {
  (void)fd; (void)state;
  /*  create a wl_shm_pool object (marshal create_pool request) */

  assert(state->shm_pool_size > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_shm);
  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_shm_create_pool_opcode);
  uint16_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id) + sizeof(shm_pool_size);

  assert(msg_announced_size == roundup_4(msg_announced_size));
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);
  buf_write_u32(msg, &msg_size, sizeof(msg), state->shm_pool_size);

  assert(msg_size == roundup_4(msg));
  /* The following will be macros to assist in sending the wayland file descriptor as ancillary data.
   * Why? In this case, we want to share pixel buffers for rendering. 
   * Rather than copy large amounts of pixel data between client and compositor, 
   * a file descriptor for a shared memory region (created by wl_shm) is passed.
   * Then, both client and compositor can mmap the FD and access to the same memory region.
   *
   * Consequence: No memory copying, greater performance.
   */
  char buf[CMSG_SPACE(sizeof(state->shm_fd))] = ""; // create ancillary buffer for new FD

  struct iovec io = {.iov_base = msg, .iov_len = msg_size}; // memory region, starting address & size of memory
  struct msghdr socket_msg = {
    .msg_iov = &io,
    .msg_iovlen = 1,
    .msg_control = control,
    .msg_controllen = sizeof(control),
  };
  
  struct cmsghdr *cmdhr = CMSG_FIRSTHDR($socket_msg);
  cmdhr->cmdhr_level = SOL_SOCKET;
  cmdhr->cmdhr_type = SCM_RIGHTS;
  cmdhr->cmdhr_len = CMSG_LEN(sizeof(state->shm_fd));

  *((int *)CMSG_DATA(cmdhr)) = state->shm_fd;
  socket_msg.msg_controllen = CMSG_SPACE(sizeof(state->shm_fd));

  if(sendmsg(fd, &socket_msg, 0) == -1)
    exit(errno);

  LOG("-> wl_shm@%u.create_pool: wl_shm_pool=%u\n", state->wl_shm, wayland_current_id);
  return wayland_current_id;
}

/* Create a buffer from the shm pool */
static uint32_t wayland_wl_shm_pool_create_buffer(int fd, state_t *state) {
  (void)fd; (void)state;
  /* allocate and create a wl_buffer using wl_shm_pool.create_buffer */
  assert(state->shm_pool_size > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_shm);
  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_shm_pool_create_buffer_opcode);
  uint16_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id) + sizeof(uint32_t) * 5;
  
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);
  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);
  uint32_t offset = 0;
  buf_write_u32(msg, &msg_size, sizeof(msg), offset);
  buf_write_u32(msg, &msg_size, sizeof(msg), state->w);
  buf_write_u32(msg, &msg_size, sizeof(msg), state->h);
  buf_write_u32(msg, &msg_size, sizeof(msg), state->stride); 

  uint32_t format = wayland_format_xrgb8888;
  buf_write_u32(msg, &msg_size, sizeof(msg), format);
  
  if((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  LOG("-> wl_shm_pool@%u.create.buffer: wl_buffer=%u\n", state->wl_shm_pool, wayland_current_id);

  return wayland_current_id;
}

static void wayland_wl_buffer_destroy(int fd, uint32_t wl_buffer) {
  (void)fd; (void)wl_buffer;
  /* TODO: send wl_buffer.destroy for the given object id */
}

static void wayland_wl_surface_attach(int fd, uint32_t wl_surface, uint32_t wl_buffer) {
  (void)fd; (void)wl_surface; (void)wl_buffer;
  /* TODO: send wl_surface.attach with buffer and commit if needed */
}

static uint32_t wayland_wl_surface_frame(int fd, uint32_t wl_surface) {
  (void)fd; (void)wl_surface;
  /* TODO: request a callback for next frame (wl_surface.frame) */
  return 0;
}

static uint32_t wayland_xdg_surface_get_toplevel(int fd, state_t *state) {
  (void)fd; (void)state;
  /* TODO: send xdg_surface.get_toplevel and return assigned object id */
  return 0;
}

static void wayland_wl_surface_commit(int fd, state_t *state) {
  (void)fd; (void)state;
  /* TODO: send wl_surface.commit; this applies attached buffers */
}

static void wayland_wl_surface_damage_buffer(int fd, uint32_t wl_surface,
                                             uint32_t x, uint32_t y,
                                             uint32_t w, uint32_t h) {
  (void)fd; (void)wl_surface; (void)x; (void)y; (void)w; (void)h;
  /* TODO: send wl_surface.damage_buffer with rectangle extents */
}

static uint32_t wayland_wl_seat_get_pointer(int fd, uint32_t wl_seat) {
  (void)fd; (void)wl_seat;
  /* TODO: request a pointer object from wl_seat */
  return 0;
}

/* ------------------- Simple renderer helpers ------------------------------ */

/* Clear a region of pixels in the shm buffer. Pixels are 32-bit ARGB or similar. */
static void renderer_clear(volatile uint32_t *pixels, uint64_t size, uint32_t color_rgb) {
  (void)pixels; (void)size; (void)color_rgb;
  /* Clear `size` pixels to color_rgb. Handle volatile pointers. */

  for (uint64_t i = 0; i < size; i++)
    pixels[i] = color_rgb;
}

/* Draw a filled rectangle in the framebuffer */
static void renderer_draw_rect(volatile uint32_t *dst, uint64_t dst_w,
                               uint64_t dst_h, uint32_t dst_stride,
                               uint64_t rect_x, uint64_t rect_y,
                               uint64_t rect_w, uint64_t rect_h,
                               uint32_t color_rgb) {
  (void)dst; (void)dst_w; (void)dst_h; (void)dst_stride;
  (void)rect_x; (void)rect_y; (void)rect_w; (void)rect_h; (void)color_rgb;
  /* implement safe rectangle drawing with bounds checks. */

  uint64_t dst_len = window_w * window_h;

  for(uint64_t src.y = 0; src.y < rec_h; src.y++){
    for(uint64_t src.x = 0; src.x < rect_w; src.x++) {
      uint64_t x = dst_x + src_x;
      uint64_t y = dst_y + src_y;

      if (x >= window_w || y >= window_h)
        continue;

      uint64_t pos = window_w * y + x;
    }
  }
}

/* Compose a full frame by drawing the entities and other UI elements */
static void render_frame(int fd, state_t *state) {
  (void)fd; (void)state;
  /* TODO: call renderer_clear and renderer_draw_rect to populate the shm buffer,
   * then attach the buffer to the surface and commit. This drives on-screen pixels.
   */
}

/* ------------------- Event handling ------------------------------------- */

/* Parse and handle an incoming Wayland message
 * - fd: connection to the Wayland socket
 * - state: our client state
 * - msg: pointer to received message buffer (will be advanced)
 * - msg_len: remaining length in buffer
 *
 * This function should:
 *  - Read sender_id, object_id, opcode, and dispatch to proper handlers.
 *  - Update state fields based on registry announcements (globals), seat
 *    capabilities (pointer/keyboard), pointer events (motion/button), and
 *    other relevant protocol events.
 */
static void wayland_handle_message(int fd, state_t *state, char **msg, uint64_t *msg_len) {
  (void)fd; (void)state; (void)msg; (void)msg_len;
  /* TODO: implement message parsing and dispatch based on object IDs/opcodes. */
}

/* ------------------- Main (program flow) --------------------------------- */

/* The main routine should:
 *  - Connect to the Wayland display
 *  - Get the registry and bind to relevant globals (wl_shm, wl_seat, xdg_wm_base)
 *  - Create a shm pool and a buffer for the initial window size
 *  - Create an xdg_surface and xdg_toplevel, set up event callbacks
 *  - Enter an event loop: read socket data, call wayland_handle_message, and
 *    schedule frame callbacks and commits using render_frame.
 *
 * For the assignment, implement progressively:
 *  1) Connection + registry bind
 *  2) Create shm pool + buffer, attach to surface, commit (see render_frame)
 *  3) Implement a basic renderer to draw background and moving entities
 *  4) Implement pointer event handling to update pointer position and click state
 */
int main(void) {
  /* TODO: implement program bootstrap steps described above. */
  struct wl_display *display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "No display\n");
    return 1;
  }

  fprintf(stderr, "Connection established\n");
  struct wl_registry = wl_display_get_registry(display);
  
  return 0;
}
