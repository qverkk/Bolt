#ifndef _BOLT_LIBRARY_PLUGIN_H_
#define _BOLT_LIBRARY_PLUGIN_H_
#include <stddef.h>
#include <stdint.h>

#include "../rwlock/rwlock.h"
#include "../event.h"

struct RenderBatch2D;
struct Plugin;
struct lua_State;

#define GRAB_TYPE_NONE 0
#define GRAB_TYPE_START 1
#define GRAB_TYPE_STOP 2

struct Transform3D {
    double matrix[16];
};

struct Point3D {
    union {
        int32_t ints[3];
        double floats[4];
    } xyzh;
    uint8_t integer;
    uint8_t homogenous;
};

/// Struct containing "vtable" callback information for RenderBatch2D's list of vertices.
/// Unless stated otherwise, functions will be called with three params: the index, the specified
/// userdata, and an output pointer, which must be able to index the returned number of items.
struct Vertex2DFunctions {
    /// Userdata which will be passed to the functions contained in this struct.
    void* userdata;

    /// Returns the vertex X and Y, in screen coordinates.
    void (*xy)(size_t index, void* userdata, int32_t* out);
    
    /// Returns the X and Y of the texture image associated with this vertex, in pixel coordinates.
    void (*atlas_xy)(size_t index, void* userdata, int32_t* out);

    /// Returns the W and H of the texture image associated with this vertex, in pixel coordinates.
    void (*atlas_wh)(size_t index, void* userdata, int32_t* out);

    /// Returns the U and V of this vertex in pixel coordinates, normalised from 0.0 to 1.0 within
    /// the sub-image specified by atlas xy and wh.
    void (*uv)(size_t index, void* userdata, double* out);

    /// Returns the RGBA colour of this vertex, each one normalised from 0.0 to 1.0.
    void (*colour)(size_t index, void* userdata, double* out);
};

/// Struct containing "vtable" callback information for Render3D's list of vertices.
/// NOTE: there's an important difference here from the 2D pipeline, in the atlas_meta function.
/// The atlas_xywh does not take a vertex index, but rather a meta-ID returned from atlas_meta. The
/// purpose of this is to be able to compare meta-IDs together to check if two vertices have the
/// same texture without having to actually fetch the texture info for each one.
struct Vertex3DFunctions {
    /// Userdata which will be passed to the functions contained in this struct.
    void* userdata;

    /// Returns the vertex X Y and Z, in model coordinates.
    void (*xyz)(size_t index, void* userdata, struct Point3D* out);

    /// Returns a meta-ID for the texture associated with this vertex.
    size_t (*atlas_meta)(size_t index, void* userdata);

    /// Returns the XYWH of the texture image referred to by this meta-ID, in pixel coordinates.
    void (*atlas_xywh)(size_t meta, void* userdata, int32_t* out);

    /// Returns the U and V of this vertex in pixel coordinates, normalised from 0.0 to 1.0 within
    /// the sub-image specified by atlas xy and wh.
    void (*uv)(size_t index, void* userdata, double* out);

    /// Returns the RGBA colour of this vertex, each one normalised from 0.0 to 1.0.
    void (*colour)(size_t index, void* userdata, double* out);

    /// Returns the ID of the bone this vertex belongs to.
    uint8_t (*bone_id)(size_t index, void* userdata);

    /// Returns the transform matrix for the given bone.
    void (*bone_transform)(uint8_t bone_id, void* userdata, struct Transform3D* out);
};

/// Struct containing "vtable" callback information for textures.
/// Note that in the context of Bolt plugins, textures are always two-dimensional.
struct TextureFunctions {
    /// Userdata which will be passed to the functions contained in this struct.
    void* userdata;

    /// Returns the ID for the associated texture object.
    size_t (*id)(void* userdata);

    /// Returns the size of this texture atlas in pixels.
    void (*size)(void* userdata, size_t* out);

    /// Compares a section of this texture to some RGBA bytes using `memcmp`. Returns true if the
    /// section matches exactly, otherwise false.
    ///
    /// Note that changing the in-game "texture compression" setting will change the contents of
    /// the texture for some images and therefore change the result of this comparison.
    uint8_t (*compare)(void* userdata, size_t x, size_t y, size_t len, const unsigned char* data);

    /// Fetches a pointer to the texture's pixel data at coordinates x and y. Doesn't do any checks
    /// on whether x and y are in-bounds. Data is always RGBA and pixel rows are always contiguous.
    uint8_t* (*data)(void* userdata, size_t x, size_t y);
};

/// Struct containing "vtable" callback information for 3D renders' transformation matrices.
struct Render3DMatrixFunctions {
    /// Userdata which will be passed to the functions contained in this struct.
    void* userdata;

    /// Gets the model matrix for this render.
    void (*model_matrix)(void* userdata, struct Transform3D* out);

    /// Gets the combined view-projection matrix for this render.
    void (*viewproj_matrix)(void* userdata, struct Transform3D* out);
};

/// Struct containing "vtable" callback information for surfaces.
struct SurfaceFunctions {
    /// Userdata which will be passed to the functions contained in this struct.
    void* userdata;

    /// Equivalent to glClearColor(r, g, b, a) & glClear().
    void (*clear)(void* userdata, double r, double g, double b, double a);

    /// Updates a rectangular subsection of the surface with the given RGBA or BGRA pixels.
    void (*subimage)(void* userdata, int x, int y, int w, int h, const void* pixels, uint8_t is_bgra);

    /// Draws a rectangle from the surface, indicated by sx,sy,sw,sh, to a rectangle on the backbuffer,
    /// indicated by dx,dy,dw,dh. All values are in pixels.
    void (*draw_to_screen)(void* userdata, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);

    /// Draws a rectangle from the surface, indicated by sx,sy,sw,sh, to a rectangle on the target surface,
    /// indicated by dx,dy,dw,dh. All values are in pixels.
    void (*draw_to_surface)(void* userdata, void* target, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);
};

/// Struct containing functions initiated by plugin code, which must be set on startup, as opposed to
/// being set when a callback object is created like with other vtable structs.
struct PluginManagedFunctions {
    void (*surface_init)(struct SurfaceFunctions*, unsigned int, unsigned int, const void*);
    void (*surface_destroy)(void*);
    void (*surface_resize_and_clear)(void*, unsigned int, unsigned int);
    void (*draw_region_outline)(void* target, int16_t x, int16_t y, uint16_t width, uint16_t height);
    void (*read_screen_pixels)(uint32_t width, uint32_t height, void* data);
    void (*game_view_rect)(int* x, int* y, int* w, int* h);
};

struct WindowPendingInput {
    /* bools are listed at the top to make the structure smaller by having less padding in it */
    uint8_t mouse_motion;
    uint8_t mouse_leave;
    uint8_t mouse_left;
    uint8_t mouse_right;
    uint8_t mouse_middle;
    uint8_t mouse_left_up;
    uint8_t mouse_right_up;
    uint8_t mouse_middle_up;
    uint8_t mouse_scroll_down;
    uint8_t mouse_scroll_up;
    struct MouseEvent mouse_motion_event;
    struct MouseEvent mouse_leave_event;
    struct MouseEvent mouse_left_event;
    struct MouseEvent mouse_right_event;
    struct MouseEvent mouse_middle_event;
    struct MouseEvent mouse_left_up_event;
    struct MouseEvent mouse_right_up_event;
    struct MouseEvent mouse_middle_up_event;
    struct MouseEvent mouse_scroll_down_event;
    struct MouseEvent mouse_scroll_up_event;
};

struct BoltSHM {
#if defined(_WIN32)
    HANDLE handle;
#else
    size_t map_length;
    int fd;
    int unlink_pid; // if 0, don't unlink
#endif
    const char* tag;
    uint64_t id;
    void* file;
};

struct EmbeddedWindowMetadata {
    int x;
    int y;
    int width;
    int height;
};

struct EmbeddedWindow {
    uint64_t id;
    uint64_t plugin_id;
    struct SurfaceFunctions surface_functions;
    struct lua_State* plugin;
    RWLock lock; // applies to the metadata
    struct EmbeddedWindowMetadata metadata;
    RWLock input_lock; // applies to the pending inputs
    struct WindowPendingInput input;
    int16_t drag_xstart;
    int16_t drag_ystart;
    int16_t repos_target_x;
    int16_t repos_target_y;
    uint16_t repos_target_w;
    uint16_t repos_target_h;
    uint8_t reposition_mode; // 1 if the window is being moved or resized by a mouse action
    int8_t reposition_w; // negative, positive or 0 to indicate which edge is being moved
    int8_t reposition_h; // negative, positive or 0 to indicate which edge is being moved
    uint8_t reposition_threshold; // whether the minimum distance threshold has been met during this repositioning
    uint8_t is_browser;
    uint8_t is_deleted;

    /* everything below here is used and initialised only if is_browser, except as noted */
    uint8_t do_capture; // always false for non-browser
    uint8_t capture_ready;
    uint8_t popup_shown; // always false for non-browser
    uint8_t popup_initialised;
    uint64_t capture_id;
    struct BoltSHM browser_shm;
    struct EmbeddedWindowMetadata popup_meta;
    struct SurfaceFunctions popup_surface_functions;
};

struct WindowInfo {
    RWLock lock; // applies to the map
    struct hashmap* map;
    RWLock input_lock; // applies to the pending inputs
    struct WindowPendingInput input;
};

struct RenderBatch2D {
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t index_count;
    uint32_t vertices_per_icon;
    uint8_t is_minimap;
    struct Vertex2DFunctions vertex_functions;
    struct TextureFunctions texture_functions;
};

struct Render3D {
    uint32_t vertex_count;
    uint8_t is_animated;
    struct Vertex3DFunctions vertex_functions;
    struct TextureFunctions texture_functions;
    struct Render3DMatrixFunctions matrix_functions;
};

struct RenderMinimapEvent {
    double angle;
    double scale;
    double x;
    double y;
};

struct SwapBuffersEvent {
#if defined(_MSC_VER)
    // MSVC doesn't allow empty structs
    void* _;
#endif
};

/// Setup the plugin library. Must be called (and return) before using any other plugin library functions,
/// including init and is_inited. This function does not have a "close" reciprocal, as it's expected
/// that this will be called immediately at startup and remain for the entire duration of the process.
void _bolt_plugin_on_startup();

/// Init the plugin library. Call _bolt_plugin_close at the end of execution, and don't double-init.
/// Must be provided with a fully-populated PluginManagedFunctions struct for backend-specific functions
/// from plugin code. The contents are copied so they do not need to remain in scope after this function ends.
void _bolt_plugin_init(const struct PluginManagedFunctions*);

/// Returns true if the plugin library is initialised (i.e. init has been called more recently than
/// close), otherwise false. This function is not thread-safe, since in a multi-threaded context,
/// the returned value could be invalidated immediately by another thread calling plugin_init.
uint8_t _bolt_plugin_is_inited();

/// Sends a SwapBuffers event to all plugins, sends various queued I/O events to the relevant plugins,
/// and finalises other tasks such as the rendering of overlays. Should be called once per frame, from a
/// SwapBuffers hook, BEFORE allowing the SwapBuffers function to run normally. Pass the width and height
/// of the game view.
void _bolt_plugin_end_frame(uint32_t, uint32_t);

/// Close the plugin library.
void _bolt_plugin_close();

/// Gets a reference to the global WindowInfo struct
struct WindowInfo* _bolt_plugin_windowinfo();

/// Handle all incoming IPC messages.
void _bolt_plugin_handle_messages();

/// Creates a new instance of a plugin with its own Lua environment (lua_setfenv).
/// The `lua` param will be loaded and executed in a fresh environment, then event callbacks will be
/// sent to it until it is destroyed by the plugin being stopped.
///
/// Returns 0 on success or 1 on failure.
uint8_t _bolt_plugin_add(const char* path, struct Plugin* plugin);

/// Handles any mouse event, returning true if the event was consumed or false if the event should
/// be passed to the game window. Offsets are into the WindowPendingInput struct. grab_type will be
/// one of the GRAB_TYPE_ defined values. Booleans are write-only and may be NULL.
uint8_t _bolt_plugin_handle_mouse_event(struct MouseEvent* event, ptrdiff_t bool_offset, ptrdiff_t event_offset, uint8_t grab_type, uint8_t* mousein_fake, uint8_t* mousein_real);

/// Returns the ID of the last window to receive an event, or 0 for the game window.
uint64_t _bolt_plugin_get_last_mouseevent_windowid();

/// Create an inbound SHM handle with a tag and ID. This pairing of tag and ID must not have been
/// used for any SHM object previously during this run of the plugin loader. This is usually
/// achieved by assigning ID values incrementally, starting at 1. The tag should be short - usually
/// two letters. "inbound" means it will be opened in read-only mode, and typically the host will
/// open it in write-only mode.
///
/// `tag` and `id` are unused on Windows. The above rules must be followed for posix-compliant systems,
/// since all shm objects must be named (usually in /dev/shm), to ensure all names are unique.
uint8_t _bolt_plugin_shm_open_inbound(struct BoltSHM* shm, const char* tag, uint64_t id);

/// Similar to open_inbound, but will be opened in write-only mode with the host typically using
/// read-only mode. The mapping will always be named using the tag and id, even on Windows.
uint8_t _bolt_plugin_shm_open_outbound(struct BoltSHM* shm, size_t size, const char* tag, uint64_t id);

/// Close and delete an SHM object. The library needs to ensure that the browser host process has
/// been informed and won't try to use this SHM object anymore, before calling this function on it.
void _bolt_plugin_shm_close(struct BoltSHM* shm);

/// Resize an outbound SHM object. The SHM object is assumed to be outbound, i.e. that this process
/// has WRITE permission only. `new_id` is used on Windows only.
void _bolt_plugin_shm_resize(struct BoltSHM* shm, size_t length, uint64_t new_id);

/// Update mapping of an inbound SHM object according to its new size. `handle` is the new Windows
/// HANDLE object, created by the host using DuplicateHandle, and is unused on non-Windows systems.
void _bolt_plugin_shm_remap(struct BoltSHM* shm, size_t length, void* handle);

/// Sends a RenderBatch2D to all plugins.
void _bolt_plugin_handle_render2d(struct RenderBatch2D*);

/// Sends a Render3D to all plugins.
void _bolt_plugin_handle_render3d(struct Render3D*);

/// Sends a RenderMinimap to all plugins.
void _bolt_plugin_handle_minimap(struct RenderMinimapEvent*);

#endif
