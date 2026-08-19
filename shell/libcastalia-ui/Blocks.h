// Castalia shell — the block layer, as the desktop sees it (Bible §9, §12.3).
//
// Two apps need the same view of the machine's disks: the Disk Manager
// (§9, mount/unmount/format) and the Migration Assistant (§9, find the old
// Windows disk). Reading `lsblk --json` twice, slightly differently, is how
// two apps end up disagreeing about what a partition is — so it lives here,
// with the shared foundation, and each app keeps only its own *policy*.
//
// util-linux is not a stable contract in the way a header is: it has shipped
// `size` as a number and as a string, `rm` as a bool and as "0", and
// `mountpoint` as null. The parser absorbs all of that rather than losing a
// disk list to it.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace castalia {
namespace blocks {

// One block device: a whole disk, or one partition of one.
struct Block {
    QString name;          // sda1
    QString path;          // /dev/sda1
    QString type;          // disk / part / rom / loop
    QString fstype;        // ext4 / vfat / ntfs / …
    QString label;
    QString mountpoint;    // empty when not mounted
    QString model;
    QString transport;     // usb / sata / nvme
    qint64 size = 0;       // bytes
    bool removable = false;
    bool readOnly = false;
    QVector<Block> children;
};

// Parse `lsblk -J -b -o NAME,PATH,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT,RM,RO,
// MODEL,TRAN`. Malformed input yields an empty list — never a crash and
// never a half-built device that an app might act on.
QVector<Block> parseLsblk(const QByteArray &json);

// Run lsblk and parse it. Empty when lsblk is not installed (a FLOOR-tier
// image without util-linux) — the caller says so rather than pretending the
// machine has no disks.
QVector<Block> list();

// Every partition on the machine, flattened, disks first then their
// children in order. Convenience for apps that do not care about the tree.
QVector<Block> partitions(const QVector<Block> &disks);

// "149.1 GiB". Sizes are shown to people, so this is base-2 with one decimal
// from GiB up, and an em dash for "unknown" rather than a misleading 0 B.
QString humanBytes(qint64 bytes);

} // namespace blocks
} // namespace castalia
