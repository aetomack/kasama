# CS Systems Programming Assignment

## Project: Building a Minimal Wayland Client for a Terminal Emulator

This assignment introduces the fundamentals of the Wayland protocol and client implementation.
You will implement core components of a minimal Wayland client, providing the groundwork for
a graphical terminal emulator. The provided skeleton file **wayland_assignment.c** contains
type definitions, function signatures, and inline comments describing each function’s intent.

---

## Learning Objectives

- Understand the structure of the Wayland display protocol and its client-side APIs.
- Work with UNIX domain sockets to establish a client–server connection.
- Implement shared memory buffers and message marshaling/unmarshaling.
- Handle Wayland events and manage state objects (surfaces, callbacks, etc.).
- Lay the foundation for a simple terminal emulator frontend.

---

## Assignment Structure

The skeleton file is divided into several logical sections. Each part builds toward a functioning
Wayland client capable of opening a window and rendering a surface.

1. **Initialization** – Connect to the Wayland display using a UNIX domain socket.
2. **Registry and Globals** – Discover and bind to core Wayland interfaces.
3. **Shared Memory Buffers** – Create and manage memory-backed buffers for drawing pixels.
4. **Surface Management** – Attach buffers to surfaces and display them.
5. **Event Loop** – Receive and handle Wayland events to keep the client responsive.

---

## Step-by-Step Implementation Instructions

### 1. Buffer Helper Methods

**Functions to implement:**
- `buf_write_u32`
- `buf_write_u16`
- `buf_write_string`
- `buf_read_u32`
- `buf_read_u16`
- `buf_read_n`

**Goal:**
Implement small read/write utilities to serialize and deserialize primitive data
to and from byte buffers. Wayland uses little-endian encoding and 4-byte alignment.

**Steps:**
1. Work with raw byte arrays to store or extract integers.
2. For writing: check that the buffer has enough capacity, then copy bytes.
3. For reading: advance the buffer pointer and reduce the available size.
4. Strings should be padded to 4-byte boundaries.

---

### 2. Connection Setup

**Function to implement:**
- `wayland_display_connect`

**Goal:**
Connect to the Wayland compositor via a UNIX domain socket.

**Steps:**
1. Retrieve the display name using `WAYLAND_DISPLAY` environment variable, defaulting to `"wayland-0"`.
2. Create a UNIX domain socket of type `SOCK_STREAM`.
3. Construct the path `/run/user/<uid>/<display>` or use the abstract socket namespace if preferred.
4. Connect to the socket; on success, return its file descriptor.

---

### 3. Message Marshalling

**Functions to implement:**
- `wayland_wl_display_get_registry`
- `wayland_wl_registry_bind`

**Goal:**
Build and send binary messages conforming to the Wayland wire protocol.

**Steps:**
1. Prepare a message buffer.
2. Write message header fields: object ID, opcode, and total message size.
3. Append arguments using your buffer helpers.
4. Send the full message to the Wayland socket.

---

### 4. Shared Memory Pool and Buffer Creation

**Functions to implement:**
- `wayland_wl_shm_create_pool`
- `wayland_wl_shm_pool_create_buffer`

**Goal:**
Create a shared memory pool for rendering and attach buffers to it.

**Steps:**
1. Create a temporary file (e.g., using `memfd_create` or `shm_open`).
2. Resize it with `ftruncate` to the desired size.
3. Map it into memory using `mmap`.
4. Send a `wl_shm.create_pool` message to the compositor.
5. Use the returned pool ID to create a `wl_buffer` with specified width, height, and stride.

---

### 5. Rendering Helpers

**Functions to implement:**
- `renderer_clear`
- `renderer_draw_rect`
- `render_frame`

**Goal:**
Draw into the shared memory buffer and display it via the Wayland surface.

**Steps:**
1. In `renderer_clear`, fill all pixels with a single color.
2. In `renderer_draw_rect`, color a bounded rectangular region.
3. In `render_frame`, combine the two above to draw the scene, then send a `wl_surface.attach` and `wl_surface.commit` to display the result.

---

### 6. Surface and Window Management

**Functions to implement:**
- `wayland_wl_surface_attach`
- `wayland_wl_surface_commit`
- `wayland_xdg_surface_get_toplevel`
- `wayland_wl_surface_damage_buffer`

**Goal:**
Link your buffers to visible Wayland surfaces and present them on screen.

**Steps:**
1. Send a `wl_compositor.create_surface` to obtain a `wl_surface` ID.
2. Attach a buffer to it and mark the damaged (changed) region.
3. Commit the surface to make changes visible.
4. Use the xdg-shell protocol to promote it to a window (`xdg_surface` and `xdg_toplevel`).

---

### 7. Event Handling

**Function to implement:**
- `wayland_handle_message`

**Goal:**
Parse messages from the compositor and update your local state.

**Steps:**
1. Continuously read data from the Wayland socket.
2. For each message, extract the sender, opcode, and payload.
3. Match these against known objects (registry, surface, seat, etc.).
4. Update your `state_t` structure accordingly.
5. Trigger re-renders when frame callbacks or pointer motion events occur.

---

### 8. Main Program Flow

**Function to implement:**
- `main`

**Goal:**
Orchestrate all steps and run the Wayland client loop.

**Steps:**
1. Connect to the display.
2. Retrieve and bind to globals (registry, shm, compositor, xdg_wm_base).
3. Create a shared memory pool and initial buffer.
4. Create a surface and toplevel window.
5. Enter an infinite event loop:
   - Read socket data.
   - Dispatch messages via `wayland_handle_message`.
   - Redraw frames using `render_frame`.

---

## Milestones

1. **Milestone 1:** Implement buffer helpers and verify serialization.
2. **Milestone 2:** Establish Wayland socket connection.
3. **Milestone 3:** Create shm pool and buffer.
4. **Milestone 4:** Render and display your first frame.
5. **Milestone 5:** Add event handling and pointer interaction.

---

## Submission

Submit your completed `wayland_assignment.c` along with a short README describing:
- Which milestones you completed.
- Any challenges or insights gained.
- Possible extensions (text rendering, shell commands, animations, etc.).

---

**Tip:** Focus on understanding how the client and compositor communicate through file descriptors and binary messages. Debugging can be done using tools like `WAYLAND_DEBUG=1` to observe protocol traffic.

Good luck, and enjoy your first deep dive into low-level Linux graphics programming!
