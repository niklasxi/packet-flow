#pragma once
#include "session.hpp"
#include <QPainterPath>
#include <QQuickPaintedItem>

class PacketMap : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Session *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QRectF legendArea READ legendArea WRITE setLegendArea)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY viewChanged)
  public:
    explicit PacketMap(QQuickItem *parent = nullptr);
    Session *session() const { return owner; }
    void setSession(Session *);
    QRectF legendArea() const { return legend; }
    void setLegendArea(const QRectF &);
    int zoomPercent() const { return qRound(zoom * 100); }
    Q_INVOKABLE void fit();
    Q_INVOKABLE void resetLayout();
    Q_INVOKABLE void scaleBy(double);
    void paint(QPainter *) override;
  signals:
    void sessionChanged();
    void viewChanged();

  protected:
    void geometryChange(const QRectF &, const QRectF &) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void hoverMoveEvent(QHoverEvent *) override;

  private:
    struct Card {
        Session::Node node;
        QRectF rect;
        int icon;
    };
    struct Stroke {
        QPainterPath line;
        QPolygonF head;
        QColor color;
        qreal width, alpha;
        bool selected;
        uint64_t id;
    };
    Session *owner = nullptr;
    std::vector<Card> cards;
    std::vector<Stroke> strokes;
    struct Curve {
        QPointF a, b, c, d;
        std::array<double, 41> distance;
    };
    std::map<std::tuple<size_t, size_t, int>, Curve> curves;
    QPointF pan, press, last;
    std::map<pf::Address, QPointF> manualCenters;
    struct RingSlot {
        QRectF rect;
        pf::Address peer;
    };
    std::vector<RingSlot> ringSlots;
    QRectF focusRect;
    pf::Address dragged;
    QPointF dragOffset;
    QSizeF layoutSize;
    QRectF legend;
    bool automaticView = true, locationCards = false;
    double zoom = 1;
    bool moved = false, reduced = false;
    void syncNodes();
    void syncArrows();
    QRectF layoutArea() const;
    void fitCards(bool includeVacancies);
    QTransform transform() const;
    void scaleAt(double, QPointF);
    const Card *hitNode(QPointF) const;
    uint64_t hitArrow(QPointF) const;
};
