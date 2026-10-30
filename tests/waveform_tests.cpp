#include "cmp/Waveform.h"
#include <QtWidgets>
#include <cmath>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    cmp::Waveform plot;
    plot.duration = plot.end = 1;
    const QVector<QColor> colors = {QColor("#ff0000"), QColor("#0000ff"), QColor("#008000"),
                                    QColor("#800080")};
    for (int channel = 0; channel < 4; ++channel)
        plot.traces.append(
            {QString::number(channel), QString::number(channel), colors[channel], true, false});
    cmp::Samples samples;
    for (int i = 0; i < 250000; ++i)
    {
        cmp::Sample sample;
        sample.time = i / 249999.;
        for (int channel = 0; channel < 4; ++channel)
            sample.values.insert(QString::number(channel),
                                 channel == 0 ? (i == 123457 ? 10. : 0.)
                                              : .2 * channel + .1 * std::sin(i * .001 * channel));
        samples.append(sample);
    }
    QElapsedTimer timer;
    timer.start();
    plot.append(samples);
    const double appendMs = timer.nsecsElapsed() / 1e6;
    auto render = [&]
    {
        QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
        plot.render(&image);
        return image;
    };
    plot.resize(1000, 300);
    render();
    QVector<double> times;
    for (int frame = 0; frame < 24; ++frame)
    {
        plot.resize(900 + frame * 7, 280 + frame % 9);
        timer.start();
        render();
        times.append(timer.nsecsElapsed() / 1e6);
    }
    std::sort(times.begin(), times.end());
    QTextStream out(stdout);
    out << "250k samples x 4 traces; append_ms=" << appendMs
        << " resize_median_ms=" << times[times.size() / 2] << " resize_max_ms=" << times.back() << '\n';
    // The single-sample spike is deliberately not aligned with stride sampling.
    plot.resize(1000, 300);
    auto image = render();
    bool spike = false;
    for (int y = 20; y < 100; ++y)
        for (int x = 55; x < image.width() - 70; ++x)
        {
            QColor c = image.pixelColor(x, y);
            spike |= c.red() > 180 && c.green() < 100 && c.blue() < 100;
        }
    out << "single_sample_spike_preserved=" << spike << '\n';
    bool pass = spike && plot.samples.size() == 250000 && plot.at(123457 / 249999.).values.value("0") == 10;
    // Hiding a trace invalidates the raster immediately, even mid-resize.
    plot.traces[0].visible = false;
    auto hidden = render();
    bool red = false;
    for (int y = 20; y < 100; ++y)
        for (int x = 55; x < hidden.width() - 70; ++x)
        {
            QColor c = hidden.pixelColor(x, y);
            red |= c.red() > 180 && c.green() < 100 && c.blue() < 100;
        }
    pass &= !red;
    plot.traces[0].visible = true;
    pass &= render() == image;
    // Build the same capture incrementally, forcing an intermediate cached paint.
    cmp::Waveform streamed;
    streamed.duration = streamed.end = 1;
    streamed.traces = plot.traces;
    streamed.resize(plot.size());
    streamed.append(samples.sliced(0, 123450));
    QImage intermediate(streamed.size(), QImage::Format_ARGB32_Premultiplied);
    streamed.render(&intermediate);
    streamed.append(samples.sliced(123450));
    QImage complete(streamed.size(), QImage::Format_ARGB32_Premultiplied);
    streamed.render(&complete);
    pass &= complete == image;
    plot.start = .48;
    plot.end = .49;
    pass &= render() != image;
    plot.resetZoom();
    pass &= render() == image;
    plot.resize(1200, 360);
    render();
    QEventLoop settle;
    QTimer::singleShot(100, &settle, &QEventLoop::quit);
    settle.exec();
    timer.start();
    render();
    out << "settled_redraw_ms=" << timer.nsecsElapsed() / 1e6 << '\n';
    timer.start();
    render();
    out << "cached_redraw_ms=" << timer.nsecsElapsed() / 1e6 << '\n';
    pass &= plot.samples.size() == samples.size();
    plot.clear();
    pass &= plot.samples.isEmpty() && render() != image;
    out << "visibility_append_zoom_clear_and_raw_sample_checks=" << pass << '\n';
    return pass ? 0 : 1;
}
