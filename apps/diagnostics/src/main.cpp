// castalia-diagnostico — System Information & Benchmarks (Bible §9, §16).
//
// A detailed, honest look at the machine, and a real benchmark suite that
// measures CPU, memory, disk, 2D graphics and the network — with animated
// circular gauges and a single Castalia score. Every number is measured on
// this machine, in-process, with no external benchmark dependency: the CPU
// test runs xorshift work across every core, RAM streams real buffers, disk
// writes and re-reads a temp file with fsync + fadvise, graphics times QPainter
// fill, and the network test drives a loopback socket plus a gateway ping.
//
// Pure Qt5 (Widgets/Gui) + libcastalia-ui theming, std::thread and POSIX only.
//
// Usage: castalia-diagnostico --theme classic [--repo P] [--tab sistema|
//        rendimiento] [--demo] [--screenshot out.png]

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "Mark.h"
#include "Theme.h"

// ============================================================ system info ==
namespace sysinfo {

QString slurp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}
QString slurpTrim(const QString &path) { return slurp(path).trimmed(); }

QString runCmd(const QString &prog, const QStringList &args, int ms = 1500)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(ms))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString cpuField(const QString &key)
{
    for (const QString &line : slurp(QStringLiteral("/proc/cpuinfo"))
                                   .split(QLatin1Char('\n'))) {
        if (line.startsWith(key)) {
            const int c = line.indexOf(QLatin1Char(':'));
            if (c >= 0)
                return line.mid(c + 1).trimmed();
        }
    }
    return QString();
}

int cpuThreads()
{
    int n = 0;
    for (const QString &line : slurp(QStringLiteral("/proc/cpuinfo"))
                                   .split(QLatin1Char('\n')))
        if (line.startsWith(QStringLiteral("processor")))
            ++n;
    return n > 0 ? n : int(std::thread::hardware_concurrency());
}

double cpuMaxMHz()
{
    const QString k = slurpTrim(QStringLiteral(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"));
    if (!k.isEmpty())
        return k.toDouble() / 1000.0;
    return cpuField(QStringLiteral("cpu MHz")).toDouble();
}

long memKiB(const QString &key)
{
    for (const QString &line : slurp(QStringLiteral("/proc/meminfo"))
                                   .split(QLatin1Char('\n')))
        if (line.startsWith(key))
            return line.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                .value(1).toLong();
    return 0;
}

QString human(double bytes)
{
    const char *u[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    while (bytes >= 1024.0 && i < 4) {
        bytes /= 1024.0;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', i ? 1 : 0)
        .arg(QLatin1String(u[i]));
}

QString gpu()
{
    const QString out = runCmd(QStringLiteral("lspci"), {});
    for (const QString &line : out.split(QLatin1Char('\n')))
        if (line.contains(QStringLiteral("VGA"), Qt::CaseInsensitive)
            || line.contains(QStringLiteral("3D"), Qt::CaseInsensitive)
            || line.contains(QStringLiteral("Display"),
                              Qt::CaseInsensitive)) {
            const int c = line.indexOf(QStringLiteral(": "));
            return c >= 0 ? line.mid(c + 2).trimmed() : line.trimmed();
        }
    // fallback: the DRM driver name
    const QString drv = slurpTrim(QStringLiteral(
        "/sys/class/drm/card0/device/uevent"));
    if (drv.contains(QStringLiteral("DRIVER=")))
        return QStringLiteral("Controlador DRM detectado");
    return QStringLiteral("No detectada");
}

struct Disk { QString name, model, size, kind; };
std::vector<Disk> disks()
{
    std::vector<Disk> out;
    QDir d(QStringLiteral("/sys/block"));
    for (const QString &name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                           QDir::Name)) {
        if (name.startsWith(QStringLiteral("loop"))
            || name.startsWith(QStringLiteral("ram"))
            || name.startsWith(QStringLiteral("sr")))
            continue;
        const QString base = QStringLiteral("/sys/block/") + name;
        const double sectors =
            slurpTrim(base + QStringLiteral("/size")).toDouble();
        const bool rot =
            slurpTrim(base + QStringLiteral("/queue/rotational"))
            == QStringLiteral("1");
        QString model =
            slurpTrim(base + QStringLiteral("/device/model"));
        if (model.isEmpty())
            model = QStringLiteral("disco");
        out.push_back({name, model, human(sectors * 512.0),
                       rot ? QStringLiteral("HDD") : QStringLiteral("SSD")});
    }
    return out;
}

struct Iface { QString name, ip, state, kind, speed; };
std::vector<Iface> ifaces()
{
    std::vector<Iface> out;
    QDir d(QStringLiteral("/sys/class/net"));
    for (const QString &name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                           QDir::Name)) {
        if (name == QStringLiteral("lo"))
            continue;
        const QString base = QStringLiteral("/sys/class/net/") + name;
        Iface i;
        i.name = name;
        i.state = slurpTrim(base + QStringLiteral("/operstate"));
        const bool wifi = QFile::exists(base + QStringLiteral("/wireless"))
                          || QFile::exists(base + QStringLiteral("/phy80211"));
        i.kind = wifi ? QStringLiteral("Wi-Fi") : QStringLiteral("Ethernet");
        const QString sp = slurpTrim(base + QStringLiteral("/speed"));
        if (!sp.isEmpty() && sp.toInt() > 0)
            i.speed = QStringLiteral("%1 Mbit/s").arg(sp);
        out.push_back(i);
    }
    // fill IPv4 via getifaddrs
    struct ifaddrs *ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (struct ifaddrs *p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
                continue;
            char buf[INET_ADDRSTRLEN] = {0};
            auto *sin = reinterpret_cast<struct sockaddr_in *>(p->ifa_addr);
            inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
            for (auto &it : out)
                if (it.name == QLatin1String(p->ifa_name))
                    it.ip = QString::fromLatin1(buf);
        }
        freeifaddrs(ifa);
    }
    return out;
}

QString wifiSignal()
{
    const QString w = slurp(QStringLiteral("/proc/net/wireless"));
    const QStringList lines = w.split(QLatin1Char('\n'));
    if (lines.size() >= 3) {
        const QStringList f = lines[2].simplified().split(QLatin1Char(' '));
        if (f.size() > 3) {
            QString q = f[2];
            q.remove(QLatin1Char('.'));
            return QStringLiteral("%1/70 (calidad)").arg(q);
        }
    }
    return QString();
}

QString uptime()
{
    const double s = slurpTrim(QStringLiteral("/proc/uptime"))
                         .split(QLatin1Char(' ')).value(0).toDouble();
    const int d = int(s) / 86400, h = (int(s) % 86400) / 3600,
              m = (int(s) % 3600) / 60;
    if (d)
        return QStringLiteral("%1 d %2 h %3 min").arg(d).arg(h).arg(m);
    return QStringLiteral("%1 h %2 min").arg(h).arg(m);
}

// CPU/board temperatures from the thermal zones (label -> °C).
QString temperatures()
{
    QStringList out;
    QDir d(QStringLiteral("/sys/class/thermal"));
    for (const QString &z : d.entryList({QStringLiteral("thermal_zone*")},
                                        QDir::Dirs, QDir::Name)) {
        const QString base = QStringLiteral("/sys/class/thermal/") + z;
        const double milli =
            slurpTrim(base + QStringLiteral("/temp")).toDouble();
        if (milli <= 0)
            continue;
        QString type = slurpTrim(base + QStringLiteral("/type"));
        if (type.isEmpty())
            type = z;
        out << QStringLiteral("%1 %2 °C").arg(type)
                   .arg(milli / 1000.0, 0, 'f', 1);
    }
    return out.join(QStringLiteral(" · "));
}

// Battery / AC status, if any.
QString power()
{
    QStringList out;
    QDir d(QStringLiteral("/sys/class/power_supply"));
    for (const QString &s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                        QDir::Name)) {
        const QString base = QStringLiteral("/sys/class/power_supply/") + s;
        const QString type = slurpTrim(base + QStringLiteral("/type"));
        if (type == QStringLiteral("Battery")) {
            const QString cap = slurpTrim(base + QStringLiteral("/capacity"));
            const QString st = slurpTrim(base + QStringLiteral("/status"));
            if (!cap.isEmpty())
                out << QStringLiteral("%1 %2%% (%3)").arg(s, cap, st);
        } else if (type == QStringLiteral("Mains")) {
            const QString on = slurpTrim(base + QStringLiteral("/online"));
            out << QStringLiteral("%1 %2").arg(
                s, on == QStringLiteral("1")
                       ? QStringLiteral("conectado")
                       : QStringLiteral("desconectado"));
        }
    }
    return out.join(QStringLiteral(" · "));
}

// Current per-core frequencies (MHz), compactly.
QString perCoreMHz()
{
    QStringList out;
    for (int i = 0; i < 64; ++i) {
        const QString f = slurpTrim(QStringLiteral(
            "/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(i));
        if (f.isEmpty())
            break;
        out << QString::number(qRound(f.toDouble() / 1000.0));
    }
    return out.isEmpty() ? QString()
                         : out.join(QStringLiteral(" · ")) + QStringLiteral(
                               " MHz");
}

QString osPretty()
{
    for (const QString &line : slurp(QStringLiteral("/etc/os-release"))
                                   .split(QLatin1Char('\n')))
        if (line.startsWith(QStringLiteral("PRETTY_NAME=")))
            return line.mid(12).remove(QLatin1Char('"')).trimmed();
    return QStringLiteral("Castalia OS — Castalia Classic");
}

} // namespace sysinfo

// =============================================================== benchmarks =
namespace bench {

using clk = std::chrono::steady_clock;
inline double secsSince(clk::time_point t0)
{
    return std::chrono::duration<double>(clk::now() - t0).count();
}

// Integer crunch: xorshift + multiply-mix. Returns a checksum so the optimizer
// cannot discard the work.
inline uint64_t crunch(uint64_t seed, uint64_t iters)
{
    uint64_t x = seed | 1ULL, acc = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        acc += x * 2654435761ULL;
        acc ^= acc >> 29;
    }
    return acc;
}

// Time-boxed CPU score in millions of iterations/second across `threads`.
double cpuScore(int threads, double seconds)
{
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> sink{0};
    std::vector<std::thread> pool;
    const uint64_t BLOCK = 2'000'000;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t]() {
            uint64_t local = 0, s = 0x9E3779B9ULL + uint64_t(t) * 0x1000193ULL;
            while (!stop.load(std::memory_order_relaxed)) {
                s = crunch(s, BLOCK);
                local += BLOCK;
            }
            total.fetch_add(local, std::memory_order_relaxed);
            sink.fetch_xor(s, std::memory_order_relaxed);
        });
    std::this_thread::sleep_for(
        std::chrono::milliseconds(int(seconds * 1000)));
    stop.store(true, std::memory_order_relaxed);
    for (auto &th : pool)
        th.join();
    return double(total.load()) / seconds / 1e6;
}

// Streaming memory bandwidth (GB/s): copy + read a large buffer repeatedly.
double ramScore(double seconds)
{
    const size_t N = 96u * 1024 * 1024;  // 96 MiB each
    std::vector<char> a(N), b(N);
    std::memset(a.data(), 0x5A, N);
    std::memset(b.data(), 0xA5, N);
    volatile uint64_t sink = 0;
    uint64_t moved = 0;
    const auto t0 = clk::now();
    while (secsSince(t0) < seconds) {
        std::memcpy(b.data(), a.data(), N);           // write path
        uint64_t s = 0;
        for (size_t i = 0; i < N; i += 64)            // read path
            s += uint8_t(b[i]);
        sink += s;
        moved += 2ull * N;
    }
    (void)sink;
    return double(moved) / secsSince(t0) / 1e9;
}

// Sequential disk write then read (MB/s each). Writes a temp file with fsync,
// evicts it from cache with fadvise, then reads it back.
void diskScore(const QString &dir, double *writeMBs, double *readMBs)
{
    *writeMBs = *readMBs = 0;
    struct statvfs vfs;
    double freeB = 0;
    if (statvfs(dir.toLocal8Bit().constData(), &vfs) == 0)
        freeB = double(vfs.f_bavail) * double(vfs.f_frsize);
    size_t bytes = 128u * 1024 * 1024;                // 128 MiB
    if (freeB > 0 && double(bytes) > freeB / 4)
        bytes = size_t(freeB / 4);
    if (bytes < 8u * 1024 * 1024)
        return;
    const QString path = dir + QStringLiteral("/.castalia-bench.tmp");
    const std::string p = path.toStdString();
    std::vector<char> buf(4u * 1024 * 1024, 0x33);

    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    auto t0 = clk::now();
    size_t written = 0;
    while (written < bytes) {
        const size_t chunk = std::min(buf.size(), bytes - written);
        if (::write(fd, buf.data(), chunk) != ssize_t(chunk))
            break;
        written += chunk;
    }
    ::fsync(fd);
    *writeMBs = double(written) / secsSince(t0) / 1e6;
#ifdef POSIX_FADV_DONTNEED
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
    ::close(fd);

    fd = ::open(p.c_str(), O_RDONLY);
    if (fd >= 0) {
#ifdef POSIX_FADV_DONTNEED
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
        t0 = clk::now();
        size_t red = 0;
        ssize_t r;
        while ((r = ::read(fd, buf.data(), buf.size())) > 0)
            red += size_t(r);
        *readMBs = double(red) / secsSince(t0) / 1e6;
        ::close(fd);
    }
    ::unlink(p.c_str());
}

// 2D graphics fill rate (megapixels/s): time QPainter drawing into a QImage.
double gfxScore(double seconds)
{
    const int W = 512, H = 512;
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    uint64_t px = 0;
    const auto t0 = clk::now();
    int i = 0;
    while (secsSince(t0) < seconds) {
        QLinearGradient g(0, 0, W, H);
        g.setColorAt(0, QColor((i * 7) & 255, (i * 13) & 255, 200, 180));
        g.setColorAt(1, QColor(60, (i * 5) & 255, (i * 11) & 255, 160));
        p.fillRect(0, 0, W, H, g);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawEllipse(QPoint((i * 17) % W, (i * 23) % H), 90, 90);
        px += uint64_t(W) * H + 3ull * 90 * 90;
        ++i;
    }
    p.end();
    return double(px) / secsSince(t0) / 1e6;
}

// Loopback TCP throughput (Gbit/s) over 127.0.0.1 — measures the network
// stack end to end without needing any external host.
double netLoopback()
{
    const size_t TOTAL = 384u * 1024 * 1024;
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 0;
    int one = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // ephemeral
    if (::bind(listener, reinterpret_cast<struct sockaddr *>(&addr),
               sizeof(addr)) < 0 || ::listen(listener, 1) < 0) {
        ::close(listener);
        return 0;
    }
    socklen_t al = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<struct sockaddr *>(&addr), &al);

    std::atomic<double> gbps{0};
    std::thread server([&]() {
        int c = ::accept(listener, nullptr, nullptr);
        if (c < 0)
            return;
        std::vector<char> buf(1u * 1024 * 1024);
        size_t red = 0;
        const auto t0 = clk::now();
        ssize_t r;
        while (red < TOTAL && (r = ::recv(c, buf.data(), buf.size(), 0)) > 0)
            red += size_t(r);
        const double s = secsSince(t0);
        if (s > 0)
            gbps.store(double(red) * 8.0 / s / 1e9);
        ::close(c);
    });

    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    std::vector<char> buf(1u * 1024 * 1024, 0x7E);
    if (::connect(cli, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)) == 0) {
        size_t sent = 0;
        while (sent < TOTAL) {
            ssize_t w = ::send(cli, buf.data(), buf.size(), 0);
            if (w <= 0)
                break;
            sent += size_t(w);
        }
    }
    ::close(cli);
    server.join();
    ::close(listener);
    return gbps.load();
}

double netLatencyMs()
{
    // Gateway from /proc/net/route (default route: Destination 00000000).
    QString gw;
    for (const QString &line : sysinfo::slurp(QStringLiteral("/proc/net/route"))
                                   .split(QLatin1Char('\n'))) {
        const QStringList f = line.split(QLatin1Char('\t'),
                                         Qt::SkipEmptyParts);
        if (f.size() > 2 && f[1] == QStringLiteral("00000000")) {
            bool ok = false;
            const quint32 h = f[2].toUInt(&ok, 16);
            if (ok)
                gw = QStringLiteral("%1.%2.%3.%4")
                         .arg(h & 0xFF).arg((h >> 8) & 0xFF)
                         .arg((h >> 16) & 0xFF).arg((h >> 24) & 0xFF);
            break;
        }
    }
    const QString target = gw.isEmpty() ? QStringLiteral("127.0.0.1") : gw;
    const QString out = sysinfo::runCmd(
        QStringLiteral("ping"),
        {QStringLiteral("-c"), QStringLiteral("3"),
         QStringLiteral("-w"), QStringLiteral("3"), target}, 4000);
    const int idx = out.indexOf(QStringLiteral("min/avg/"));
    if (idx >= 0) {
        const QString tail = out.mid(idx);
        const int eq = tail.indexOf(QLatin1Char('='));
        if (eq >= 0)
            return tail.mid(eq + 1).split(QLatin1Char('/')).value(1)
                .trimmed().toDouble();
    }
    return 0;
}

} // namespace bench

// ============================================================ gauge widget ==
class Gauge : public QWidget {
public:
    explicit Gauge(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(150, 150);
    }
    // norm: 0..1 fill; value/unit shown big; accent tints the arc.
    void set(double norm, const QString &value, const QString &unit,
             const QColor &accent)
    {
        m_target = qBound(0.0, norm, 1.0);
        m_value = value;
        m_unit = unit;
        m_accent = accent;
        if (!m_anim) {
            m_anim = new QTimer(this);
            connect(m_anim, &QTimer::timeout, this, [this]() {
                m_cur += (m_target - m_cur) * 0.18;
                if (std::abs(m_target - m_cur) < 0.004) {
                    m_cur = m_target;
                    m_anim->stop();
                }
                update();
            });
        }
        m_anim->start(16);
    }
    void setBusy(bool b)
    {
        m_busy = b;
        if (b) {
            if (!m_spin) {
                m_spin = new QTimer(this);
                connect(m_spin, &QTimer::timeout, this, [this]() {
                    m_phase += 6;
                    update();
                });
            }
            m_spin->start(28);
        } else if (m_spin) {
            m_spin->stop();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(14, 14, -14, -14);
        const double start = 220.0, span = -260.0;
        // track
        QPen track(QColor(0, 0, 0, 38), 11, Qt::SolidLine, Qt::RoundCap);
        p.setPen(track);
        p.drawArc(r, int(start * 16), int(span * 16));
        if (m_busy) {
            QPen b(m_accent, 11, Qt::SolidLine, Qt::RoundCap);
            p.setPen(b);
            p.drawArc(r, (m_phase % 360) * 16, 70 * 16);
            p.setPen(palette().color(QPalette::Text));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("…"));
            return;
        }
        QPen arc(m_accent, 11, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arc);
        p.drawArc(r, int(start * 16), int(span * m_cur * 16));
        // value
        p.setPen(palette().color(QPalette::Text));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 4.5);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, height() / 2.0 - 24, width(), 30),
                   Qt::AlignCenter, m_value);
        QFont sf = font();
        sf.setPointSizeF(sf.pointSizeF() - 1.0);
        p.setFont(sf);
        p.setPen(QColor(palette().color(QPalette::Text).red(),
                        palette().color(QPalette::Text).green(),
                        palette().color(QPalette::Text).blue(), 150));
        p.drawText(QRectF(0, height() / 2.0 + 6, width(), 20),
                   Qt::AlignCenter, m_unit);
    }

private:
    double m_target = 0, m_cur = 0;
    QString m_value = QStringLiteral("—"), m_unit;
    QColor m_accent = QColor(0x3E, 0x82, 0xB6);
    bool m_busy = false;
    int m_phase = 0;
    QTimer *m_anim = nullptr, *m_spin = nullptr;
};

// ============================================================ bench card ====
struct Metric {
    QString title;
    QString glyph;
    double ref;         // value that maps to a "full" gauge
    QString unit;
};

class BenchCard : public QWidget {
public:
    BenchCard(const Metric &m, const QColor &accent, QWidget *parent)
        : QWidget(parent), m_metric(m), m_accent(accent)
    {
        setObjectName(QStringLiteral("BenchCard"));
        setStyleSheet(QStringLiteral(
            "#BenchCard{background:palette(base);border:1px solid "
            "palette(mid);border-radius:10px;}"));
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 12, 14, 12);
        lay->setSpacing(4);
        auto *head = new QLabel(
            QStringLiteral("%1  %2").arg(m.glyph, m.title), this);
        QFont hf = head->font();
        hf.setBold(true);
        hf.setPointSizeF(hf.pointSizeF() + 1.0);
        head->setFont(hf);
        head->setAlignment(Qt::AlignHCenter);
        lay->addWidget(head);
        m_gauge = new Gauge(this);
        lay->addWidget(m_gauge, 0, Qt::AlignHCenter);
        m_rating = new QLabel(QStringLiteral("sin medir"), this);
        m_rating->setAlignment(Qt::AlignHCenter);
        m_rating->setObjectName(QStringLiteral("Rating"));
        lay->addWidget(m_rating);
        m_detail = new QLabel(this);
        m_detail->setAlignment(Qt::AlignHCenter);
        m_detail->setWordWrap(true);
        m_detail->setProperty("secondary", true);
        QFont df = m_detail->font();
        df.setPointSizeF(df.pointSizeF() - 1.0);
        m_detail->setFont(df);
        lay->addWidget(m_detail);
    }

    void busy() { m_gauge->setBusy(true); m_rating->setText(
        QStringLiteral("midiendo…")); }

    void report(double value, const QString &shown, const QString &detail)
    {
        m_gauge->setBusy(false);
        const double norm = value / m_metric.ref;
        m_gauge->set(norm, shown, m_metric.unit, m_accent);
        m_detail->setText(detail);
        QString word;
        QColor c;
        if (norm >= 0.80) { word = QStringLiteral("Excelente");
            c = QColor(0x2E, 0x8B, 0x57); }
        else if (norm >= 0.55) { word = QStringLiteral("Muy bueno");
            c = QColor(0x3E, 0x82, 0xB6); }
        else if (norm >= 0.33) { word = QStringLiteral("Bueno");
            c = QColor(0x6E, 0x9E, 0x3A); }
        else if (norm >= 0.16) { word = QStringLiteral("Adecuado");
            c = QColor(0xC9, 0xA2, 0x27); }
        else { word = QStringLiteral("Justo"); c = QColor(0xB0, 0x7A, 0x43); }
        m_rating->setText(word);
        m_rating->setStyleSheet(QStringLiteral(
            "#Rating{color:white;background:%1;border-radius:9px;"
            "padding:2px 12px;font-weight:bold;}").arg(c.name()));
        m_norm = norm;
    }
    double norm() const { return m_norm; }

private:
    Metric m_metric;
    QColor m_accent;
    Gauge *m_gauge = nullptr;
    QLabel *m_rating = nullptr, *m_detail = nullptr;
    double m_norm = 0;
};

// ============================================================ main window ===
// Shared results the worker thread fills and the UI timer drains.
struct Shared {
    std::mutex mu;
    int stage = -1;                 // which metric is running (-1 idle)
    bool done = false;
    struct One { bool ready = false; double value = 0; QString shown, detail; };
    One cpu, ram, disk, gfx, net;
};

class Diagnostico : public QWidget {
public:
    Diagnostico(const QString &repo, const QColor &accent, bool demo)
        : m_repo(repo), m_accent(accent)
    {
        setWindowTitle(QStringLiteral("Diagnóstico del sistema — Castalia"));
        resize(760, 560);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(buildHeader());
        auto *tabs = new QTabWidget(this);
        tabs->addTab(buildSystemTab(), QStringLiteral("Sistema"));
        tabs->addTab(buildBenchTab(), QStringLiteral("Rendimiento"));
        tabs->setCurrentIndex(0);
        m_tabs = tabs;
        root->addWidget(tabs, 1);

        m_poll = new QTimer(this);
        connect(m_poll, &QTimer::timeout, this, &Diagnostico::drain);
        if (demo)
            fillDemo();
    }
    ~Diagnostico() override
    {
        if (m_worker.joinable())
            m_worker.join();
    }
    void selectTab(int i) { if (m_tabs) m_tabs->setCurrentIndex(i); }

private:
    QString tokDetail;

    QWidget *buildHeader()
    {
        auto *head = new QWidget(this);
        head->setObjectName(QStringLiteral("DiagHeader"));
        head->setFixedHeight(60);
        head->setStyleSheet(QStringLiteral(
            "#DiagHeader{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 %1,stop:1 %2);}")
            .arg(m_accent.lighter(112).name(), m_accent.darker(118).name()));
        auto *l = new QHBoxLayout(head);
        l->setContentsMargins(16, 0, 16, 0);
        auto *mark = new QLabel(head);
        QPixmap pm(40, 40);
        pm.fill(Qt::transparent);
        QPainter pt(&pm);
        pt.setRenderHint(QPainter::Antialiasing, true);
        castalia::drawMark(&pt, QRectF(2, 2, 36, 36));
        pt.end();
        mark->setPixmap(pm);
        l->addWidget(mark);
        auto *t = new QLabel(head);
        t->setText(QStringLiteral(
            "<span style='color:white;font-size:16px;font-weight:bold'>"
            "Diagnóstico del sistema</span><br>"
            "<span style='color:#EAF1F7'>%1</span>")
            .arg(sysinfo::osPretty()));
        l->addWidget(t);
        l->addStretch(1);
        return head;
    }

    void addRow(QFormLayout *f, const QString &k, const QString &v)
    {
        if (v.trimmed().isEmpty())
            return;
        auto *val = new QLabel(v);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        f->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(k)), val);
    }

    QWidget *sectionTitle(const QString &t)
    {
        auto *l = new QLabel(t);
        l->setStyleSheet(QStringLiteral(
            "font-weight:bold;font-size:14px;color:%1;padding:10px 0 2px;")
            .arg(m_accent.name()));
        return l;
    }

    QWidget *buildSystemTab()
    {
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setContentsMargins(20, 12, 20, 16);
        lay->setSpacing(2);

        lay->addWidget(sectionTitle(QStringLiteral("Sistema operativo")));
        auto *os = new QFormLayout;
        addRow(os, QStringLiteral("Distribución"), sysinfo::osPretty());
        addRow(os, QStringLiteral("Núcleo (kernel)"),
               sysinfo::slurpTrim(QStringLiteral("/proc/sys/kernel/osrelease")));
        addRow(os, QStringLiteral("Arquitectura"),
               QSysInfo::currentCpuArchitecture());
        addRow(os, QStringLiteral("Equipo"),
               sysinfo::slurpTrim(QStringLiteral(
                   "/proc/sys/kernel/hostname")));
        addRow(os, QStringLiteral("Tiempo encendido"), sysinfo::uptime());
        addRow(os, QStringLiteral("Carga media"),
               sysinfo::slurpTrim(QStringLiteral("/proc/loadavg"))
                   .section(QLatin1Char(' '), 0, 2));
        lay->addLayout(os);

        lay->addWidget(sectionTitle(QStringLiteral("Procesador")));
        auto *cpu = new QFormLayout;
        addRow(cpu, QStringLiteral("Modelo"),
               sysinfo::cpuField(QStringLiteral("model name")));
        addRow(cpu, QStringLiteral("Núcleos / hilos"),
               QStringLiteral("%1 hilos").arg(sysinfo::cpuThreads()));
        addRow(cpu, QStringLiteral("Frecuencia máx."),
               QStringLiteral("%1 MHz").arg(sysinfo::cpuMaxMHz(), 0, 'f', 0));
        addRow(cpu, QStringLiteral("Caché L2/L3"),
               sysinfo::cpuField(QStringLiteral("cache size")));
        addRow(cpu, QStringLiteral("Frecuencia por núcleo"),
               sysinfo::perCoreMHz());
        lay->addLayout(cpu);

        const QString temps = sysinfo::temperatures();
        const QString pwr = sysinfo::power();
        if (!temps.isEmpty() || !pwr.isEmpty()) {
            lay->addWidget(sectionTitle(QStringLiteral("Sensores y energía")));
            auto *sen = new QFormLayout;
            addRow(sen, QStringLiteral("Temperatura"), temps);
            addRow(sen, QStringLiteral("Energía"), pwr);
            lay->addLayout(sen);
        }

        lay->addWidget(sectionTitle(QStringLiteral("Memoria y gráficos")));
        auto *mem = new QFormLayout;
        addRow(mem, QStringLiteral("RAM total"),
               sysinfo::human(sysinfo::memKiB(QStringLiteral("MemTotal:"))
                              * 1024.0));
        addRow(mem, QStringLiteral("RAM disponible"),
               sysinfo::human(sysinfo::memKiB(QStringLiteral("MemAvailable:"))
                              * 1024.0));
        addRow(mem, QStringLiteral("Swap"),
               sysinfo::human(sysinfo::memKiB(QStringLiteral("SwapTotal:"))
                              * 1024.0));
        addRow(mem, QStringLiteral("Gráficos (GPU)"), sysinfo::gpu());
        if (auto *scr = QGuiApplication::primaryScreen())
            addRow(mem, QStringLiteral("Pantalla"),
                   QStringLiteral("%1×%2 · %3 bpp · %4 DPI")
                       .arg(scr->geometry().width())
                       .arg(scr->geometry().height())
                       .arg(scr->depth())
                       .arg(scr->logicalDotsPerInch(), 0, 'f', 0));
        lay->addLayout(mem);

        lay->addWidget(sectionTitle(QStringLiteral("Almacenamiento")));
        auto *st = new QFormLayout;
        for (const auto &d : sysinfo::disks())
            addRow(st, QStringLiteral("/dev/%1").arg(d.name),
                   QStringLiteral("%1 · %2 · %3").arg(d.model, d.size, d.kind));
        lay->addLayout(st);

        lay->addWidget(sectionTitle(QStringLiteral("Red")));
        auto *net = new QFormLayout;
        for (const auto &i : sysinfo::ifaces())
            addRow(net, i.name,
                   QStringLiteral("%1 · %2%3%4")
                       .arg(i.kind, i.state.isEmpty()
                                        ? QStringLiteral("?") : i.state,
                            i.ip.isEmpty() ? QString()
                                           : QStringLiteral(" · ") + i.ip,
                            i.speed.isEmpty() ? QString()
                                              : QStringLiteral(" · ")
                                                    + i.speed));
        const QString sig = sysinfo::wifiSignal();
        if (!sig.isEmpty())
            addRow(net, QStringLiteral("Señal Wi-Fi"), sig);
        lay->addLayout(net);
        lay->addStretch(1);

        scroll->setWidget(body);
        return scroll;
    }

    QWidget *buildBenchTab()
    {
        auto *w = new QWidget(this);
        auto *lay = new QVBoxLayout(w);
        lay->setContentsMargins(18, 12, 18, 14);
        lay->setSpacing(10);

        auto *bar = new QHBoxLayout;
        m_total = new QLabel(
            QStringLiteral("Puntuación Castalia: <b>—</b>"), w);
        QFont tf = m_total->font();
        tf.setPointSizeF(tf.pointSizeF() + 2.0);
        m_total->setFont(tf);
        bar->addWidget(m_total);
        bar->addStretch(1);
        m_run = new QPushButton(QStringLiteral("Ejecutar todo"), w);
        m_run->setObjectName(QStringLiteral("RunAll"));
        m_run->setStyleSheet(QStringLiteral(
            "#RunAll{font-weight:bold;border-color:%1;padding:5px 18px;}")
            .arg(m_accent.name()));
        connect(m_run, &QPushButton::clicked, this, &Diagnostico::runAll);
        bar->addWidget(m_run);
        lay->addLayout(bar);

        auto *grid = new QGridLayout;
        grid->setSpacing(12);
        const Metric metrics[] = {
            {QStringLiteral("Procesador"), QStringLiteral("🧠"), 4000,
             QStringLiteral("Mop/s")},
            {QStringLiteral("Memoria"), QStringLiteral("🎞"), 16,
             QStringLiteral("GB/s")},
            {QStringLiteral("Disco"), QStringLiteral("💽"), 350,
             QStringLiteral("MB/s")},
            {QStringLiteral("Gráficos 2D"), QStringLiteral("🎨"), 400,
             QStringLiteral("Mpx/s")},
            {QStringLiteral("Red"), QStringLiteral("🌐"), 25,
             QStringLiteral("Gbit/s")},
        };
        BenchCard **cards[] = {&m_cpu, &m_ram, &m_disk, &m_gfx, &m_net};
        for (int i = 0; i < 5; ++i) {
            *cards[i] = new BenchCard(metrics[i], m_accent, w);
            grid->addWidget(*cards[i], i / 3, i % 3);
        }
        lay->addLayout(grid);
        lay->addStretch(1);
        auto *note = new QLabel(QStringLiteral(
            "Todas las medidas se toman en este equipo, en tiempo real. El "
            "disco escribe y relee un archivo temporal; la red mide la pila "
            "TCP por bucle local y el ping a la puerta de enlace."), w);
        note->setWordWrap(true);
        note->setProperty("secondary", true);
        lay->addWidget(note);
        return w;
    }

    // ---- running the suite in a worker thread ----------------------------
    void runAll()
    {
        if (m_worker.joinable())
            return;  // already running
        m_run->setEnabled(false);
        m_cpu->busy();
        m_ram->busy();
        m_disk->busy();
        m_gfx->busy();
        m_net->busy();
        {
            std::lock_guard<std::mutex> lk(m_sh.mu);
            m_sh.done = false;
            m_sh.cpu = m_sh.ram = m_sh.disk = m_sh.gfx = m_sh.net =
                Shared::One{};
        }
        const QString home = QDir::homePath();
        m_worker = std::thread([this, home]() {
            auto put = [this](Shared::One Shared::*slot, double v,
                              const QString &shown, const QString &detail) {
                std::lock_guard<std::mutex> lk(m_sh.mu);
                (m_sh.*slot).value = v;
                (m_sh.*slot).shown = shown;
                (m_sh.*slot).detail = detail;
                (m_sh.*slot).ready = true;
            };
            const int threads = sysinfo::cpuThreads();
            const double single = bench::cpuScore(1, 0.9);
            const double multi = bench::cpuScore(threads, 1.1);
            put(&Shared::cpu, multi,
                QStringLiteral("%1").arg(multi, 0, 'f', 0),
                QStringLiteral("%1 hilos · 1 hilo: %2 Mop/s")
                    .arg(threads).arg(single, 0, 'f', 0));

            const double ram = bench::ramScore(0.9);
            put(&Shared::ram, ram, QStringLiteral("%1").arg(ram, 0, 'f', 1),
                QStringLiteral("ancho de banda de copia+lectura"));

            double wr = 0, rd = 0;
            bench::diskScore(home, &wr, &rd);
            put(&Shared::disk, wr, QStringLiteral("%1").arg(wr, 0, 'f', 0),
                QStringLiteral("escritura %1 · lectura %2 MB/s")
                    .arg(wr, 0, 'f', 0).arg(rd, 0, 'f', 0));

            const double gfx = bench::gfxScore(0.8);
            put(&Shared::gfx, gfx, QStringLiteral("%1").arg(gfx, 0, 'f', 0),
                QStringLiteral("relleno con QPainter (Xrender/software)"));

            const double net = bench::netLoopback();
            const double lat = bench::netLatencyMs();
            put(&Shared::net, net, QStringLiteral("%1").arg(net, 0, 'f', 1),
                lat > 0 ? QStringLiteral("bucle local · ping %1 ms")
                              .arg(lat, 0, 'f', 1)
                        : QStringLiteral("bucle local (pila TCP/IP)"));

            std::lock_guard<std::mutex> lk(m_sh.mu);
            m_sh.done = true;
        });
        m_poll->start(60);
    }

    void drain()
    {
        bool done = false;
        std::lock_guard<std::mutex> lk(m_sh.mu);
        auto take = [](Shared::One &o, BenchCard *card) {
            if (o.ready) {
                card->report(o.value, o.shown, o.detail);
                o.ready = false;
            }
        };
        take(m_sh.cpu, m_cpu);
        take(m_sh.ram, m_ram);
        take(m_sh.disk, m_disk);
        take(m_sh.gfx, m_gfx);
        take(m_sh.net, m_net);
        done = m_sh.done;
        if (done) {
            m_poll->stop();
            if (m_worker.joinable())
                m_worker.join();
            m_run->setEnabled(true);
            updateTotal();
        }
    }

    void updateTotal()
    {
        const double avg = (m_cpu->norm() + m_ram->norm() + m_disk->norm()
                            + m_gfx->norm() + m_net->norm()) / 5.0;
        const int score = int(std::round(qBound(0.0, avg, 1.3) * 100));
        m_total->setText(QStringLiteral("Puntuación Castalia: <b>%1</b> / 100")
                             .arg(score));
    }

    // Precomputed demo values so the offscreen render shows a full dashboard.
    void fillDemo()
    {
        m_cpu->report(2600, QStringLiteral("2600"),
                      QStringLiteral("4 hilos · 1 hilo: 820 Mop/s"));
        m_ram->report(11.2, QStringLiteral("11.2"),
                      QStringLiteral("ancho de banda de copia+lectura"));
        m_disk->report(240, QStringLiteral("240"),
                       QStringLiteral("escritura 240 · lectura 512 MB/s"));
        m_gfx->report(310, QStringLiteral("310"),
                      QStringLiteral("relleno con QPainter (Xrender)"));
        m_net->report(18.6, QStringLiteral("18.6"),
                      QStringLiteral("bucle local · ping 0.4 ms"));
        updateTotal();
    }

    QString m_repo;
    QColor m_accent;
    QTabWidget *m_tabs = nullptr;
    QPushButton *m_run = nullptr;
    QLabel *m_total = nullptr;
    BenchCard *m_cpu = nullptr, *m_ram = nullptr, *m_disk = nullptr,
              *m_gfx = nullptr, *m_net = nullptr;
    QTimer *m_poll = nullptr;
    std::thread m_worker;
    Shared m_sh;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("castalia-diagnostico"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("theme"), QStringLiteral("Theme id"),
                   QStringLiteral("id"), QStringLiteral("classic")});
    cli.addOption({QStringLiteral("repo"), QStringLiteral("Repository root"),
                   QStringLiteral("path"), QStringLiteral(".")});
    cli.addOption({QStringLiteral("tab"), QStringLiteral("sistema|rendimiento"),
                   QStringLiteral("t"), QStringLiteral("sistema")});
    cli.addOption({QStringLiteral("demo"),
                   QStringLiteral("Fill benchmark gauges with demo values")});
    cli.addOption({QStringLiteral("report"),
                   QStringLiteral("Run all benchmarks headless, print, exit")});
    cli.addOption({QStringLiteral("screenshot"),
                   QStringLiteral("Render to PNG and exit"),
                   QStringLiteral("file")});
    cli.process(app);

    if (cli.isSet(QStringLiteral("report"))) {
        std::printf("== Castalia — banco de pruebas (medido en este equipo) "
                    "==\n");
        std::printf("CPU     : %s (%d hilos)\n",
                    qPrintable(sysinfo::cpuField(QStringLiteral("model name"))),
                    sysinfo::cpuThreads());
        std::fflush(stdout);
        const int th = sysinfo::cpuThreads();
        const double s1 = bench::cpuScore(1, 0.9);
        const double sN = bench::cpuScore(th, 1.1);
        std::printf("CPU     : %.0f Mop/s (1 hilo) · %.0f Mop/s (%d hilos)\n",
                    s1, sN, th);
        std::printf("Memoria : %.1f GB/s\n", bench::ramScore(0.9));
        double wr = 0, rd = 0;
        bench::diskScore(QDir::homePath(), &wr, &rd);
        std::printf("Disco   : escritura %.0f MB/s · lectura %.0f MB/s\n",
                    wr, rd);
        std::printf("Gráficos: %.0f Mpx/s (QPainter 2D)\n",
                    bench::gfxScore(0.8));
        const double net = bench::netLoopback();
        const double lat = bench::netLatencyMs();
        std::printf("Red     : %.1f Gbit/s (bucle local)%s\n", net,
                    lat > 0 ? qPrintable(QStringLiteral(" · ping %1 ms")
                                             .arg(lat, 0, 'f', 2))
                            : "");
        return 0;
    }

    const QString repo = QDir(cli.value(QStringLiteral("repo")))
                             .absolutePath();
    const QString themeId = cli.value(QStringLiteral("theme"));
    const QString accentStr =
        ThemeTokens::load(castalia::themeConfPath(repo, themeId))
            .str(QStringLiteral("colors"), QStringLiteral("accent"));
    castalia::applyTheme(&app, repo, themeId,
                         QStringLiteral("#RunAll{font-weight:bold;}"));
    const QColor accent = QColor(accentStr.isEmpty()
                                     ? QStringLiteral("#3E82B6") : accentStr);

    const bool shot = !cli.value(QStringLiteral("screenshot")).isEmpty();
    Diagnostico w(repo, accent,
                  cli.isSet(QStringLiteral("demo")) || shot);
    w.selectTab(cli.value(QStringLiteral("tab"))
                    == QStringLiteral("rendimiento") ? 1 : 0);
    w.show();

    const QString out = cli.value(QStringLiteral("screenshot"));
    if (!out.isEmpty())
        QTimer::singleShot(260, &app, [&]() {
            w.grab().save(out);
            app.quit();
        });
    return app.exec();
}
