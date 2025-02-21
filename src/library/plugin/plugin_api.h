#include <lua.h>

/*
 * The functions in this file are implemented in `plugin.c`. Their forward-declarations have been
 * separated out into this file purely to make it easier to find documentation on the plugin API.
 * 
 * There are no plugin API functions other than the ones in this file. They are listed here in no
 * particular order, other than an approximate attempt to group relevant functions together.
 * 
 * To access this API from a plugin, load the API and use its functions like so. For the purpose of
 * future-proofing, it's important to check the API version is compatible with your plugin, using
 * either `bolt.apiversion` or `bolt.checkversion`.
 * Note that `bolt.checkversion` in Lua is equivalent to `api_checkversion` declared in this file.
 * ```
 * local bolt = require("bolt")
 * bolt.checkversion(1, 0)
 * --...
 * ```
 *
 * After that, pass Lua functions to the `bolt.on...` functions to set event callbacks.
 *
 * The 2D rendering pipeline is fairly simple. Images are drawn in large batches of vertices,
 * usually 6 vertices per icon (three per triangle, two triangles.) Plugins should call the
 * `verticesperimage` function instead of hard-coding the number 6. Each individual vertex has an
 * associated texture; the assumption that all six will have the same texture appears to be a safe
 * one as of right now, but who knows what might break in future engine updates?
 *
 * The 3D rendering pipeline is *far* more complicated, but is mostly tracked internally by Bolt
 * in order to provide a simple API for plugins. 3D renders are not batched, so a 3D render event
 * will contain all the vertices for one whole model. However, each vertex still has its own
 * texture, and many models do have multiple textures. Plugins usually do not need to check every
 * single vertex - a single vertex with a known texture image on it would usually be sufficient.
 *
 * Most coordinates below are specifically "world coordinates", which work on a scale of 512 per
 * tile. So if you move one tile to the east, your X in world coordinates increases by 512.
 */

/// [-0, +2, -]
/// Returns the Bolt API major version and minor version, in that order.
/// Plugins should call this function on startup and, if the major version is one it doesn't
/// recognise, it should exit by calling `error()`. The minor version however does not need to be
/// checked, as minor versions will never contain breaking changes; they may add features, though,
/// and the minor version can be used to check for the existence of those features.
///
/// For compatibility reasons, there will never be a breaking change to this function.
static int api_apiversion(lua_State*);

/// [-2, +0, v]
/// Simple alternative to `apiversion()` which calls `error()` if any of these conditions is true:
/// - the first parameter is not equal to Bolt's major version
/// - the second parameter is greater than Bolt's minor version
///
/// Due to the way `error()` is implemented in Lua, this function will never return on failure.
///
/// For compatibility reasons, there will never be a breaking change to this function.
static int api_checkversion(lua_State*);

/// [-0, +0, -]
/// Stops this instance of this plugin. Any resources will be cleaned up and destroyed. Do not use
/// any API functions after this one.
static int api_close(lua_State*);

/// [-0, +1, -]
/// Returns a monotonic time as an integer, in microseconds.
/// 
/// This function can be used for timing. The number it returns is arbitrary - that is, it's the
/// number of microseconds that have elapsed since an arbitrary point in time - therefore it's not
/// useful for anything other than to call this function multiple times and compare the results.
///
/// Note that on a 32-bit CPU this number will overflow back to 0 every ~4296 seconds, which is
/// slightly more than an hour. On a 64-bit CPU, it will overflow every ~18 trillion seconds, or
/// around 585 millennia. Playing on a 32-bit CPU is therefore not advisable, but extra precautions
/// must be taken if a plugin wishes to support 32-bit CPUs while using this function.
static int api_time(lua_State*);

/// [-0, +6, -]
/// Returns six integers: the current calendar year, month (1-12), day (1-31), hour (0-23), minute
/// (0-59), and second (0-60*), in game-time (i.e. UTC). The time is based on the user's system
/// clock but the result will be converted to game-time. There is no way to get the user's timezone
/// information via Bolt.
///
/// (*seconds value can be 60 in the case of a leap-second)
///
/// Do not try to use this function for precision timing. Use time() instead.
static int api_datetime(lua_State*);

/// [-0, +1, -]
/// Returns an integer representing the current weekday in game-time (i.e. UTC). A value of 1
/// represents Sunday, 2 represents Monday, 3 represents Tuesday, and so on.
///
/// This function is based on the user's system clock but the result will be converted to
/// game-time. There is no way to get the user's timezone information via Bolt.
static int api_weekday(lua_State*);

/// [-1, +1, -]
/// Loads the file into a Lua string and returns it. The file will be located relative to the
/// plugin directory. Either '/' or '\' may be used as file separators, regardless of OS, and it
/// makes no difference if the path does or doesn't start with a file separator. In the case of an
/// error, this function will return nil. The most likely cause of failure is that the file doesn't
/// exist.
///
/// The plugin directory is read-only. For writeable files, use `saveconfig` and `loadconfig`.
static int api_loadfile(lua_State*);

/// [-1, +1, -]
/// Loads the file into a Lua string and returns it. The file will be located relative to the
/// plugin's config directory, the exact location of which depends on the user's OS. Either '/' or
/// '\' may be used as file separators, regardless of OS, and it makes no difference if the path
/// does or doesn't start with a file separator. In the case of an error, this function will return
/// nil. The most likely cause of failure is that the file doesn't exist.
static int api_loadconfig(lua_State*);

/// [-2, +1, -]
/// Saves the Lua string in the second parameter into a file identified by the first parameter. The
/// file will be located relative to the plugin's config directory, the exact location of which
/// depends on the user's OS. Either '/' or '\' may be used as file separators, regardless of OS,
/// and it makes no difference if the path does or doesn't start with a file separator.
/// 
/// This function returns a boolean: if the file is saved successfully this function will return
/// true. If not, it will return false. The most likely cause of failure is that the file already
/// exists and is locked for writing, such as by the user having it open in a text editor.
static int api_saveconfig(lua_State*);

/// [-2, +1, -]
/// Creates a surface with the given width and height, and returns it as a userdata object. The
/// surface will initially be fully transparent.
///
/// A surface can be drawn onto with the rendering functions and can be overlaid onto the screen
/// by calling `surface:drawtoscreen()` during a swapbuffers callback.
///
/// Surface widths and heights should always be integral powers of 2. GPUs often can't handle other
/// values correctly which will result in unexpected behaviour.
///
/// All of the member functions of surface objects can be found in this file, prefixed with
/// "api_surface_".
static int api_createsurface(lua_State*);

/// [-3, +1, -]
/// Creates a surface with the given width, height, and RGBA data (string). See `createsurface`
/// documentation for more information on surfaces.
///
/// There are four bytes in an RGBA pixel, so the number of bytes in the string is expected to be
/// 4 * width * height. If fewer bytes than that are provided, the data will be padded with zeroes.
/// If too many bytes are provided, the excess data will be unused. The data will be interpreted in
/// row-major order with the first pixel being in the top-left.
static int api_createsurfacefromrgba(lua_State*);

/// [-1, +1, e]
/// Creates a surface from the PNG file at the given path. See `createsurface` documentation for
/// more information on surfaces.
///
/// The path will be interpreted similarly to require(), i.e. relative to the plugin's root
/// directory, using '.' as file separators, and must not include the ".png" extension (this is
/// appended automatically). This function will call `error()` if the file does not exist or is
/// inaccessible for any reason.
///
/// As with `createsurface`, the width and height of your PNG file should be integral powers of 2.
///
/// Loading PNG files is slow, so use this function sparingly.
static int api_createsurfacefrompng(lua_State*);

/// [-4, +1, -]
/// Creates an embedded window with the given initial values for x, y, width, height. The x and y
/// relate to the top-left corner of the window. An embedded window's top, left, bottom and right
/// are always clamped to the window size. Embedded windows can capture mouse and keyboard events,
/// and can be drawn to like a surface, although there's no way to draw from a window to elsewhere.
///
/// All of the member functions of window objects can be found in this file, prefixed with
/// "api_window_".
static int api_createwindow(lua_State*);

/// [-3, +1, -]
/// Creates a browser window with the given initial values for width, height, and URL. If the URL
/// begins with "file://", it will be interpreted as a file path relative to the root directory of
/// this plugin, and must use "/" as file separators (if any). Otherwise, it will be treated as a
/// URL of an internet website.
///
/// All of the member functions of browser objects can be found in this file, prefixed with
/// "api_browser_".
static int api_createbrowser(lua_State*);

/// [-5, +1, -]
/// Creates an embedded browser window with the given initial values for x, y, width, height, and
/// URL. If the URL begins with "file://", it will be interpreted as a file path relative to the
/// root directory of this plugin, and must use "/" as file separators (if any). Otherwise, it will
/// be treated as a URL of an internet website.
///
/// Embedded browsers behave similarly to embedded windows, except that browsers' window events are
/// handled internally, so the plugin cannot receive callbacks for them.
///
/// A browser cannot be changed from embedded to external, nor vice versa, after creation.
///
/// All of the member functions of embedded browser objects can be found in this file, prefixed
/// with "api_embeddedbrowser_" and "api_browser_on". (i.e. event handler functions are shared
/// between embedded and non-embedded browsers)
static int api_createembeddedbrowser(lua_State*);

/// [-3, +1, -]
/// Creates a Point object from x y and z values. Point objects have functions which are useful for
/// 3D space calculations. All of the member functions of Point objects can be found in this file,
/// prefixed with "api_point_".
static int api_point(lua_State*);

/// [-1, +1, m]
/// Creates a fixed-size Buffer object with the given size. Buffer objects can be used to create
/// large byte arrays more efficiently by using a string, since string concatenations use a lot of
/// allocator calls and can't be pre-allocated even if the size is known. For Bolt functions which
/// use strings as byte-arrays, such as browser:sendmessage, buffers can be used instead.
///
/// All of the member functions of Buffer objects can be found in this file, prefixed with
/// "api_buffer_".
static int api_createbuffer(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for SwapBuffers events, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for SwapBuffers events.
///
/// In simple terms, SwapBuffers represents the end of a frame's rendering, as well as the start of
/// the next one. The callback will be called with one param, a SwapBuffers userdata object, which
/// currently has no purpose.
///
/// The callback will be called every frame - that's anywhere from 5 to 150+ times per second - so
/// avoid using it for anything time-consuming.
static int api_onswapbuffers(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for rendering of 2D images, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for 2D rendering.
///
/// Each time a batch of 2D images is rendered, the callback will be called with one param, that
/// being a 2D batch object. All of the member functions of 2D batch objects can be found in this
/// file, prefixed with with "api_batch2d_". The batch object and everything contained by it will
/// become invalid as soon as the callback ends, so do not retain them.
///
/// The callback will be called an extremely high amount of times per second, so great care should
/// be taken to determine as quickly as possible whether any image is of interest or not, such as
/// by checking each image's width and height.
static int api_onrender2d(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for rendering of 3D models, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for 2D rendering.
///
/// Each time a 3D model is rendered, the callback will be called with one param, that being a 3D
/// render object. All of the member functions of 3D render objects can be found in this file,
/// prefixed with "api_render3d". The object and everything contained by it will become invalid as
/// soon as the callback ends, so do not retain them.
///
/// The callback will be called an extremely high amount of times per second, so great care should
/// be taken to determine as quickly as possible whether any image is of interest or not, such as
/// by checking the model's vertex count.
static int api_onrender3d(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for rendering of a minimap background image, overwriting the previous
/// callback, if any. Passing a non-function (ideally `nil`) will restore the default setting,
/// which is to have no handler for minimap rendering.
///
/// The game renders chunks of 3D land to a large 2048x2048 texture and caches it until the player
/// moves far enough away that it needs to be remade. A scaled and rotated section of this image is
/// drawn to a smaller texture once per frame while the minimap is on screen. As such, plugins can
/// expect to get a maximum of one minimap event per frame (i.e. between each SwapBuffers event.)
///
/// The callback will be called with one param, that being a minimap render object. All of the
/// member functions of that object can be found in this file, prefixed with "api_minimap_".
///
/// The pixel contents cannot be examined directly, however it's possible to query the image angle,
/// image scale (zoom level), and a rough estimate of the tile position it's centered on.
static int api_onminimap(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for mouse motion events, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for mouse motion events.
///
/// This callback applies only to inputs received by the game view. If any embedded windows or
/// browsers receive the input, it will be sent to them, and not to this function. note also that
/// this callback will be called at most once per frame: plugins will always receive the latest
/// mouse position, but some position updates will be overwritten by newer ones before the plugin
/// ever receives them.
///
/// The callback will be called with one param, that being a mouse motion object. All of the member
/// functions of that object can be found in this file, prefixed with "api_mouseevent_".
static int api_onmousemotion(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for mouse button events, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for mouse button events.
///
/// This callback applies only to inputs received by the game view. If any embedded windows or
/// browsers receive the input, it will be sent to them, and not to this function.
///
/// The callback will be called with one param, that being a mouse-button object. All of the member
/// functions of that object can be found in this file, prefixed with "api_mouseevent_" and
/// "api_mousebutton_".
static int api_onmousebutton(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for mouse button release events, overwriting the previous callback, if
/// any. Passing a non-function (ideally `nil`) will restore the default setting, which is to have
/// no handler for mouse button events.
///
/// This callback generally applies to mouse button releases for which the click was received by
/// the game view. For example, if a user clicks their left mouse button, drags the mouse outside
/// the game window, then releases that button, this event will still be fired.
///
/// The callback will be called with one param, that being a mouse-button object. All of the member
/// functions of that object can be found in this file, prefixed with "api_mouseevent_" and
/// "api_mousebutton_".
static int api_onmousebuttonup(lua_State*);

/// [-1, +0, -]
/// Sets a callback function for mouse scroll events, overwriting the previous callback, if any.
/// Passing a non-function (ideally `nil`) will restore the default setting, which is to have no
/// handler for mouse scroll events.
///
/// This callback applies only to inputs received by the game view. If any embedded windows or
/// browsers receive the input, it will be sent to them, and not to this function.
///
/// The callback will be called with one param, that being a mouse-scroll object. All of the member
/// functions of that object can be found in this file, prefixed with "api_mouseevent_" and
/// "api_scroll_".
static int api_onscroll(lua_State*);

/// [-1, +1, -]
/// Returns the number of vertices in a 2D batch object.
static int api_batch2d_vertexcount(lua_State*);

/// [-1, +1, -]
/// Returns the number of vertices per individual image in this batch. At time of writing, this
/// will always return 6 (enough to draw two separate triangles.)
///
/// If the game engine is ever improved to be able to draw an image with only 4 vertices (enough
/// to draw a solid rectangle, e.g. using GL_TRIANGLE_STRIP), then that will be indicated here, so
/// it's recommended to use this function instead of hard-coding the number 6.
static int api_batch2d_verticesperimage(lua_State*);

/// [-1, +1, -]
/// Returns true if this render targets the minimap texture. There will usually be a maximum of one
/// batch per frame targeting the minimap texture.
static int api_batch2d_isminimap(lua_State*);

/// [-1, +2, -]
/// Returns the width and height of the target area of this render, in pixels.
///
/// If `isminimap()` is true, this will be the size of the minimap texture - usually 256x256.
///
/// If `isminimap()` is false, this will be proprortional to the size of the inner area of the game
/// window - that is, if the user has an interface scaling other than 100%, it will be larger or
/// smaller than that area, proportionally.
static int api_batch2d_targetsize(lua_State*);

/// [-2, +2, -]
/// Given an index of a vertex in a batch, returns its X and Y in screen coordinates.
static int api_batch2d_vertexxy(lua_State*);

/// [-2, +2, -]
/// Given an index of a vertex in a batch, returns the X and Y of its associated image in the
/// batch's texture atlas, in pixel coordinates.
static int api_batch2d_vertexatlasxy(lua_State*);

/// [-2, +2, -]
/// Given an index of a vertex in a batch, returns the width and height of its associated image in
/// the batch's texture atlas, in pixel coordinates.
static int api_batch2d_vertexatlaswh(lua_State*);

/// [-2, +2, -]
/// Given an index of a vertex in a batch, returns the vertex's associated "UV" coordinates.
///
/// The values will be floating-point numbers in the range 0.0 - 1.0. They are relative to the
/// position of the overall image in the texture atlas, queried by vertexatlasxy and vertexatlaswh.
static int api_batch2d_vertexuv(lua_State*);

/// [-2, +4, -]
/// Given an index of a vertex in a batch, returns the red, green, blue and alpha values for that
/// vertex, in that order. All four values will be floating-point numbers in the range 0.0 - 1.0.
///
/// Also aliased as "vertexcolor" to keep the Americans happy.
static int api_batch2d_vertexcolour(lua_State*);

/// [-1, +1, -]
/// Returns the unique ID of the texture associated with this render. There will always be one (and
/// only one) texture associated with a 2D render batch. These textures are "atlased", meaning they
/// will contain a large amount of small images, and each set of vertices in the batch may relate
/// to different images in the same texture.
///
/// The plugin API does not have a way to get a texture by its ID; this is intentional. The purpose
/// of this function is to be able to compare texture IDs together to check if the current texture
/// atlas is the same one that was used in a previous render.
static int api_batch2d_textureid(lua_State*);

/// [-1, +2, -]
/// Returns the size of the overall texture atlas associated with this render, in pixels.
static int api_batch2d_texturesize(lua_State*);

/// [-4, +1, -]
/// Compares a section of the texture atlas for this batch to some RGBA data. For example:
/// 
/// `batch:texturecompare(64, 128, {0xFF, 0x0, 0x0, 0xFF, 0xFF, 0x0, 0x0, 0xFF})`
///
/// This would check if the pixels at 64,128 and 65,128 are red. The bytes must match exactly
/// for the function to return true, otherwise it will return false.
///
/// Normally the X and Y coordinates should be calculated from `vertexatlasxy()` and
/// `vertexatlaswh()`. Comparing a whole block of pixels at once by this method is relatively fast,
/// but can only be done one row at a time.
static int api_batch2d_texturecompare(lua_State*);

/// [-4, +1, -]
/// Gets the RGBA data starting at a given coordinate of the texture atlas, for example:
///
/// `batch:texturedata(64, 128, 8)`
///
/// This would return RGBA data for eight bytes, i.e. the two pixels at (64,128) and (65,128),
/// encoded as a Lua string.
///
/// Encoding Lua strings is computationally expensive, and indexing the data one byte at a time is
/// even more so. Unless you really need to do that, use `texturecompare()` instead.
static int api_batch2d_texturedata(lua_State*);

/// [-1, +1, -]
/// Returns the angle at which the minimap background image is being rendered, in radians.
/// 
/// The angle is 0 when upright (facing directly north), and increases counter-clockwise (note that
/// turning the camera clockwise rotates the minimap counter-clockwise and vice versa.)
static int api_minimap_angle(lua_State*);

/// [-1, +1, -]
/// Returns the scale at which the minimap background image is being rendered.
///
/// This indicates how far in or out the player has zoomed their minimap. It appears to be capped
/// between roughly 0.5 and 3.5.
static int api_minimap_scale(lua_State*);

/// [-1, +2, -]
/// Returns an estimate of the X and Y position the minimap is centered on, in world coordinates.
/// 
/// This is only a rough estimate and can move around a lot even while standing still. It usually
/// doesn't vary by more than half a tile.
static int api_minimap_position(lua_State*);

/// [-(1|4|5), +0, -]
/// Deletes any previous contents of the surface and sets it to contain a single colour and alpha.
///
/// If four params are provided, they must be RGBA values, in that order, in the range 0.0-1.0.
///
/// If three params are provided, they must be RGB values, in that order, in the range 0.0-1.0. The
/// alpha value will be inferred to be 1.0.
///
/// If no params are provided, the alpha values will be inferred to be 0.0 (fully transparent),
/// with the red, green and blue values undefined.
static int api_surface_clear(lua_State*);

/// [-6, +0, -]
/// Updates a rectangular subsection of this surface with the given RGBA pixel data.
///
/// The parameters are X,Y,W,H in pixels, followed by the RGBA data (string).
///
/// There are four bytes in an RGBA pixel, so the number of bytes in the string is expected to be
/// 4 * width * height. If fewer bytes than that are provided, the data will be padded with zeroes.
/// If too many bytes are provided, the excess data will be unused. The data will be interpreted in
/// row-major order with the first pixel being in the top-left.
static int api_surface_subimage(lua_State*);

/// [-9, +0, -]
/// Draws a section of the surface directly onto the screen.
///
/// Paramaters are source X,Y,W,H followed by destination X,Y,W,H, all in pixels.
static int api_surface_drawtoscreen(lua_State*);

/// [-10, +0, -]
/// Draws a section of the surface directly onto a section of another surface.
///
/// Paramaters are target surface, then source X,Y,W,H, then destination X,Y,W,H, all in pixels.
static int api_surface_drawtosurface(lua_State*);

/// [-10, +0, -]
/// Draws a section of the surface directly onto a section of a window object.
///
/// Paramaters are target window, then source X,Y,W,H, then destination X,Y,W,H, all in pixels.
static int api_surface_drawtowindow(lua_State*);

/// [-1, +0, -]
/// Closes and destroys the window. This is the only way for a window to be destroyed, other than
/// the plugin stopping, which will destroy the window automatically.
///
/// Do not use the window object again after calling this function on it.
static int api_window_close(lua_State*);

/// [-1, +1, -]
/// Returns the unique ID of this window (an integer).
static int api_window_id(lua_State*);

/// [-1, +2, -]
/// Returns the width and height of the window.
static int api_window_size(lua_State*);

/// [-(1|4|5), +0, -]
/// Deletes any previous contents of the window and sets it to contain a single colour and alpha.
/// See surface_clear for usage.
static int api_window_clear(lua_State*);

/// [-6, +0, -]
/// Updates a rectangular subsection of this window with the given RGBA pixel data. See
/// surface_subimage for usage.
static int api_window_subimage(lua_State*);

/// [-3, +0, -]
/// Starts repositioning for this window. This function changes how the user's "drag" action is
/// processed, and would usually be called from the `onmousebutton` callback for the left mouse
/// button. Repositioning will occur until the user releases the left mouse button or until the
/// repositioning is cancelled. In the first case, an `onreposition` event will be fired.
///
/// This function takes two integer parameters. The first should be negative if the window's left
/// edge is being dragged, positive if the right edge is being dragged, or zero if neither the left
/// or right edge is being dragged. The second parameter should be negative if the window's top
/// edge is being dragged, positive for the window's bottom edge, or zero for neither the top or
/// bottom edge. Finally, if both are zero, the window will be moved instead of resized.
static int api_window_startreposition(lua_State*);

/// [-1, +0, -]
/// Cancels repositioning for this window. If the window is in the process of being repositioned by
/// the user dragging it, that will be cancelled and no repositioning will take place.
static int api_window_cancelreposition(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for reposition events. If the value is a function, it
/// will be called with one parameter, that being a reposition event object. If the value is not a
/// function, it will not be called, and therefore the plugin will not be notified of reposition
/// events for this window.
///
/// Reposition events refer to the window's position and/or size having changed. If the window was
/// resized, its new contents will be fully transparent and must be redrawn. Call event:didresize()
/// to check if that's the case.
///
/// Note that when repositioning ends by the user releasing the mouse button, this event will be
/// fired even if the position and size didn't actually change. This is primarily because there
/// would be no other way for the plugin to know when repositioning has ended.
static int api_window_onreposition(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for mouse motion events. If the value is a function, it
/// will be called with one parameter, that being a mouse motion object. If the value is not a
/// function, it will not be called, and therefore the plugin will not be notified of mouse motion
/// events.
static int api_window_onmousemotion(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for mouse button events. If the value is a function, it
/// will be called with one parameter, that being a mouse-button object. If the value is not a
/// function, it will not be called, and therefore the plugin will not be notified of mouse-button
/// events.
static int api_window_onmousebutton(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for mouse button release events. If the value is a
/// function, it will be called with one parameter, that being a mouse-button object. If the value
/// is not a function, it will not be called, and therefore the plugin will not be notified of
/// mouse-button release events.
static int api_window_onmousebuttonup(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for mouse scroll events. If the value is a function, it
/// will be called with one parameter, that being a mouse-scroll object. If the value is not a
/// function, it will not be called, and therefore the plugin will not be notified of mouse-scroll
/// events.
static int api_window_onscroll(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this window for mouse leave events. If the value is a function, it
/// will be called with one parameter, that being a mouse motion object. If the value is not a
/// function, it will not be called, and therefore the plugin will not be notified of mouse motion
/// events.
static int api_window_onmouseleave(lua_State*);

/// [-1, +1, -]
/// Returns the number of vertices in a 3D render object (i.e. a model).
static int api_render3d_vertexcount(lua_State*);

/// [-2, +1, -]
/// Given an index of a vertex in a model, returns a Point object representing its model
/// coordinates. Specifically, this is the default position of this vertex in the model - it is not
/// affected by any kind of scaling, rotation, movement, or animation that may be happening to the
/// model. The Point can be transformed using the transforms available from the Render3D object.
static int api_render3d_vertexxyz(lua_State*);

/// [-1, +1, -]
/// Returns a Transform object representing the model matrix for this render. The model matrix
/// transforms a point from model coordinates to world coordinates.
static int api_render3d_modelmatrix(lua_State*);

/// [-1, +1, -]
/// Returns a Transform object representing the combined view and projection matrix, commonly
/// called the "viewproj" matrix, for this render. The viewproj matrix transforms a point from
/// world coordinates to screen coordinates.
///
/// After transforming a point into screen coordinates using the viewproj matrix, its X and Y will
/// be in the range [-1.0, +1.0] if it's on the screen, and its Z will relate to its depth (i.e.
/// its distance from the screen.) On the Y axis, -1.0 relates to the bottom of the window and 1.0
/// to the top, meaning it's upside-down compared to Bolt's other screen-space functions. All of
/// this will be corrected when using the point's `aspixels()` function.
static int api_render3d_viewprojmatrix(lua_State*);

/// [-2, +1, -]
/// Given a bone ID, returns the Transform object that would be applied to its static model in
/// model-space, to transform it to its animated position.
///
/// It is a fatal error to call this function on a render event for a non-animated model, since
/// non-animated models have no bone transforms that could be queried. To check if the model is
/// animated, use `animated()`.
static int api_render3d_boneanimation(lua_State*);

/// [-2, +1, -]
/// Given an index of a vertex in a model, returns a meta-ID relating to its associated image.
///
/// Much like 2D batches, 3D renders always have exactly one texture atlas associated with them,
/// but each vertex can still be associated with a different image from the atlas. To allow for
/// finding if two vertices share the same image without having to fetch and compare the whole
/// image data for each one, an extra step was added to the API: plugins must query the vertex's
/// image meta-ID, then use that ID to fetch texture details (if desired). Meta-IDs should not be
/// retained and used outside the current callback, as the game may invalidate them.
static int api_render3d_vertexmeta(lua_State*);

/// Given an image meta-ID from this render, fetches the X Y W and H of its associated image in the
/// texture atlas, in pixel coordinates.
static int api_render3d_atlasxywh(lua_State*);

/// [-2, +2, -]
/// Given an index of a vertex in a model, returns the vertex's associated "UV" coordinates.
///
/// The values will be floating-point numbers in the range 0.0 - 1.0. They are relative to the
/// position of the overall image in the texture atlas, queried by atlasxywh.
static int api_render3d_vertexuv(lua_State*);

/// [-2, +4, -]
/// Given an index of a vertex in a model, returns the red, green, blue and alpha values for that
/// vertex, in that order. All four values will be floating-point numbers in the range 0.0 - 1.0.
///
/// Also aliased as "vertexcolor" to keep the Americans happy.
static int api_render3d_vertexcolour(lua_State*);

/// [-1, +1, -]
/// Returns the unique ID of the texture associated with this render. There will always be one (and
/// only one) texture associated with a 3D model render. These textures are "atlased", meaning they
/// will contain a large amount of small images, and each vertex in the model may relate to
/// different images in the same texture.
///
/// The plugin API does not have a way to get a texture by its ID; this is intentional. The purpose
/// of this function is to be able to compare texture IDs together to check if the current texture
/// atlas is the same one that was used in a previous render.
static int api_render3d_textureid(lua_State*);

/// [-1, +2, -]
/// Returns the size of the overall texture atlas associated with this render, in pixels.
static int api_render3d_texturesize(lua_State*);

/// [-4, +1, -]
/// Compares a section of the texture atlas for this render to some RGBA data. For example:
/// 
/// `render:texturecompare(64, 128, {0xFF, 0x0, 0x0, 0xFF, 0xFF, 0x0, 0x0, 0xFF})`
///
/// This would check if the pixels at 64,128 and 65,128 are red. The bytes must match exactly
/// for the function to return true, otherwise it will return false.
///
/// Normally the X and Y coordinates should be calculated from `atlasxywh()`. Comparing a whole
/// block of pixels at once by this method is relatively fast, but can only be done one row at a
/// time.
static int api_render3d_texturecompare(lua_State*);

/// [-4, +1, -]
/// Gets the RGBA data starting at a given coordinate of the texture atlas, for example:
///
/// `render:texturedata(64, 128, 8)`
///
/// This would return RGBA data for eight bytes, i.e. the two pixels at (64,128) and (65,128),
/// encoded as a Lua string.
///
/// Encoding Lua strings is computationally expensive, and indexing the data one byte at a time is
/// even more so. Unless you really need to do that, use `texturecompare()` instead.
static int api_render3d_texturedata(lua_State*);

/// [-1, +1, -]
/// Returns the bone ID of this vertex. Animated models have multiple bones which can move
/// independently of each other, and this function can be used to find out which bone a vertex
/// belongs to. The returned value may be any integer from 0 to 255, although the game engine
/// actually seems to be unable to handle indices higher than 128. (128 itself is valid.)
///
/// All vertices have bone IDs, even in non-animated models, so plugins may call this function
/// regardless of whether the model is animated or not. For a non-animated model the bone ID seems
/// to be meaningless and is usually 0. To check if the model is animated, use `animated()`.
static int api_render3d_vertexbone(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether this model is animated. Animated models can have
/// multiple bones which can move independently of each other. For more information on bones, see
/// `vertexbone()` and `bonetransforms()`.
static int api_render3d_animated(lua_State*);

/// [-2, +1, -]
/// Transforms this Point by a Transform object and returns a new Point. The original Point object
/// is not modified.
static int api_point_transform(lua_State*);

/// [-1, +3, -]
/// Returns the X, Y and Z values for this point.
static int api_point_get(lua_State*);

/// [-1, +2, -]
/// For a point that's been transformed into screen space, this function returns its X and Y in
/// pixels, with (0, 0) being the top-left of the game view.
static int api_point_aspixels(lua_State*);

/// [-1, +9, -]
/// Decomposes a transform into the following nine floating-point values in this order: translation
/// X, Y and Z, in model coordinates; scale factor X, Y and Z; yaw, pitch and roll, in radians.
///
/// Matrix decomposition is an experimental feature. It assumes the right-most column of the matrix
/// to be (0, 0, 0, 1). That will always be the case in transforms returned by `boneanimation()`,
/// which is the primary intended use of this function.
static int api_transform_decompose(lua_State*);

/// [-1, +16, -]
/// Returns the 16 values that compose this matrix, in row-major order.
static int api_transform_get(lua_State*);

/// [-1, +4, -]
/// Returns the new x, y, width and height that the window was repositioned to.
static int api_repositionevent_xywh(lua_State*);

/// [-1, +1, -]
/// Returns a boolean indicating whether the window changed size. If true, the contents of the
/// window were cleared and need to be redrawn.
static int api_repositionevent_didresize(lua_State*);

/// [-1, +2, -]
/// Returns the x and y for this mouse event.
static int api_mouseevent_xy(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether ctrl was held when this event fired.
static int api_mouseevent_ctrl(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether shift was held when this event fired.
static int api_mouseevent_shift(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether the meta key (also known as super, command, or the
/// "windows key") was held when this event fired.
static int api_mouseevent_meta(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether alt was held when this event fired.
static int api_mouseevent_alt(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether caps lock was on when this event fired.
static int api_mouseevent_capslock(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value indicating whether numlock was on when this event fired.
static int api_mouseevent_numlock(lua_State*);

/// [-1, +1, -]
/// Returns three boolean values indicating whether each primary mouse button was held when this
/// event fired, in the order: left, right, middle.
static int api_mouseevent_mousebuttons(lua_State*);

/// [-1, +1, -]
/// Returns an integer representing the mouse button that was pressed. Possible values are 1 for
/// the left mouse button, 2 for the right mouse button, and 3 for the middle mouse button
/// (clicking the mouse wheel).
static int api_mousebutton_button(lua_State*);

/// [-1, +1, -]
/// Returns a boolean value representing the scroll direction. False means scrolling down, toward
/// the user, and true means scrolling up, away from the user.
static int api_scroll_direction(lua_State*);

/// [-1, +0, -]
/// Closes and destroys the browser. This is the only way for a browser to be destroyed, other than
/// the plugin stopping, which will destroy the browser automatically.
///
/// Do not use the browser object again after calling this function on it.
static int api_browser_close(lua_State*);

/// [-2, +0, -]
/// Sends a message to the browser. The parameter must be a string, or convertible to a string. It
/// will be sent to the browser using the postMessage function, so to handle it in your browser
/// application, just add an event listener for "message" to the window object. The event's data
/// will be an object with "type": "pluginMessage", and "content" will be an ArrayBuffer containing
/// the Lua string that was passed to this function. Note that the string will be transferred
/// exactly as it appeared in Lua, byte-for-byte - it will not be decoded or re-encoded in any way.
static int api_browser_sendmessage(lua_State*);

/// [-1, +0, -]
/// Enables screen capture for this browser. The screen contents will be sent to the browser using
/// the postMessage function. The event's data will be an object with "type": "screenCapture",
/// "width" and "height" will be integers indicating the size of the captured area, and "content"
/// will be an ArrayBuffer of length (width * height * 3). The contents will be three bytes per
/// pixel, in RGB format, in row-major order, starting with the bottom-left pixel.
///
/// The data will be sent using a shared memory mapping, so the overhead is much lower than it
/// would be to send all the data using sendmessage. However, downloading screen contents from the
/// GPU will still slow the game down (takes around 2 to 5 milliseconds depending on window size),
/// so Bolt will limit itself to capturing 4 frames per second via this function.
static int api_browser_enablecapture(lua_State*);

/// [-1, +0, -]
/// Disables screen capture for this browser.
static int api_browser_disablecapture(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this browser for close requests. If the value is a function, it will
/// be called with no parameters when the browser window has requested to close, such as by the
/// user clicking the 'X' button at the top corner of the window. If the value is not a function,
/// it will not be called, and therefore the plugin will not be notified of close requests.
///
/// Bolt takes no default action other than calling this function, which means nothing will happen
/// by default when the user tries to close the window. To enable normal closing behaviour, add a
/// closerequest handler which calls `mybrowser:close()`.
static int api_browser_oncloserequest(lua_State*);

/// [-2, +0, -]
/// Sets an event handler for this browser for message events. If the value is a function, it will
/// be called with one parameter, that being a string. If the value is not a function, it will not
/// be called, and therefore the plugin will not be notified of messages.
///
/// A message event is fired when the browser calls the "send-message" endpoint from Javascript.
static int api_browser_onmessage(lua_State*);

/// [-1, +0, -]
/// Closes and destroys the browser. This is the only way for an embedded browser to be destroyed,
/// other than the plugin stopping, which will destroy the browser automatically.
///
/// Do not use the browser object again after calling this function on it.
static int api_embeddedbrowser_close(lua_State*);

/// [-2, +0, -]
/// Sends a message to the browser. The parameter must be a string, or convertible to a string. It
/// will be sent to the browser using the postMessage function, so to handle it in your browser
/// application, just add an event listener for "message" to the window object. The event's data
/// will be an object with "type": "pluginMessage", and "content" will be an ArrayBuffer containing
/// the Lua string that was passed to this function. Note that the string will be transferred
/// exactly as it appeared in Lua, byte-for-byte - it will not be decoded or re-encoded in any way.
static int api_embeddedbrowser_sendmessage(lua_State*);

/// [-1, +0, -]
/// Enables screen capture for this browser. The screen contents will be sent to the browser using
/// the postMessage function. The event's data will be an object with "type": "screenCapture",
/// "width" and "height" will be integers indicating the size of the captured area, and "content"
/// will be an ArrayBuffer of length (width * height * 3). The contents will be three bytes per
/// pixel, in RGB format, in row-major order, starting with the bottom-left pixel.
///
/// The data will be sent using a shared memory mapping, so the overhead is much lower than it
/// would be to send all the data using sendmessage. However, downloading screen contents from the
/// GPU will still slow the game down (takes around 2 to 5 milliseconds depending on window size),
/// so Bolt will limit itself to capturing 4 frames per second via this function.
static int api_embeddedbrowser_enablecapture(lua_State*);

/// [-1, +0, -]
/// Disables screen capture for this browser.
static int api_embeddedbrowser_disablecapture(lua_State*);

/// [-4, +0, -]
/// Writes an integer into the buffer. The first parameter is the integer itself, the second is the
/// offset in the buffer, and the third is the number of bytes the integer will be truncated to.
/// The integer will be written little-endian.
static int api_buffer_writeinteger(lua_State*);

/// [-3, +0, -]
/// Writes a number into the buffer. The first parameter is the number and the second is the offset
/// in the buffer. The number will be written as a native-endian, 8-byte, double-precision floating
/// point value.
static int api_buffer_writenumber(lua_State*);

/// [-3, +0, -]
/// Writes a string into the buffer. The first parameter is the string and the second is the offset
/// into the buffer where the string should begin.
static int api_buffer_writestring(lua_State*);

/// [-3, +0, -]
/// Writes the contents of another buffer into this buffer. The first parameter is the buffer to be
/// copied from, and the second is the offset in this buffer where it should be copied to.
static int api_buffer_writebuffer(lua_State*);
