#include "map.hpp"
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <algorithm>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("PacketFlow");
    app.setApplicationName("Packet Flow");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<PacketMap>("PacketFlow", 1, 0, "PacketMap");
    qmlRegisterUncreatableType<Session>("PacketFlow", 1, 0, "PacketSession", "Provided by the application");
    Session session;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("flow", &session);
    engine.load(QUrl("qrc:/ui/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto args = app.arguments();
    auto i = args.indexOf("--screenshot");
    if (i >= 0 && i + 1 < args.size()) {
        session.previewAt(0.48);
        QTimer::singleShot(650, &app,
                           [&, path = args[i + 1]] { app.exit(window->grabWindow().save(path) ? 0 : 2); });
    }
    if (args.contains("--measure-demo")) {
        static QElapsedTimer timer;
        static std::vector<double> gaps;
        QObject::connect(
            window, &QQuickWindow::frameSwapped, &app,
            [] {
                if (timer.isValid())
                    gaps.push_back(timer.nsecsElapsed() / 1e6);
                timer.start();
            },
            Qt::QueuedConnection);
        QTimer::singleShot(8000, &app, [&] {
            std::sort(gaps.begin(), gaps.end());
            if (!gaps.empty())
                std::printf("%zu frame intervals · median %.2f ms · p95 %.2f ms\n", gaps.size(),
                            gaps[gaps.size() / 2], gaps[gaps.size() * 95 / 100]);
            app.exit(gaps.empty() ? 1 : 0);
        });
    }
    return app.exec();
}
