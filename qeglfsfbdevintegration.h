#ifndef QEGLFSFBDEVINTEGRATION_H
#define QEGLFSFBDEVINTEGRATION_H

#include <private/qeglfsdeviceintegration_p.h>
#include <gbm.h>
#include <linux/fb.h>

QT_BEGIN_NAMESPACE

class QEglFSFbdevIntegration : public QEglFSDeviceIntegration
{
public:
    void platformInit() override;
    void platformDestroy() override;

    EGLNativeDisplayType platformDisplay() const override;
    EGLDisplay createDisplay(EGLNativeDisplayType nativeDisplay) override;

    QSize screenSize() const override;
    int screenDepth() const override;
    QImage::Format screenFormat() const override;
    qreal refreshRate() const override;

    EGLNativeWindowType createNativeWindow(QPlatformWindow *window,
                                           const QSize &size,
                                           const QSurfaceFormat &format) override;
    void destroyNativeWindow(EGLNativeWindowType window) override;

    QSurfaceFormat surfaceFormatFor(const QSurfaceFormat &inputFormat) const override;
    bool filterConfig(EGLDisplay display, EGLConfig config) const override;

    void presentBuffer(QPlatformSurface *surface) override;

private:
    int fb_fd_ = -1;
    uint8_t *fb_map_ = nullptr;
    size_t fb_map_size_ = 0;
    struct fb_var_screeninfo fb_vinfo_ = {};
    struct fb_fix_screeninfo fb_finfo_ = {};
    uint32_t fb_stride_ = 0;
    bool fb_is_rgb565_ = false;

    int render_fd_ = -1;
    mutable gbm_device *gbm_device_ = nullptr;

    gbm_surface *gbm_surface_ = nullptr;

    void ensureGbmDevice() const;
};

QT_END_NAMESPACE

#endif
