# qt-eglfs-fbdev

Qt6 EGLFS device integration plugin that renders via the GPU render node and blits to `/dev/fb0` — no DRM modesetting, no display flash.

Part of the [Librescoot](https://librescoot.org/) open-source platform.

## How it works

Qt's EGLFS platform normally requires a KMS/DRM display device to present frames. This plugin replaces that with a two-step path:

1. EGL rendering happens against a GBM surface on the GPU render node (`/dev/dri/renderD128`)
2. At each `SwapBuffers`, the front buffer is mapped and copied to `/dev/fb0`

The EGL surface uses XRGB8888 color while preserving the application's requested
depth and stencil buffers. Qt Quick uses these for opaque geometry ordering and
clipping, including when rendering directly without an offscreen effect layer.

For the DBC's RGB565 display the copy includes an XRGB8888 → RGB565 conversion per row. 32bpp framebuffers get a straight `memcpy`.

The framebuffer is opened directly for screen geometry, not for modesetting — the display stays in whatever mode the kernel or previous process left it, which means no blank frame between Plymouth and Qt.

## Configuration

| Environment variable | Default | Description |
|---|---|---|
| `EGLFS_FBDEV_RENDER_DEVICE` | `/dev/dri/renderD128` | GPU render node |
| `EGLFS_FBDEV_FB_DEVICE` | `/dev/fb0` | Framebuffer device |

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

Requires Qt6 (Core, Gui, EglFSDeviceIntegrationPrivate), libgbm, and EGL.

For cross-compilation to ARMv7:

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=<armv7-toolchain.cmake> \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The plugin installs to `$QT6_INSTALL_PLUGINS/egldeviceintegrations/` as `libeglfs-fbdev-integration.so`.

## Tests

Host tests additionally require Qt Quick and Qt Test. Run them with a real XCB
OpenGL window, using Xvfb and Mesa software rendering on headless hosts:

```bash
cmake -B build-test -DBUILD_TESTING=ON
cmake --build build-test
QT_QPA_PLATFORM=xcb QSG_RENDER_LOOP=basic LIBGL_ALWAYS_SOFTWARE=1 \
    xvfb-run -a ctest --test-dir build-test --output-on-failure
```

The tests check format preservation and native-window pixels for opaque geometry
and rotated clipping, in light and dark themes, before, during and after an
offscreen layer is enabled. `FBDEV_TEST_CAPTURE_DIR` can name an existing directory
for the captured frames. These host tests do not exercise framebuffer presentation
or the display's hardware overlay.

## Usage

```bash
QT_QPA_PLATFORM=eglfs \
QT_QPA_EGLFS_INTEGRATION=eglfs_fbdev \
EGLFS_FBDEV_RENDER_DEVICE=/dev/dri/renderD128 \
EGLFS_FBDEV_FB_DEVICE=/dev/fb0 \
./your-qt-app
```

## License

Original Librescoot code in this repository is available, at your option, under
[AGPL-3.0-only](LICENSE) or [CC-BY-NC-SA-4.0](LICENSE.CC-BY-NC-SA-4.0).
Third-party code retains its existing license.

The maintainers reserve the right to grant separate licenses for commercial distribution; please contact the maintainers to discuss commercial licensing.

[![CC BY-NC-SA 4.0][cc-by-nc-sa-image]][cc-by-nc-sa]

[cc-by-nc-sa]: http://creativecommons.org/licenses/by-nc-sa/4.0/
[cc-by-nc-sa-image]: https://licensebuttons.net/l/by-nc-sa/4.0/88x31.png
