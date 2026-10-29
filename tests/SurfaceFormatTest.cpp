#include "qeglfsfbdevintegration.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QSignalSpy>
#include <QtTest>

class SurfaceFormatTest : public QObject
{
    Q_OBJECT

private slots:
    void preservesRequestedBuffers_data()
    {
        QTest::addColumn<int>("depth");
        QTest::addColumn<int>("stencil");
        QTest::newRow("unspecified") << -1 << -1;
        QTest::newRow("none") << 0 << 0;
        QTest::newRow("depth16") << 16 << 0;
        QTest::newRow("depth16-stencil8") << 16 << 8;
        QTest::newRow("depth24-stencil8") << 24 << 8;
        QTest::newRow("depth32-stencil8") << 32 << 8;
    }

    void preservesRequestedBuffers()
    {
        QFETCH(int, depth);
        QFETCH(int, stencil);
        QSurfaceFormat input;
        input.setRedBufferSize(5);
        input.setGreenBufferSize(6);
        input.setBlueBufferSize(5);
        input.setAlphaBufferSize(8);
        input.setDepthBufferSize(depth);
        input.setStencilBufferSize(stencil);
        input.setRenderableType(QSurfaceFormat::OpenGLES);
        input.setVersion(3, 0);
        input.setSamples(4);
        input.setSwapInterval(2);
        input.setSwapBehavior(QSurfaceFormat::DoubleBuffer);

        QSurfaceFormat expected = input;
        expected.setRedBufferSize(8);
        expected.setGreenBufferSize(8);
        expected.setBlueBufferSize(8);
        expected.setAlphaBufferSize(0);
        QEglFSFbdevIntegration integration;
        QCOMPARE(integration.surfaceFormatFor(input), expected);
    }

    void rendersOpaqueGeometry_data()
    {
        QTest::addColumn<QColor>("background");
        QTest::addColumn<QColor>("foreground");
        QTest::newRow("dark") << QColor(Qt::black) << QColor(Qt::white);
        QTest::newRow("light") << QColor(Qt::white) << QColor(Qt::black);
    }

    void rendersOpaqueGeometry()
    {
        QFETCH(QColor, background);
        QFETCH(QColor, foreground);
        QQuickView view;
        QEglFSFbdevIntegration integration;
        auto requested = view.requestedFormat();
        requested.setDepthBufferSize(24);
        requested.setStencilBufferSize(8);
        view.setFormat(integration.surfaceFormatFor(requested));

        QQmlComponent component(view.engine());
        component.setData(R"QML(
import QtQuick
Rectangle {
    width: 240; height: 180
    property color foreground: "white"
    property bool layered: false
    layer.enabled: layered
    Rectangle { x: 40; y: 40; width: 40; height: 80; color: parent.foreground }
    Item {
        x: 140; y: 50; width: 40; height: 40
        rotation: 45
        clip: true
        Rectangle { x: -30; y: -30; width: 100; height: 100; color: "lime" }
    }
})QML", QUrl());
        auto *root = qobject_cast<QQuickItem *>(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("color", background);
        root->setProperty("foreground", foreground);
        view.setContent(QUrl(), &component, root);
        view.resize(240, 180);
        QSignalSpy frames(&view, &QQuickWindow::frameSwapped);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Read the native window, not QQuickWindow::grabWindow(): an offscreen
        // capture can supply its own depth buffer and hide this regression.
        int stage = 0;
        for (bool layered : {false, true, false}) {
            frames.clear();
            root->setProperty("layered", layered);
            view.update();
            QTRY_VERIFY(frames.count() > 0);
            const QImage image = view.screen()->grabWindow(view.winId()).toImage();
            QVERIFY(!image.isNull());
            const auto captureDir = qEnvironmentVariable("FBDEV_TEST_CAPTURE_DIR");
            if (!captureDir.isEmpty()) {
                QVERIFY(image.save(QStringLiteral("%1/%2-%3.png")
                    .arg(captureDir, QTest::currentDataTag()).arg(stage)));
            }
            QCOMPARE(image.pixelColor(60, 60), foreground);
            QCOMPARE(image.pixelColor(10, 10), background);
            QCOMPARE(image.pixelColor(160, 70), QColor(Qt::green));
            QCOMPARE(image.pixelColor(140, 50), background);
            ++stage;
        }
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    SurfaceFormatTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SurfaceFormatTest.moc"
