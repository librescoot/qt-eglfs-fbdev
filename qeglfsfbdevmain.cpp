#include "qeglfsfbdevintegration.h"

QT_BEGIN_NAMESPACE

class QEglFSFbdevIntegrationPlugin : public QEglFSDeviceIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QEglFSDeviceIntegrationFactoryInterface_iid
                       FILE "eglfs_fbdev.json")

public:
    QEglFSDeviceIntegration *create() override
    {
        return new QEglFSFbdevIntegration;
    }
};

QT_END_NAMESPACE

#include "qeglfsfbdevmain.moc"
