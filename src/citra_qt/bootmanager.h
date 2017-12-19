// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <QGLWidget>
#include <QThread>
#include <boost/optional.hpp>
#include "common/thread.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"

class QKeyEvent;
class QScreen;

class GGLWidgetInternal;
class GMainWindow;
class GRenderWindow;

namespace State {
enum class LoadStateError;
}

class EmuThread : public QThread {
    Q_OBJECT

public:
    explicit EmuThread(GRenderWindow* render_window);

    /**
     * Start emulation (on new thread)
     * @warning Only call when not running!
     */
    void run() override;

    /**
     * Steps the emulation thread by a single CPU instruction (if the CPU is not already running)
     * @note This function is thread-safe
     */
    void ExecStep() {
        exec_step = true;
        running_cv.notify_all();
    }

    /**
     * Sets whether the emulation thread is running or not
     * @param running Boolean value, set the emulation thread to running if true
     * @note This function is thread-safe
     */
    void SetRunning(bool running) {
        {
            std::unique_lock<std::mutex> lock(running_mutex);
            this->running = running;
        }
        running_cv.notify_all();
    }

    /**
     * Check if the emulation thread is running or not
     * @return True if the emulation thread is running, otherwise false
     * @note This function is thread-safe
     */
    bool IsRunning() const {
        return running;
    }

    /**
     * Requests for the emulation thread to stop running
     */
    void RequestStop() {
        stop_run = true;
        SetRunning(false);
    }

    /**
     * Requests for current state to be saved to a file
     * @param to_file The file state should be saved to
     * @note This function is thread-safe
     */
    void RequestSaveState(std::ofstream to_file) {
        {
            std::unique_lock<std::mutex> lock(running_mutex);
            save_state = std::move(to_file);
        }
        running_cv.notify_all();
    }

    /**
     * Requests for state to be loaded from a file
     * @param from_file The file state should be loaded from
     * @note This function is thread-safe
     */
    void RequestLoadState(std::ifstream from_file) {
        {
            std::unique_lock<std::mutex> lock(running_mutex);
            load_state = std::move(from_file);
        }
        running_cv.notify_all();
    }

private:
    bool exec_step = false;
    bool running = false;
    boost::optional<std::ofstream> save_state;
    boost::optional<std::ifstream> load_state;
    std::atomic<bool> stop_run{false};
    std::mutex running_mutex;
    std::condition_variable running_cv;

    GRenderWindow* render_window;

signals:
    /**
     * Emitted when the CPU has halted execution
     *
     * @warning When connecting to this signal from other threads, make sure to specify either
     * Qt::QueuedConnection (invoke slot within the destination object's message thread) or even
     * Qt::BlockingQueuedConnection (additionally block source thread until slot returns)
     */
    void DebugModeEntered();

    /**
     * Emitted right before the CPU continues execution
     *
     * @warning When connecting to this signal from other threads, make sure to specify either
     * Qt::QueuedConnection (invoke slot within the destination object's message thread) or even
     * Qt::BlockingQueuedConnection (additionally block source thread until slot returns)
     */
    void DebugModeLeft();

    /**
     * Emitted when state has been saved.
     *
     * @warning When connecting to this signal from other threads, make sure to specify either
     * Qt::QueuedConnection (invoke slot within the destination object's message thread) or even
     * Qt::BlockingQueuedConnection (additionally block source thread until slot returns)
     */
    void SaveStateCompleted();

    /**
     * Emitted when state has been loaded.
     * @param error Result of the load. If error != None, emulation will be stopped.
     *
     * @warning When connecting to this signal from other threads, make sure to specify either
     * Qt::QueuedConnection (invoke slot within the destination object's message thread) or even
     * Qt::BlockingQueuedConnection (additionally block source thread until slot returns)
     */
    void LoadStateCompleted(State::LoadStateError error);

    void ErrorThrown(Core::System::ResultStatus, std::string);
};

class GRenderWindow : public QWidget, public EmuWindow {
    Q_OBJECT

public:
    GRenderWindow(QWidget* parent, EmuThread* emu_thread);
    ~GRenderWindow();

    // EmuWindow implementation
    void SwapBuffers() override;
    void MakeCurrent() override;
    void DoneCurrent() override;
    void PollEvents() override;

    void BackupGeometry();
    void RestoreGeometry();
    void restoreGeometry(const QByteArray& geometry); // overridden
    QByteArray saveGeometry();                        // overridden

    qreal windowPixelRatio();

    void closeEvent(QCloseEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

    void OnClientAreaResized(unsigned width, unsigned height);

    void InitRenderTarget();

public slots:
    void moveContext(); // overridden

    void OnEmulationStarting(EmuThread* emu_thread);
    void OnEmulationStopping();
    void OnFramebufferSizeChanged();

signals:
    /// Emitted when the window is closed
    void Closed();

private:
    void OnMinimalClientAreaChangeRequest(
        const std::pair<unsigned, unsigned>& minimal_size) override;

    GGLWidgetInternal* child;

    QByteArray geometry;

    EmuThread* emu_thread;

protected:
    void showEvent(QShowEvent* event) override;
};
