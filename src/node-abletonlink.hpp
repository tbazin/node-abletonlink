#pragma once

#if defined(BOOST_NO_ANSI_APIS) || (defined(_MSC_VER) && (_MSC_VER >= 1800))
#   include <malloc.h>
#endif

#include <memory>
#include <queue>
#include <mutex>

#include <ableton/Link.hpp>

#include "nbind/nbind.h"
#include "nbind/api.h"
#include <node.h>

namespace bbb {
    class AbletonLink;

    struct ALTempoQueue {
        AbletonLink *that;
        double bpm;
    };
    struct ALNumPeersQueue {
        AbletonLink *that;
        std::size_t numPeers;
    };
    struct ALPlayingQueue {
        AbletonLink *that;
        bool isPlaying;
    };

    static uv_async_t async;
    static std::mutex bbb_mutex;
    static std::queue<ALTempoQueue> bbb_tempo_queue;
    static std::queue<ALNumPeersQueue> bbb_peers_queue;
    static std::queue<ALPlayingQueue> bbb_is_playing_queue;
    static void bbb_async_cb_handler(uv_async_t *handle);

    class AbletonLink {
        ableton::Link link;
        double beat{0.0};
        double phase{0.0};
        double bpm{120.0};
        double quantum{4.0};
        bool isPlayingWhenUpdate{false};

    public:
        AbletonLink()
        : AbletonLink(120.0) {}
        AbletonLink(double bpm)
        : AbletonLink(bpm, 4.0) {}
        AbletonLink(double bpm, double quantum)
        : AbletonLink(bpm, quantum, true) {}
        AbletonLink(double bpm, double quantum, bool enable)
        : link(bpm)
        , bpm(bpm)
        , quantum(quantum)
        , tempoCallback(nullptr)
        , numPeersCallback(nullptr)
        {
            static bool b{true};
            if(b) {
                uv_async_init(uv_default_loop(), &async, bbb_async_cb_handler);
                b = false;
            }
            link.enable(enable);
            link.setTempoCallback([this](double bpm) {
                std::lock_guard<std::mutex> lock(bbb_mutex);
                bbb_tempo_queue.push({this, bpm});
                uv_async_send(&async);
            });
            link.setNumPeersCallback([this](std::size_t num) {
                std::lock_guard<std::mutex> lock(bbb_mutex);
                bbb_peers_queue.push({this, num});
                uv_async_send(&async);
            });
            link.setStartStopCallback([this](bool isPlaying) {
                std::lock_guard<std::mutex> lock(bbb_mutex);
                bbb_is_playing_queue.push({this, isPlaying});
                uv_async_send(&async);
            });
        };
        explicit AbletonLink(const AbletonLink *other)
        : AbletonLink(other->bpm, other->quantum, other->getLinkEnable()) {}

        bool getLinkEnable() const { return link.isEnabled(); }
        void setLinkEnable(bool enable) { link.enable(enable); }
        void enable() { link.enable(true); }
        void disable() { link.enable(false); }
        
        double getBeat() const { return beat; }
        void setBeat(double beat) {
            const auto time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->requestBeatAtTime(beat, time, quantum);
        }
        void setBeatForce(double beat) {
            const auto &&time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->forceBeatAtTime(beat, time, quantum);
        }
        
        double getPhase() const { return phase; }
        void setPhase(double phase) {
        }
        
        double getBpm() const { return bpm; }
        void setBpm(double bpm) {
            this->bpm = bpm;
            const auto &&time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->setTempo(bpm, time);
        }
        
        bool getIsPlaying() const {
            auto &&sessionState = link.captureAppSessionState();
            return sessionState.isPlaying();
        }
        void setIsPlaying(bool isPlaying) {
            const auto &&time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->setIsPlaying(isPlaying, time);
        }
        void play() {
            const auto &&time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->setIsPlaying(true, time);
        }
        void stop() {
            const auto &&time = get_time();
            auto &&sessionState = get_session_state();
            sessionState->setIsPlaying(false, time);
        }

        std::size_t getNumPeers() const { return link.numPeers(); }
        
        void setQuantum(double quantum) { this->quantum = quantum; }
        double getQuantum() const { return quantum; }
        
        void onTempoChanged(nbind::cbFunction &callback) {
            on("tempo", callback);
        }
        void onNumPeersChanged(nbind::cbFunction &callback) {
            on("numPeers", callback);
        }
        void onPlayStateChanged(nbind::cbFunction &callback) {
            on("playState", callback);
        }
        void on(std::string key, nbind::cbFunction &callback) {
            if(key == "tempo") {
                tempoCallback = std::make_shared<nbind::cbFunction>(callback);
            } else if(key == "numPeers") {
                numPeersCallback = std::make_shared<nbind::cbFunction>(callback);
            } else if(key == "playState") {
                playStateCallback = std::make_shared<nbind::cbFunction>(callback);
            }
         }

        void off(std::string key) {
            if(key == "tempo") {
                tempoCallback.reset();
            } else if(key == "numPeers") {
                numPeersCallback.reset();
            } else if(key == "playState") {
                playStateCallback.reset();
            }
        }
        
        void tempoChanged(double tempo) {
            if(tempoCallback) {
                Nan::HandleScope scope;
                (*tempoCallback)(tempo);
            }
        }

        void numPeersChanged(std::size_t num) {
            if(numPeersCallback) {
                Nan::HandleScope scope;
                (*numPeersCallback)(num);
            }
        }

        void playStateChanged(bool isPlaying) {
            if(playStateCallback) {
                Nan::HandleScope scope;
                (*playStateCallback)(isPlaying);
            }
        }

        std::shared_ptr<nbind::cbFunction> tempoCallback;
        std::shared_ptr<nbind::cbFunction> numPeersCallback;
        std::shared_ptr<nbind::cbFunction> playStateCallback;

        void update() {
            const auto &&time = link.clock().micros();
            auto &&sessionState = link.captureAppSessionState();
            
            beat = sessionState.beatAtTime(time, quantum);
            phase = sessionState.phaseAtTime(time, quantum);
            bpm = sessionState.tempo();
            isPlayingWhenUpdate = sessionState.isPlaying();
        };

    private:
        inline std::chrono::microseconds get_time() const
        { return link.clock().micros(); }

        struct scoped_session_state {
            scoped_session_state() = delete;
            scoped_session_state(ableton::Link &link)
            : link(link)
            , sessionState(link.captureAppSessionState())
            {}
            ~scoped_session_state()
            { link.commitAppSessionState(sessionState); };

            ableton::Link &link;
            ableton::Link::SessionState sessionState;
            inline ableton::Link::SessionState *operator->()
            { return &sessionState; };
            inline const ableton::Link::SessionState *operator->() const
            { return &sessionState; };
        };
        inline scoped_session_state get_session_state()
        { return { link }; };
    };

    static void bbb_async_cb_handler(uv_async_t *handle) {
        std::lock_guard<std::mutex> sl(bbb_mutex);
        while(!bbb_tempo_queue.empty()) {
            auto &front = bbb_tempo_queue.front();
            front.that->tempoChanged(front.bpm);
            bbb_tempo_queue.pop();
        }
        while(!bbb_peers_queue.empty()) {
            auto &front = bbb_peers_queue.front();
            front.that->numPeersChanged(front.numPeers);
            bbb_peers_queue.pop();
        }
        while(!bbb_is_playing_queue.empty()) {
            auto &front = bbb_is_playing_queue.front();
            front.that->playStateChanged(front.isPlaying);
            bbb_is_playing_queue.pop();
        }
    }
};
