#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <atomic>
#include <csignal>
#include <cassert>
#include <cerrno>
#include <stdexcept>
#include <limits.h>
#include <filesystem>
#include <cstring>
#include <sys/wait.h>
#include <string_view>
#include <algorithm>


#define EVIOC_GRAB 1
#define EVIOC_UNGRAB 0

// ----- NEW: run the usb-gadget.sh script -----
static bool gadget_started = false;


static bool is_valid_action(std::string_view action)
{
    constexpr std::array<std::string_view, 3> allowed = {"start", "stop", "status"};
    return std::find(allowed.begin(), allowed.end(), action) != allowed.end();
}

static void run_gadget_script(const char* action)
{
    if (!is_valid_action(action)) {
        throw std::invalid_argument(std::string("Invalid action: ") + action);
    }

    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork process");
    }

    if (pid == 0) {
        std::string exe_path(PATH_MAX, '\0');
        ssize_t len = readlink("/proc/self/exe", exe_path.data(), exe_path.size());
        if (len == -1) {
            throw std::runtime_error("Failed to resolve executable path");
        }

        exe_path.resize(len); // Trim to actual size
        std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
        std::filesystem::path script_path = exe_dir / "usb-gadget.sh";
        char* const argv[] = {
            const_cast<char*>(script_path.c_str()),
            const_cast<char*>(action),
            nullptr
        };

        execv(script_path.c_str(), argv);
        perror("execv");
        _exit(1); // child must exit if exec fails
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            throw std::runtime_error("Failed to wait for child process");
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("Script execution failed with status " + std::to_string(WEXITSTATUS(status)));
        }
    }
}


// ---------------------------------------------

class FileDescriptor
{
    int fd;
public:
    explicit FileDescriptor(int fd_ = -1) noexcept : fd(fd_) {}
    ~FileDescriptor() { if (fd >= 0) ::close(fd); }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd(other.fd) { other.fd = -1; }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            if (fd >= 0) ::close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd; }
    explicit operator bool() const noexcept { return fd >= 0; }
};

class Pipe
{
    FileDescriptor read_fd;
    FileDescriptor write_fd;

public:
    Pipe()
    {
        int fds[2];
        if (::pipe(fds) == -1) {
            throw std::runtime_error("Pipe creation failed: " + std::string(strerror(errno)));
        }
        read_fd = FileDescriptor(fds[0]);
        write_fd = FileDescriptor(fds[1]);
    }

    int read() const noexcept { return read_fd.get(); }
    int write() const noexcept { return write_fd.get(); }
    bool valid() const noexcept { return read_fd && write_fd; }
};

static FileDescriptor kb_ev_fd;
static FileDescriptor kb_hid_fd;
static FileDescriptor gadget_hid_fd;
static Pipe pipefd;

static std::array<uint8_t, 8> report{};
static std::vector<uint8_t> pressed_keys;
static std::atomic<bool> running{true};

static FileDescriptor grab(const std::string_view dev)
{
    int fd = open(dev.data(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("Failed to open device " + std::string(dev) + ": " + strerror(errno));
    }
    ioctl(fd, EVIOCGRAB, EVIOC_UNGRAB);
    usleep(500000);
    if (ioctl(fd, EVIOCGRAB, EVIOC_GRAB) < 0) {
        close(fd);
        throw std::runtime_error("Failed to grab device " + std::string(dev) + ": " + strerror(errno));
    }
    return FileDescriptor{fd};
}

static void ungrab(FileDescriptor& fd)
{
    if (fd && ioctl(fd.get(), EVIOCGRAB, EVIOC_UNGRAB) < 0) {
        throw std::runtime_error("Failed to ungrab device: " + std::string(strerror(errno)));
    }
    fd = FileDescriptor{};
}

static FileDescriptor find_hidraw_device(int16_t vid, int16_t pid)
{
    struct hidraw_devinfo hidinfo{};
    char path[14]{};

    for (int x = 0; x < 16; ++x) {
        snprintf(path, sizeof(path), "/dev/hidraw%d", x);

        int fd = open(path, O_RDONLY);
        if (fd == -1) {
            continue;
        }

        if (ioctl(fd, HIDIOCGRAWINFO, &hidinfo) == -1) {
            close(fd);
            continue;
        }

        if (hidinfo.vendor == vid && hidinfo.product == pid) {
            return FileDescriptor{fd};
        }

        close(fd);
    }

    throw std::runtime_error("No matching hidraw device found for VID:PID " +
                             std::to_string(vid) + ":" + std::to_string(pid));
}

static void hid_writer_thread()
{
    std::array<uint8_t, 8> buffer{};
    while (running.load()) {
        ssize_t bytes = ::read(pipefd.read(), buffer.data(), buffer.size());
        if (bytes == (ssize_t)buffer.size()) {
            ssize_t written = ::write(gadget_hid_fd.get(), buffer.data(), buffer.size());
            if (written != (ssize_t)buffer.size()) {
                throw std::runtime_error("HID write failed: " + std::string(strerror(errno)));
            }
        } else if (bytes == -1 && errno != EINTR) {
            throw std::runtime_error("Pipe read error: " + std::string(strerror(errno)));
        }
    }
}

static void stop_gadget_if_started()
{
    if (gadget_started) {
        try
        {
            std::cout << "🛑 Stopping USB gadget...\n";
            run_gadget_script("stop");
            std::cout << "✅ Gadget stopped.\n";
            gadget_started = false;
        } catch (const std::exception& ex) {
            std::cerr << "⚠️  usb-gadget stop failed: " << ex.what() << "\n";
        }
    }
}

static void byebye()
{
    if (running.load()) {
        std::cout << "\n👋 Shutting down cleanly...\n";

        // Release any pressed keys
        pressed_keys.clear();
        report.fill(0);
        for (int i = 0; i < 3; ++i) {
            ::write(pipefd.write(), report.data(), report.size());
            usleep(1000);
        }

        running.store(false);

        // Close/ungrab input devices first
        try { ungrab(kb_ev_fd); } catch (...) {}
        kb_hid_fd = FileDescriptor{};

        // Close gadget FD before tearing gadget down
        gadget_hid_fd = FileDescriptor{};

        // Drop the pipe ends
        pipefd = Pipe{};

        // Now stop the gadget if we started it
        stop_gadget_if_started();

        std::cout << "✅ Cleanup complete. Bye!\n";
    }
}

static void handle_sigint(int)
{
    std::cout << "\n🛑 Caught SIGINT from terminal\n";
    byebye();
}

static void handle_sigterm(int)
{
    std::cout << "\n🛑 Caught SIGTERM\n";
    byebye();
}

int main()
{
    if (geteuid() != 0) {
        throw std::runtime_error("This app requires root privileges");
    }
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigterm);

    try
    {
        const std::string keyboard_path = "/dev/input/by-id/usb-Raspberry_Pi_Ltd_Pi_500_Keyboard-event-kbd";
        const std::string hidg_path = "/dev/hidg0";

        // --- Start gadget before opening /dev/hidg0 ---
        std::cout << "🚀 Starting USB gadget...\n";
        run_gadget_script("start");
        gadget_started = true;
        std::cout << "✅ Gadget started.\n";

        kb_ev_fd = grab(keyboard_path);
        kb_hid_fd = find_hidraw_device(0x2e8a, 0x0010);

        int fd = open(hidg_path.c_str(), O_WRONLY | O_SYNC);
        if (fd < 0) {
            throw std::runtime_error("Failed to open HID gadget device: " + std::string(strerror(errno)));
        }
        gadget_hid_fd = FileDescriptor(fd);

        pipefd = Pipe();

        std::thread writer(hid_writer_thread);

        while (running.load()) {
            ssize_t bytes = ::read(kb_hid_fd.get(), report.data(), report.size());
            if (bytes != (ssize_t)report.size()) {
                break;
            }

            bytes = ::write(pipefd.write(), report.data(), report.size());
            assert(bytes == (ssize_t)report.size());
        }

        writer.join();
        byebye();
    } catch (const std::exception& ex) {
        std::cerr << "❌ Exception: " << ex.what() << "\n";
        byebye();
        return 1;
    }

    return 0;
}
