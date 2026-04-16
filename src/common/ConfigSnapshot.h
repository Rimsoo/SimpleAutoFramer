// src/common/ConfigSnapshot.h
//
// Thread-safe configuration container. The capture/processing thread and the
// UI/shortcut threads share framing parameters (zoom, smoothing, enabled flag).
// A naive design protects every read with a mutex, which causes contention in
// a 30-60 fps loop.
//
// Strategy here: copy-on-write shared_ptr<const Config>. Readers do an atomic
// load of the shared_ptr and then read the immutable Config without locks.
// Writers create a new Config (cheap: a few floats), and atomic-swap it in.
//
// This pattern has zero reader-side contention and is well-suited to the
// SimpleAutoFramer access pattern (many reads, few writes).

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace saf {

struct FramerConfig {
    bool  enabled          = true;
    float zoom             = 1.5f;      // current zoom level
    float smoothing        = 0.85f;     // 0=instant, 1=never moves
    float padding          = 0.3f;      // margin around the face (0..1)
    int   minFaceSize      = 80;        // px, skip smaller detections
    bool  useDnn           = true;      // true=DNN, false=Haar
    std::string cameraPath = "/dev/video0";
    std::string sinkPath   = "/dev/video20";
    int   captureWidth     = 1280;
    int   captureHeight    = 720;
    int   fps              = 30;
};

class ConfigStore {
public:
    ConfigStore()
        : m_current(std::make_shared<FramerConfig>()) {}

    /// Thread-safe read: returns an immutable snapshot.
    /// Use e.g.: auto cfg = store.snapshot(); cfg->zoom ...
    std::shared_ptr<const FramerConfig> snapshot() const {
        return std::atomic_load_explicit(&m_current, std::memory_order_acquire);
    }

    /// Thread-safe write via mutator lambda. Builds a new config from the
    /// current one, applies the mutator, and swaps it in atomically.
    template <typename F>
    void mutate(F&& fn) {
        std::lock_guard<std::mutex> lock(m_writerMutex);
        auto cur = std::atomic_load_explicit(&m_current, std::memory_order_acquire);
        auto next = std::make_shared<FramerConfig>(*cur);
        fn(*next);
        std::atomic_store_explicit(&m_current, std::shared_ptr<const FramerConfig>(next),
                                   std::memory_order_release);
    }

    /// Atomic replacement (e.g. after loading from JSON on startup).
    void replace(FramerConfig c) {
        std::lock_guard<std::mutex> lock(m_writerMutex);
        std::atomic_store_explicit(
            &m_current,
            std::shared_ptr<const FramerConfig>(std::make_shared<FramerConfig>(std::move(c))),
            std::memory_order_release);
    }

private:
    mutable std::shared_ptr<const FramerConfig> m_current;
    std::mutex m_writerMutex;   // serialises writers only
};

} // namespace saf

// ---- Usage pattern -----------------------------------------------------
//
// saf::ConfigStore store;
//
// // Capture thread (60 fps, must be fast):
// auto cfg = store.snapshot();
// if (!cfg->enabled) return;
// float z = cfg->zoom;                   // no contention
//
// // UI / shortcut thread:
// store.mutate([](saf::FramerConfig& c){ c.zoom += 0.1f; });
//
// // Startup:
// saf::FramerConfig loaded = loadFromJson(path);
// store.replace(std::move(loaded));
