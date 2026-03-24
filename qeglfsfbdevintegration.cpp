#include "qeglfsfbdevintegration.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <QtCore/QByteArray>
#include <QtCore/QLoggingCategory>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(qLcFbdev, "qt.qpa.eglfs.fbdev")

void QEglFSFbdevIntegration::ensureGbmDevice() const
{
    if (gbm_device_)
        return;

    QByteArray renderDevice = qgetenv("EGLFS_FBDEV_RENDER_DEVICE");
    if (renderDevice.isEmpty())
        renderDevice = "/dev/dri/renderD128";

    auto *self = const_cast<QEglFSFbdevIntegration *>(this);
    self->render_fd_ = open(renderDevice.constData(), O_RDWR | O_CLOEXEC);
    if (self->render_fd_ < 0) {
        qFatal("eglfs_fbdev: cannot open %s: %s", renderDevice.constData(), strerror(errno));
        return;
    }

    self->gbm_device_ = gbm_create_device(self->render_fd_);
    if (!self->gbm_device_) {
        qFatal("eglfs_fbdev: gbm_create_device failed");
        return;
    }

    qCInfo(qLcFbdev, "GPU render node: %s, GBM device ready", renderDevice.constData());
}

void QEglFSFbdevIntegration::platformInit()
{
    ensureGbmDevice();

    QByteArray fbDevice = qgetenv("EGLFS_FBDEV_FB_DEVICE");
    if (fbDevice.isEmpty())
        fbDevice = "/dev/fb0";

    fb_fd_ = open(fbDevice.constData(), O_RDWR);
    if (fb_fd_ < 0) {
        qFatal("eglfs_fbdev: cannot open %s: %s", fbDevice.constData(), strerror(errno));
        return;
    }

    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &fb_vinfo_) < 0) {
        qFatal("eglfs_fbdev: FBIOGET_VSCREENINFO failed: %s", strerror(errno));
        return;
    }

    if (ioctl(fb_fd_, FBIOGET_FSCREENINFO, &fb_finfo_) < 0) {
        qFatal("eglfs_fbdev: FBIOGET_FSCREENINFO failed: %s", strerror(errno));
        return;
    }

    fb_stride_ = fb_finfo_.line_length;
    fb_is_rgb565_ = (fb_vinfo_.bits_per_pixel == 16
                     && fb_vinfo_.red.offset == 11
                     && fb_vinfo_.green.offset == 5
                     && fb_vinfo_.blue.offset == 0);

    qCInfo(qLcFbdev, "Framebuffer: %ux%u %ubpp stride=%u %s",
           fb_vinfo_.xres, fb_vinfo_.yres, fb_vinfo_.bits_per_pixel,
           fb_stride_, fb_is_rgb565_ ? "RGB565" : "");

    fb_map_size_ = fb_finfo_.smem_len;
    fb_map_ = static_cast<uint8_t *>(
        mmap(nullptr, fb_map_size_, PROT_WRITE, MAP_SHARED, fb_fd_, 0));
    if (fb_map_ == MAP_FAILED) {
        qFatal("eglfs_fbdev: mmap framebuffer failed: %s", strerror(errno));
        fb_map_ = nullptr;
        return;
    }
}

void QEglFSFbdevIntegration::platformDestroy()
{
    if (gbm_surface_) {
        gbm_surface_destroy(gbm_surface_);
        gbm_surface_ = nullptr;
    }
    if (gbm_device_) {
        gbm_device_destroy(gbm_device_);
        gbm_device_ = nullptr;
    }
    if (render_fd_ >= 0) {
        close(render_fd_);
        render_fd_ = -1;
    }
    if (fb_map_) {
        munmap(fb_map_, fb_map_size_);
        fb_map_ = nullptr;
    }
    if (fb_fd_ >= 0) {
        close(fb_fd_);
        fb_fd_ = -1;
    }
    qCInfo(qLcFbdev, "Cleaned up");
}

EGLNativeDisplayType QEglFSFbdevIntegration::platformDisplay() const
{
    ensureGbmDevice();
    return reinterpret_cast<EGLNativeDisplayType>(gbm_device_);
}

EGLDisplay QEglFSFbdevIntegration::createDisplay(EGLNativeDisplayType nativeDisplay)
{
    return eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA,
                                 reinterpret_cast<void *>(nativeDisplay), nullptr);
}

QSize QEglFSFbdevIntegration::screenSize() const
{
    return QSize(fb_vinfo_.xres, fb_vinfo_.yres);
}

int QEglFSFbdevIntegration::screenDepth() const
{
    return 32;
}

QImage::Format QEglFSFbdevIntegration::screenFormat() const
{
    return QImage::Format_RGB32;
}

qreal QEglFSFbdevIntegration::refreshRate() const
{
    return 59.1;
}

QSurfaceFormat QEglFSFbdevIntegration::surfaceFormatFor(const QSurfaceFormat &inputFormat) const
{
    QSurfaceFormat fmt = inputFormat;
    // Ensure we get a config compatible with our GBM surface
    fmt.setRedBufferSize(8);
    fmt.setGreenBufferSize(8);
    fmt.setBlueBufferSize(8);
    fmt.setAlphaBufferSize(0);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    return fmt;
}

bool QEglFSFbdevIntegration::filterConfig(EGLDisplay display, EGLConfig config) const
{
    EGLint nativeVisualId = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &nativeVisualId);
    qCInfo(qLcFbdev, "filterConfig: EGL_NATIVE_VISUAL_ID=0x%x (XRGB8888=0x%x)",
           nativeVisualId, GBM_FORMAT_XRGB8888);
    // Accept only configs whose native visual matches our GBM surface format
    return nativeVisualId == (EGLint)GBM_FORMAT_XRGB8888;
}

EGLNativeWindowType QEglFSFbdevIntegration::createNativeWindow(
    QPlatformWindow *,
    const QSize &size,
    const QSurfaceFormat &)
{
    if (gbm_surface_) {
        qCWarning(qLcFbdev, "Destroying existing GBM surface before creating new one");
        gbm_surface_destroy(gbm_surface_);
    }

    // Try SCANOUT first (needed by some Mesa versions for eglCreateWindowSurface),
    // fall back to RENDERING-only if the device doesn't support SCANOUT
    gbm_surface_ = gbm_surface_create(gbm_device_,
                                        size.width(), size.height(),
                                        GBM_FORMAT_XRGB8888,
                                        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surface_) {
        qCInfo(qLcFbdev, "SCANOUT not supported, trying RENDERING only");
        gbm_surface_ = gbm_surface_create(gbm_device_,
                                            size.width(), size.height(),
                                            GBM_FORMAT_XRGB8888,
                                            GBM_BO_USE_RENDERING);
    }
    if (!gbm_surface_) {
        qCCritical(qLcFbdev, "gbm_surface_create(%dx%d) failed", size.width(), size.height());
        return 0;
    }

    qCInfo(qLcFbdev, "Created GBM surface %dx%d XRGB8888",
           size.width(), size.height());

    return reinterpret_cast<EGLNativeWindowType>(gbm_surface_);
}

void QEglFSFbdevIntegration::destroyNativeWindow(EGLNativeWindowType window)
{
    auto *surface = reinterpret_cast<gbm_surface *>(window);
    if (surface) {
        gbm_surface_destroy(surface);
        if (surface == gbm_surface_)
            gbm_surface_ = nullptr;
    }
}

void QEglFSFbdevIntegration::presentBuffer(QPlatformSurface *)
{
    if (!gbm_surface_ || !fb_map_)
        return;

    auto *bo = gbm_surface_lock_front_buffer(gbm_surface_);
    if (!bo) {
        qCWarning(qLcFbdev, "Failed to lock GBM front buffer");
        return;
    }

    uint32_t stride = 0;
    void *map_data = nullptr;
    void *bo_data = gbm_bo_map(bo, 0, 0,
                                gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                                GBM_BO_TRANSFER_READ, &stride, &map_data);
    if (!bo_data) {
        qCWarning(qLcFbdev, "Failed to map GBM buffer");
        gbm_surface_release_buffer(gbm_surface_, bo);
        return;
    }

    const uint32_t src_width = gbm_bo_get_width(bo);
    const uint32_t src_height = gbm_bo_get_height(bo);
    const uint32_t dst_width = fb_vinfo_.xres;
    const uint32_t dst_height = fb_vinfo_.yres;
    const uint32_t copy_width = qMin(src_width, dst_width);
    const uint32_t copy_height = qMin(src_height, dst_height);

    const auto *src = static_cast<const uint8_t *>(bo_data);
    auto *dst = fb_map_;

    if (fb_is_rgb565_) {
        for (uint32_t y = 0; y < copy_height; y++) {
            const auto *src_row = reinterpret_cast<const uint32_t *>(src + y * stride);
            auto *dst_row = reinterpret_cast<uint16_t *>(dst + y * fb_stride_);
            for (uint32_t x = 0; x < copy_width; x++) {
                const uint32_t pixel = src_row[x];
                const uint8_t r = (pixel >> 16) & 0xFF;
                const uint8_t g = (pixel >> 8) & 0xFF;
                const uint8_t b = pixel & 0xFF;
                dst_row[x] = static_cast<uint16_t>(
                    ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else if (fb_vinfo_.bits_per_pixel == 32) {
        const uint32_t row_bytes = copy_width * 4;
        for (uint32_t y = 0; y < copy_height; y++) {
            memcpy(dst + y * fb_stride_, src + y * stride, row_bytes);
        }
    } else {
        qCWarning(qLcFbdev, "Unsupported framebuffer format: %ubpp",
                  fb_vinfo_.bits_per_pixel);
    }

    gbm_bo_unmap(bo, map_data);
    gbm_surface_release_buffer(gbm_surface_, bo);
}

QT_END_NAMESPACE
