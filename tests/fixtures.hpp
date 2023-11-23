#pragma once
#include "packet.hpp"
#include <filesystem>
#include <fstream>

namespace fixtures {
using pf::Bytes;
inline void le16(Bytes &v, uint16_t n) {
    v.push_back(n);
    v.push_back(n >> 8);
}
inline void le32(Bytes &v, uint32_t n) {
    le16(v, n);
    le16(v, n >> 16);
}
inline void write(const std::filesystem::path &path, const Bytes &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (!out)
        throw std::runtime_error("Could not write test fixture");
}
inline Bytes classic(std::span<const pf::Packet> packets, bool nano = false, int link = 1) {
    Bytes v;
    le32(v, nano ? 0xa1b23c4d : 0xa1b2c3d4);
    le16(v, 2);
    le16(v, 4);
    le32(v, 0);
    le32(v, 0);
    le32(v, 65535);
    le32(v, link);
    for (auto &p : packets) {
        le32(v, p.timestamp / 1000000000);
        le32(v, (p.timestamp % 1000000000) / (nano ? 1 : 1000));
        le32(v, p.bytes.size());
        le32(v, p.original);
        v.insert(v.end(), p.bytes.begin(), p.bytes.end());
    }
    return v;
}
inline Bytes pcapng(std::span<const pf::Packet> packets) {
    Bytes v;
    auto block = [&](uint32_t type, Bytes body) {
        while (body.size() % 4)
            body.push_back(0);
        uint32_t size = body.size() + 12;
        le32(v, type);
        le32(v, size);
        v.insert(v.end(), body.begin(), body.end());
        le32(v, size);
    };
    Bytes shb;
    le32(shb, 0x1a2b3c4d);
    le16(shb, 1);
    le16(shb, 0);
    le32(shb, UINT32_MAX);
    le32(shb, UINT32_MAX);
    block(0x0a0d0d0a, shb);
    Bytes idb;
    le16(idb, 1);
    le16(idb, 0);
    le32(idb, 65535);
    block(1, idb);
    for (auto &p : packets) {
        Bytes epb;
        uint64_t us = p.timestamp / 1000;
        le32(epb, 0);
        le32(epb, us >> 32);
        le32(epb, us);
        le32(epb, p.bytes.size());
        le32(epb, p.original);
        epb.insert(epb.end(), p.bytes.begin(), p.bytes.end());
        block(6, epb);
    }
    return v;
}
} // namespace fixtures
