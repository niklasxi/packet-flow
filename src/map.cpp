#include "map.hpp"
#include <QCursor>
#include <QFontDatabase>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
QPointF lerp(QPointF a, QPointF b, double t) { return a + (b - a) * t; }
QPointF cubic(QPointF a, QPointF b, QPointF c, QPointF d, double t) {
    auto e = lerp(a, b, t), f = lerp(b, c, t), g = lerp(c, d, t);
    return lerp(lerp(e, f, t), lerp(f, g, t), t);
}
QPointF normalized(QPointF p) { return p / std::max(0.0001, std::hypot(p.x(), p.y())); }
QPointF edge(QRectF r, QPointF toward) {
    auto v = toward - r.center();
    auto ratio = std::max(std::abs(v.x()) / (r.width() / 2), std::abs(v.y()) / (r.height() / 2));
    auto p = r.center() + v / std::max(0.001, ratio);
    if (std::abs(p.y() - r.top()) < 0.1 || std::abs(p.y() - r.bottom()) < 0.1)
        p.setX(std::clamp(p.x(), r.left() + 12, r.right() - 12));
    else
        p.setY(std::clamp(p.y(), r.top() + 10, r.bottom() - 10));
    return p;
}
// A wide map can use oval rings to keep horizontal labels readable.
std::vector<QPointF> ringOffsets(int peers, QSizeF peer, QSizeF focus, QSizeF viewport) {
    std::vector<QPointF> offsets;
    offsets.reserve(peers);

    const double aspect = std::clamp(viewport.width() / viewport.height(), 1.0, 1.8);
    int rings = 1;
    while (peers > 8 * rings * (rings + 1) / 2)
        ++rings;
    const int weights = rings * (rings + 1) / 2;
    // Divide peers in proportion to circumference so even a partly filled
    // outer ring wraps all the way around the map.
    for (int ring = 1; ring <= rings; ++ring) {
        const int throughRing = qRound(double(peers) * ring * (ring + 1) / (2 * weights));
        const int count = throughRing - int(offsets.size());
        const double step = 2 * std::numbers::pi / count;
        const double start = std::numbers::pi / 4 + (ring % 2 == 0 ? step / 2 : 0);
        auto addSlot = [&](int index) {
            const double angle = start - index * step;
            offsets.emplace_back(ring * aspect * std::cos(angle), ring * std::sin(angle));
        };
        // Bisect the gaps so the first arrivals spread around the ring too.
        addSlot(0);
        std::vector<std::pair<int, int>> gaps{{0, count}};
        for (size_t i = 0; i < gaps.size(); ++i) {
            auto [first, last] = gaps[i];
            if (last - first <= 1)
                continue;
            int middle = (first + last) / 2;
            addSlot(middle);
            gaps.emplace_back(first, middle);
            gaps.emplace_back(middle, last);
        }
    }

    // Scale the rings just enough that each pair of upright cards clears on
    // at least one axis. Include the larger focus card in the center.
    auto separation = [](QPointF delta, QSizeF required) {
        return std::min((required.width() + 18) / std::max(1e-9, std::abs(delta.x())),
                        (required.height() + 14) / std::max(1e-9, std::abs(delta.y())));
    };
    double spacing = rings == 1 ? std::min(220.0, viewport.height() * 0.35) : 0;
    for (size_t i = 0; i < offsets.size(); ++i) {
        spacing = std::max(spacing, separation(offsets[i], (peer + focus) / 2));
        for (size_t j = 0; j < i; ++j)
            spacing = std::max(spacing, separation(offsets[i] - offsets[j], peer));
    }
    for (auto &point : offsets)
        point *= spacing;
    return offsets;
}
QFont font(int size, bool bold = false, bool mono = false) {
    QFont f = mono ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                   : QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    f.setPixelSize(size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return f;
}
void device(QPainter &p, QRectF box, int kind, QColor ink) {
    p.save();
    p.translate(box.center());
    p.scale(box.width() / 32, box.height() / 32);
    p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (kind == 0 || kind == 4) {
        p.drawRoundedRect(QRectF(-10, -8, 20, 14), 1.6, 1.6);
        QPolygonF base{{-11, 7}, {-13, 9}, {13, 9}, {11, 7}};
        p.drawPolyline(base);
    } else if (kind == 3) {
        p.drawRoundedRect(QRectF(-11, 2, 22, 7), 2.2, 2.2);
        p.drawPoint(QPointF(-7, 5.5));
        p.drawPoint(QPointF(7, 5.5));
        for (int r : {5, 9}) {
            QPainterPath arc;
            arc.moveTo(-r, -5 + (9 - r));
            arc.quadTo(0, -12 + (9 - r), r, -5 + (9 - r));
            p.drawPath(arc);
        }
    } else {
        for (int y : {-9, 2}) {
            p.drawRoundedRect(QRectF(-10, y, 20, 7.5), 1.8, 1.8);
            p.drawPoint(QPointF(-6, y + 3.7));
            p.drawPoint(QPointF(-2, y + 3.7));
        }
    }
    p.restore();
}
} // namespace
PacketMap::PacketMap(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setFillColor(QColor());
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
}
void PacketMap::setSession(Session *s) {
    if (owner == s)
        return;
    if (owner)
        disconnect(owner, nullptr, this, nullptr);
    owner = s;
    if (owner) {
        connect(s, &Session::frame, this, &PacketMap::syncArrows);
        connect(s, &Session::topologyChanged, this, &PacketMap::syncNodes);
        connect(s, &Session::stateChanged, this, [this] {
            if (reduced != owner->reduced())
                syncNodes();
        });
        connect(s, &Session::newSession, this, &PacketMap::resetLayout);
    }
    resetLayout();
    emit sessionChanged();
}
QTransform PacketMap::transform() const {
    QTransform t;
    t.translate(width() / 2 + pan.x(), height() / 2 + pan.y());
    t.scale(zoom, zoom);
    t.translate(-width() / 2, -height() / 2);
    return t;
}
QRectF PacketMap::layoutArea() const {
    return boundingRect().adjusted(18, std::min(72., height() / 5), -18, -std::min(54., height() / 6));
}
void PacketMap::setLegendArea(const QRectF &rect) {
    if (legend == rect)
        return;
    legend = rect;
    if (automaticView)
        fitCards(true);
}
void PacketMap::fit() {
    automaticView = true;
    fitCards(false);
}
void PacketMap::fitCards(bool includeVacancies) {
    std::vector<QRectF> bounds;
    if (includeVacancies && !ringSlots.empty()) {
        bounds.push_back(focusRect);
        for (const auto &slot : ringSlots)
            bounds.push_back(slot.rect);
    }
    for (const auto &card : cards)
        bounds.push_back(card.rect);
    pan = {};
    zoom = 1;
    const auto target = layoutArea();
    if (!bounds.empty() && !target.isEmpty()) {
        QRectF extent;
        for (const auto &rect : bounds)
            extent = extent.united(rect);
        const auto center = boundingRect().center();
        const auto origin = manualCenters.empty() ? center : extent.center();
        const double halfWidth = std::max(origin.x() - extent.left(), extent.right() - origin.x());
        const double halfHeight = std::max(origin.y() - extent.top(), extent.bottom() - origin.y());
        zoom = std::min({1., target.width() / (2 * halfWidth), target.height() / (2 * halfHeight)});
        // Only the lower-left corner contains the legend. Allow other cards
        // to use the rest of that horizontal band at full size.
        if (!legend.isEmpty()) {
            const auto corner = legend.adjusted(-8, -8, 8, 8).topRight();
            for (const auto &rect : bounds) {
                const auto relative = rect.translated(-origin);
                if (relative.left() < 0 && relative.bottom() > 0) {
                    const double beside = (target.center().x() - corner.x()) / -relative.left();
                    const double above = (corner.y() - target.center().y()) / relative.bottom();
                    if (beside > 0 || above > 0)
                        zoom = std::min(zoom, std::max(beside, above));
                }
            }
        }
        pan = target.center() - center - (origin - center) * zoom;
    }
    emit viewChanged();
    update();
}
void PacketMap::resetLayout() {
    manualCenters.clear();
    ringSlots.clear();
    cards.clear();
    curves.clear();
    strokes.clear();
    dragged = {};
    automaticView = true;
    syncNodes();
    if (ringSlots.empty())
        fit();
}
void PacketMap::scaleAt(double factor, QPointF at) {
    automaticView = false;
    auto old = transform().inverted().map(at);
    zoom = std::clamp(zoom * factor, 0.025, 3.0);
    pan += at - transform().map(old);
    emit viewChanged();
    update();
}
void PacketMap::scaleBy(double factor) { scaleAt(factor, {width() / 2, height() / 2}); }
void PacketMap::geometryChange(const QRectF &n, const QRectF &o) {
    QQuickPaintedItem::geometryChange(n, o);
    syncNodes();
}
void PacketMap::syncNodes() {
    if (!owner || width() <= 0 || height() <= 0)
        return;
    reduced = owner->reduced();
    auto nodes = owner->nodes();
    // Assign simultaneous arrivals in a deterministic order.
    std::sort(nodes.begin(), nodes.end(), [](const auto &a, const auto &b) {
        return a.focus != b.focus ? a.focus : a.address < b.address;
    });
    std::erase_if(manualCenters, [&](const auto &entry) { return !owner->hasEndpoint(entry.first); });
    const bool rebuildRings = size() != layoutSize || ringSlots.size() != size_t(owner->peerLimit()) ||
                              locationCards != owner->geo()->enabled();
    bool sameLayout = !rebuildRings && nodes.size() == cards.size() &&
                      std::equal(nodes.begin(), nodes.end(), cards.begin(), [](auto &node, auto &card) {
                          return node.address == card.node.address && node.focus == card.node.focus;
                      });
    if (sameLayout) {
        for (size_t i = 0; i < nodes.size(); ++i)
            cards[i].node = std::move(nodes[i]);
        syncArrows();
        return;
    }
    curves.clear();
    cards.clear();
    const auto center = boundingRect().center();
    if (rebuildRings) {
        layoutSize = size();
        locationCards = owner->geo()->enabled();
        bool compact = width() < 720;
        const QSizeF peer = locationCards ? QSizeF(compact ? 160 : 210, 76) : QSizeF(compact ? 132 : 159, 58);
        const QSizeF focus = locationCards ? QSizeF(compact ? 166 : 210, 78) : QSizeF(compact ? 153 : 184, 66);
        const auto offsets = ringOffsets(owner->peerLimit(), peer, focus,
                                         layoutArea().size().expandedTo(QSizeF(1, 1)));
        focusRect = QRectF(QPointF{}, focus);
        focusRect.moveCenter(center);
        ringSlots.resize(offsets.size());
        for (size_t i = 0; i < offsets.size(); ++i) {
            ringSlots[i].rect.setSize(peer);
            ringSlots[i].rect.moveCenter(center + offsets[i]);
        }
    }

    std::set<pf::Address> visiblePeers;
    for (const auto &node : nodes)
        if (!node.focus)
            visiblePeers.insert(node.address);
    for (auto &slot : ringSlots)
        if (slot.peer && !owner->hasEndpoint(slot.peer))
            slot.peer = {};

    for (auto &node : nodes) {
        auto rect = focusRect;
        if (!node.focus) {
            auto slot = std::find_if(ringSlots.begin(), ringSlots.end(),
                                     [&](const auto &s) { return s.peer == node.address; });
            if (slot == ringSlots.end()) {
                slot = std::find_if(ringSlots.begin(), ringSlots.end(), [](const auto &s) { return !s.peer; });
                // Prefer unused slots so a temporarily hidden peer can reclaim its place.
                if (slot == ringSlots.end())
                    slot = std::find_if(ringSlots.begin(), ringSlots.end(),
                                        [&](const auto &s) { return !visiblePeers.contains(s.peer); });
                Q_ASSERT(slot != ringSlots.end());
                slot->peer = node.address;
            }
            rect = slot->rect;
        }
        if (auto it = manualCenters.find(node.address); it != manualCenters.end())
            rect.moveCenter(center + it->second);
        int icon = node.local                      ? 0
                   : node.label == "Local gateway" ? 3
                   : node.label == "IPv6 peer"     ? 4
                   : node.label == "DNS resolver"  ? 1
                                                   : 2;
        cards.push_back({node, rect, icon});
    }
    if (automaticView && rebuildRings)
        fitCards(true);
    syncArrows();
}
void PacketMap::syncArrows() {
    strokes.clear();
    if (!owner)
        return;
    if (curves.size() > 2 * (pf::Scheduler::budget + 1))
        curves.clear();
    for (auto &s : owner->arrows()) {
        auto find = [&](pf::Address a) {
            return std::find_if(cards.begin(), cards.end(), [&](auto &k) { return k.node.address == a; });
        };
        auto i = find(s.arrow.from), j = find(s.arrow.to);
        if (i == cards.end() || j == cards.end())
            continue;
        auto key = std::tuple{size_t(i - cards.begin()), size_t(j - cards.begin()), s.arrow.lane};
        auto [it, needed] = curves.try_emplace(key);
        auto &c = it->second;
        if (needed) {
            auto a = i->rect.center(), b = j->rect.center();
            if (i == j) {
                c.a = {i->rect.center().x() - 20, i->rect.top()};
                c.d = {i->rect.center().x() + 20, i->rect.top()};
                c.b = c.a + QPointF(-35, -64 - s.arrow.lane * 12);
                c.c = c.d + QPointF(35, -64 - s.arrow.lane * 12);
            } else {
                auto dir = normalized(b - a);
                QPointF normal(-dir.y(), dir.x());
                double span = std::hypot(b.x() - a.x(), b.y() - a.y());
                double bend = std::min(span * 0.20, 68.) + s.arrow.lane * 14;
                c.b = lerp(a, b, 0.30) + normal * bend;
                c.c = lerp(a, b, 0.72) - normal * (bend * 0.18) + normal * s.arrow.lane * 13;
                c.a = edge(i->rect, c.b);
                c.d = edge(j->rect, c.c);
            }
            c.distance[0] = 0;
            auto last = c.a;
            for (int k = 1; k <= 40; ++k) {
                auto p = cubic(c.a, c.b, c.c, c.d, k / 40.);
                c.distance[k] = c.distance[k - 1] + QLineF(last, p).length();
                last = p;
            }
        }
        auto length = c.distance.back() * s.phase.progress;
        auto k = std::clamp<int>(
            int(std::lower_bound(c.distance.begin(), c.distance.end(), length) - c.distance.begin()), 1, 40);
        auto t =
            ((k - 1) + (length - c.distance[k - 1]) / std::max(.001, c.distance[k] - c.distance[k - 1])) /
            40.;
        auto e = lerp(c.a, c.b, t), f = lerp(c.b, c.c, t), g = lerp(c.c, c.d, t);
        auto h = lerp(e, f, t), l = lerp(f, g, t), tip = lerp(h, l, t);
        double w = pf::widthFor(s.arrow.bytes);
        auto dir = normalized(l - h);
        auto normal = QPointF(-dir.y(), dir.x());
        double head = std::min(length * .78, 8 + w * 1.5);
        // End the shaft inside the triangle so the cap never protrudes beyond its tip.
        double baseLen = std::max(0., length - head * .65);
        int bk = std::clamp<int>(
            int(std::lower_bound(c.distance.begin(), c.distance.end(), baseLen) - c.distance.begin()), 1, 40);
        double bt = ((bk - 1) +
                     (baseLen - c.distance[bk - 1]) / std::max(.001, c.distance[bk] - c.distance[bk - 1])) /
                    40.;
        auto be = lerp(c.a, c.b, bt), bf = lerp(c.b, c.c, bt), bg = lerp(c.c, c.d, bt), bh = lerp(be, bf, bt);
        QPainterPath line(c.a);
        line.cubicTo(be, bh, lerp(bh, lerp(bf, bg, bt), bt));
        auto base = tip - dir * head;
        QPolygonF polygon{tip, base + normal * (head * .45), base - normal * (head * .45)};
        strokes.push_back({line, polygon, QColor(pf::style(s.arrow.proto).color), w,
                           s.phase.opacity * (s.dim ? .25 : 1.), s.selected, s.arrow.id});
    }
    update();
}
void PacketMap::paint(QPainter *p) {
    p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(boundingRect(), QColor("#f6f9fd"));
    p->save();
    p->setTransform(transform(), true);
    QRectF visible = transform().inverted().mapRect(boundingRect());
    p->setPen(QPen(QColor("#e7edf6"), 0.7));
    for (double x = std::floor(visible.left() / 38) * 38; x < visible.right(); x += 38)
        p->drawLine(QPointF(x, visible.top()), QPointF(x, visible.bottom()));
    for (double y = std::floor(visible.top() / 38) * 38; y < visible.bottom(); y += 38)
        p->drawLine(QPointF(visible.left(), y), QPointF(visible.right(), y));
    for (const auto &s : strokes) {
        QPainterPathStroker stroke;
        stroke.setWidth(s.width);
        stroke.setCapStyle(Qt::FlatCap);
        stroke.setJoinStyle(Qt::RoundJoin);
        auto shape = stroke.createStroke(s.line);
        QPainterPath head;
        head.addPolygon(s.head);
        head.closeSubpath();
        shape = shape.united(head);
        p->setOpacity(s.alpha);
        p->setPen(s.selected ? QPen(s.color.darker(125), 0.85) : QPen(Qt::NoPen));
        p->setBrush(s.color);
        p->drawPath(shape);
    }
    p->setOpacity(1);
    for (auto &card : cards) {
        auto &n = card.node;
        auto r = card.rect;
        int k = card.icon;
        QColor fg(k == 1   ? "#8359c5"
                  : k == 2 ? "#237da6"
                  : k == 3 ? "#208c81"
                  : k == 4 ? "#3d9274"
                           : "#3f66a3");
        QColor bg(k == 1   ? "#f0eafb"
                  : k == 2 ? "#e5f3fc"
                  : k == 3 ? "#e4f4f0"
                  : k == 4 ? "#e4f5ec"
                           : "#e7eefb");
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(37, 60, 96, 9));
        p->drawRoundedRect(r.translated(0, 2), 11, 11);
        p->setPen(QPen(QColor(n.selected  ? "#7550bd"
                              : n.focus   ? "#5676ab"
                              : n.related ? "#99b0d0"
                                          : "#c3d1e3"),
                       n.focus || n.selected ? 1.55 : 1.0));
        p->setBrush(Qt::white);
        p->drawRoundedRect(r, 11, 11);
        const double titleCenter = r.center().y() - (n.location.available() ? 9 : 0);
        QRectF icon(r.left() + 10, titleCenter - 16, 32, 32);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(icon, 8, 8);
        device(*p, icon.adjusted(2, 2, -2, -2), k, fg);
        auto tx = r.left() + 49;
        double available = r.right() - tx - 6;
        p->setPen(QColor("#192e50"));
        p->setFont(font(n.focus ? 15 : 13, true));
        auto label = p->fontMetrics().elidedText(n.label, Qt::ElideMiddle, int(available));
        p->drawText(QPointF(tx, titleCenter - 2), label);
        p->setPen(QColor("#526b89"));
        p->setFont(font(10, false, reduced || n.label != n.addressText));
        p->drawText(QPointF(tx, titleCenter + 15),
                    p->fontMetrics().elidedText(n.subtitle(reduced), Qt::ElideMiddle, int(available)));
        if (n.location.available()) {
            p->setFont(font(11, false));
            p->drawText(QPointF(r.left() + 10, r.bottom() - 9),
                        p->fontMetrics().elidedText(n.location.label(), Qt::ElideRight, int(r.width() - 20)));
        }
    }
    p->restore();
}
const PacketMap::Card *PacketMap::hitNode(QPointF p) const {
    p = transform().inverted().map(p);
    for (auto it = cards.rbegin(); it != cards.rend(); ++it)
        if (it->rect.contains(p))
            return &*it;
    return nullptr;
}
uint64_t PacketMap::hitArrow(QPointF p) const {
    p = transform().inverted().map(p);
    for (auto it = strokes.rbegin(); it != strokes.rend(); ++it) {
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(10., it->width + 6));
        if (stroker.createStroke(it->line).contains(p) || it->head.containsPoint(p, Qt::WindingFill))
            return it->id;
    }
    return 0;
}
void PacketMap::mousePressEvent(QMouseEvent *e) {
    press = last = e->position();
    moved = false;
    dragged = {};
    if (auto *card = hitNode(press)) {
        dragged = card->node.address;
        dragOffset = card->rect.center() - transform().inverted().map(press);
    }
    e->accept();
}
void PacketMap::mouseMoveEvent(QMouseEvent *e) {
    auto pt = e->position();
    if (moved || QLineF(press, pt).length() > 4) {
        automaticView = false;
        if (dragged) {
            auto it = std::find_if(cards.begin(), cards.end(),
                                   [&](auto &card) { return card.node.address == dragged; });
            if (it != cards.end()) {
                auto center = transform().inverted().map(pt) + dragOffset;
                it->rect.moveCenter(center);
                manualCenters[dragged] = center - boundingRect().center();
                curves.clear();
                syncArrows();
            }
        } else {
            pan += pt - (moved ? last : press);
            update();
        }
        moved = true;
        setCursor(Qt::ClosedHandCursor);
    }
    last = pt;
    e->accept();
}
void PacketMap::mouseReleaseEvent(QMouseEvent *e) {
    if (!moved && owner) {
        if (auto *c = hitNode(e->position()))
            owner->selectPeer(c->node.addressText);
        else if (auto id = hitArrow(e->position()))
            owner->selectPacket(id);
    }
    dragged = {};
    e->accept();
}
void PacketMap::wheelEvent(QWheelEvent *e) {
    scaleAt(std::exp(std::clamp(e->angleDelta().y() / 600., -.5, .5)), e->position());
    e->accept();
}
void PacketMap::hoverMoveEvent(QHoverEvent *e) {
    setCursor(hitNode(e->position()) || hitArrow(e->position()) ? Qt::PointingHandCursor
                                                                : Qt::OpenHandCursor);
}
