import QtQuick

Canvas {
    id: icon
    property string symbol: ""
    property color ink: "#314a6d"
    implicitWidth: 18
    implicitHeight: 18
    onInkChanged: requestPaint()
    onSymbolChanged: requestPaint()
    onPaint: {
        let c = getContext("2d");
        c.reset();
        c.scale(width / 24, height / 24);
        c.strokeStyle = ink;
        c.fillStyle = ink;
        c.lineWidth = 1.65;
        c.lineJoin = "round";
        c.lineCap = "round";
        function path(points, fill) {
            c.beginPath();
            c.moveTo(points[0], points[1]);
            for (let i = 2; i < points.length; i += 2)
                c.lineTo(points[i], points[i + 1]);
            if (fill) {
                c.closePath();
                c.fill();
            } else
                c.stroke();
        }
        function box(x, y, w, h) {
            c.strokeRect(x, y, w, h);
        }
        function ring(x, y, r) {
            c.beginPath();
            c.arc(x, y, r, 0, 2 * Math.PI);
            c.stroke();
        }
        switch (symbol) {
        case "logo":
            path([1, 16, 6, 16, 10, 4, 14, 20, 18, 9, 23, 9]);
            break;
        case "down":
            path([6, 9, 12, 15, 18, 9]);
            break;
        case "right":
            path([9, 5, 16, 12, 9, 19]);
            break;
        case "play":
            path([7, 4, 20, 12, 7, 20], true);
            break;
        case "pause":
            c.fillRect(6, 4, 4, 16);
            c.fillRect(14, 4, 4, 16);
            break;
        case "stop":
            c.fillRect(5, 5, 14, 14);
            break;
        case "restart":
            path([4, 5, 4, 11, 10, 11]);
            c.beginPath();
            c.arc(12, 12, 8, 3.5, 8.7);
            c.stroke();
            break;
        case "filter":
            path([3, 5, 21, 5, 14, 13, 14, 19, 10, 21, 10, 13, 3, 5]);
            break;
        case "map":
            ring(5, 7, 2.3);
            ring(19, 6, 2.3);
            ring(12, 19, 2.3);
            path([6, 10, 11, 16]);
            path([18, 9, 13, 16]);
            break;
        case "table":
            box(3, 4, 18, 16);
            path([3, 9, 21, 9]);
            path([3, 14, 21, 14]);
            path([9, 4, 9, 20]);
            break;
        case "panel":
            box(3, 4, 18, 16);
            path([15, 4, 15, 20]);
            break;
        case "fit":
            path([4, 10, 4, 4, 10, 4]);
            path([14, 4, 20, 4, 20, 10]);
            path([20, 14, 20, 20, 14, 20]);
            path([10, 20, 4, 20, 4, 14]);
            break;
        case "minus":
            path([5, 12, 19, 12]);
            break;
        case "plus":
            path([5, 12, 19, 12]);
            path([12, 5, 12, 19]);
            break;
        case "info":
            ring(12, 12, 9);
            path([12, 11, 12, 17]);
            c.fillRect(11, 7, 2, 2);
            break;
        case "clock":
            ring(12, 12, 9);
            path([12, 6, 12, 12, 16, 14]);
            break;
        case "layers":
            path([3, 9, 12, 4, 21, 9, 12, 14, 3, 9]);
            path([3, 13, 12, 18, 21, 13]);
            path([3, 17, 12, 22, 21, 17]);
            break;
        case "out":
            path([4, 8, 19, 8, 15, 4]);
            path([20, 16, 5, 16, 9, 20]);
            break;
        case "nodes":
            ring(12, 5, 2);
            ring(5, 18, 2);
            ring(19, 18, 2);
            path([12, 8, 12, 12, 5, 12, 5, 15]);
            path([12, 12, 19, 12, 19, 15]);
            break;
        case "close":
            path([6, 6, 18, 18]);
            path([6, 18, 18, 6]);
            break;
        case "copy":
            box(8, 8, 12, 13);
            path([5, 16, 4, 16, 4, 3, 16, 3, 16, 5]);
            break;
        case "dots":
            for (let x of [5, 12, 19]) {
                c.beginPath();
                c.arc(x, 12, 1.6, 0, 2 * Math.PI);
                c.fill();
            }
            break;
        }
    }
}
