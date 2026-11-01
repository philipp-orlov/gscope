// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#include "gscope/line_stream.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>

extern char** environ;

namespace gscope {

PopenLineStream::PopenLineStream(const std::string& command)
{
    int out[2] = {-1, -1};
    int wake[2] = {-1, -1};
    if (pipe2(out, O_CLOEXEC) != 0)
        return;
    if (pipe2(wake, O_CLOEXEC | O_NONBLOCK) != 0) {
        ::close(out[0]);
        ::close(out[1]);
        return;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);  // dup2 clears close-on-exec on the target
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);  // its own group, so the whole command can be signalled

    const char* argv[] = {"sh", "-c", command.c_str(), nullptr};
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, "/bin/sh", &actions, &attributes, const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(out[1]);
    if (rc != 0) {
        ::close(out[0]);
        ::close(wake[0]);
        ::close(wake[1]);
        return;
    }
    pid_ = pid;
    outFd_ = out[0];
    wakeRead_ = wake[0];
    wakeWrite_ = wake[1];
}

PopenLineStream::~PopenLineStream()
{
    if (pid_ > 0) {
        ::kill(-pid_, SIGTERM);
        int status = 0;
        bool reaped = false;
        for (int waited = 0; waited < 200 && !reaped; ++waited) {  // up to 2 s
            reaped = ::waitpid(pid_, &status, WNOHANG) != 0;
            if (!reaped)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reaped) {
            ::kill(-pid_, SIGKILL);
            while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    for (int fd : {outFd_, wakeRead_, wakeWrite_})
        if (fd >= 0)
            ::close(fd);
}

void PopenLineStream::cancel()
{
    cancelled_.store(true);
    if (wakeWrite_ >= 0) {
        const char byte = 1;
        ssize_t ignored = ::write(wakeWrite_, &byte, 1);  // full pipe means it is already readable
        (void)ignored;
    }
}

bool PopenLineStream::nextLine(std::string& out)
{
    if (outFd_ < 0)
        return false;

    for (;;) {
        const size_t newline = pending_.find('\n');
        if (newline != std::string::npos) {
            out.assign(pending_, 0, newline);
            pending_.erase(0, newline + 1);
            return true;
        }
        if (cancelled_.load())
            return false;

        pollfd fds[2] = {{outFd_, POLLIN, 0}, {wakeRead_, POLLIN, 0}};
        const int ready = ::poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (fds[1].revents & POLLIN)
            return false;  // cancelled
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buffer[4096];
            const ssize_t n = ::read(outFd_, buffer, sizeof buffer);
            if (n > 0) {
                pending_.append(buffer, static_cast<size_t>(n));
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                // End of the stream: a last line without a newline still counts.
                if (pending_.empty())
                    return false;
                out.swap(pending_);
                pending_.clear();
                return true;
            }
        }
    }
}

}  // namespace gscope
