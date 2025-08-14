// tnk.cpp
// g++ -std=c++20 -O2 -Wall -Wextra -pthread tnk.cpp -o tnk
#include <atomic>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>
#include <memory>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/input.h>   // EVIOCGRAB
#include <linux/hidraw.h>  // hidraw ioctls
#include <pthread.h>
#include <signal.h>

// ---------- Global run flag ----------
static std::atomic<bool> g_run{true};

// ---------- Thread registry (for pthread_kill broadcast) ----------
class ThreadRegistry {
public:
    static void add(pthread_t t) {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_.push_back(t);
    }
    static void remove(pthread_t t) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(threads_.begin(), threads_.end(),
                                 [&](pthread_t x){ return pthread_equal(x, t); });
        threads_.erase(it, threads_.end());
    }
    static void broadcast(int sig) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (pthread_t t : threads_) {
            pthread_kill(t, sig);
        }
    }
private:
    static std::mutex mutex_;
    static std::vector<pthread_t> threads_;
};
std::mutex ThreadRegistry::mutex_;
std::vector<pthread_t> ThreadRegistry::threads_;

// ---------- RAII FD ----------
class FileDescriptor {
    int fd_ = -1;
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    FileDescriptor& operator=(FileDescriptor&& o) noexcept {
        if (this != &o) { if (fd_ >= 0) ::close(fd_); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }
    int get() const noexcept { return fd_; }
    int release() noexcept { int t = fd_; fd_ = -1; return t; }
    explicit operator bool() const noexcept { return fd_ >= 0; }
};

// ---------- Open helpers ----------
static FileDescriptor open_ro_blocking(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) throw std::runtime_error("open_ro failed: " + std::string(std::strerror(errno)));
    return FileDescriptor(fd);
}
static FileDescriptor open_wo_blocking(const char* path) {
    int fd = ::open(path, O_WRONLY | O_SYNC);
    if (fd < 0) throw std::runtime_error("open_wo failed: " + std::string(std::strerror(errno)));
    return FileDescriptor(fd);
}

// ---------- InputGrab RAII ----------
class InputGrab {
    FileDescriptor fd_;
public:
    explicit InputGrab(const std::string& ev_path)
        : fd_(open_ro_blocking(ev_path.c_str())) {
        ioctl(fd_.get(), EVIOCGRAB, 0);
        ::usleep(5000);
        if (ioctl(fd_.get(), EVIOCGRAB, 1) < 0)
            throw std::runtime_error("grab failed on " + ev_path + ": " + std::string(std::strerror(errno)));
    }
    ~InputGrab() {
        if (fd_) {
            ioctl(fd_.get(), EVIOCGRAB, 0);
        }
    }
    int get() const noexcept { return fd_.get(); }
};

// ---------- hidraw -> event resolution + grab ----------
static std::vector<std::string> list_hidraw_sysfs() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path sys_hidraw("/sys/class/hidraw");
    if (!fs::exists(sys_hidraw)) return out;

    for (const auto& entry : fs::directory_iterator(sys_hidraw)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("hidraw", 0) == 0) {
            out.emplace_back("/dev/" + name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static std::vector<std::string> hidraw_to_event_all_sysfs(const std::string& hidraw_dev) {
    namespace fs = std::filesystem;
    std::vector<std::string> events;

    fs::path sys_hid = fs::path("/sys/class/hidraw") / fs::path(hidraw_dev).filename();
    fs::path device_link = sys_hid / "device";
    if (!fs::exists(device_link)) return events;

    fs::path device_dir;
    try {
        device_dir = fs::canonical(device_link);
    } catch (...) {
        return events;
    }

    auto add_events_in = [&](const fs::path& input_dir) {
        if (!fs::exists(input_dir)) return;
        for (const auto& q : fs::directory_iterator(input_dir)) {
            const auto ename = q.path().filename().string();
            if (ename.rfind("event", 0) == 0) {
                events.emplace_back("/dev/input/" + ename);
            }
        }
    };

    for (const auto& p : fs::directory_iterator(device_dir)) {
        const auto fname = p.path().filename().string();
        if (fname == "input") {
            for (const auto& in : fs::directory_iterator(p.path())) {
                if (in.path().filename().string().rfind("input", 0) == 0) {
                    add_events_in(in.path());
                }
            }
        } else if (fname.rfind("input", 0) == 0) {
            add_events_in(p.path());
        }
    }

    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());
    return events;
}

static std::vector<std::unique_ptr<InputGrab>> grab_all_hid_inputs_sysfs() {
    std::vector<std::unique_ptr<InputGrab>> grabs;
    const auto hidraws = list_hidraw_sysfs();
    for (const auto& h : hidraws) {
        const auto events = hidraw_to_event_all_sysfs(h);
        for (const auto& ev : events) {
            try {
                grabs.emplace_back(std::make_unique<InputGrab>(ev));
                std::cout << "[grabbed] " << h << " -> " << ev << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[warn] failed to grab " << ev << " from " << h
                          << ": " << e.what() << "\n";
            }
        }
    }
    return grabs;
}

// ---------- usb-gadget.sh runner ----------
static void run_gadget_script(const char* action) {
    pid_t pid = fork();
    if (pid == -1) throw std::runtime_error("fork failed");
    if (pid == 0) {
        char exe_path[PATH_MAX];
        ssize_t len = ::readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
        if (len < 0) _exit(127);
        exe_path[len] = '\0';
        std::filesystem::path script = std::filesystem::path(exe_path).parent_path() / "usb-gadget.sh";
        char* const argv[] = { const_cast<char*>(script.c_str()), const_cast<char*>(action), nullptr };
        ::execv(script.c_str(), argv);
        _exit(127);
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) throw std::runtime_error("waitpid failed");
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("usb-gadget.sh failed");
    }
}

// ---------- Report length parser ----------
static size_t parse_report_length(const uint8_t* data, size_t len) {
    size_t i = 0, max_bytes = 0, cur_bits = 0;
    int rep_size_bits = 0, rep_count = 0, current_id = 0;
    bool has_ids = false;
    auto flush = [&](){
        if (cur_bits) {
            size_t length = (cur_bits + 7) / 8;
                        if (current_id != 0 || has_ids) length += 1;
            max_bytes = std::max(max_bytes, length);
        }
    };
    while (i < len) {
        uint8_t b = data[i++];
        if (b == 0xFE) { // long item
            if (i + 2 > len) break;
            uint8_t size = data[i];
            i += 2;
            i += size;
            continue;
        }
        int size_code = b & 0x03;
        int size = (size_code == 3) ? 4 : size_code;
        int typ = (b >> 2) & 0x03;
        int tag = (b >> 4) & 0x0F;
        uint32_t val = 0;
        if (size) {
            if (i + size > len) break;
            std::memcpy(&val, &data[i], size);
            i += size;
        }
        if (typ == 1) { // global item
            if (tag == 0x07) rep_size_bits = val;
            else if (tag == 0x09) rep_count = val;
            else if (tag == 0x08) { flush(); current_id = val; has_ids = true; cur_bits = 0; }
        } else if (typ == 0 && tag == 0x08) { // main item: input
            cur_bits += rep_size_bits * rep_count;
        }
    }
    flush();
    if (max_bytes == 0) max_bytes = 8;
    return max_bytes;
}

// ---------- sysfs read ----------
static std::vector<uint8_t> read_report_descriptor_sysfs(const std::string& hidraw_name) {
    std::string node = hidraw_name;
    if (node.rfind("/dev/", 0) == 0) node = node.substr(5);
    std::filesystem::path p = std::filesystem::path("/sys/class/hidraw") / node / "device" / "report_descriptor";
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) throw std::runtime_error("open report_descriptor failed");
    std::vector<uint8_t> buf(4096);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    buf.resize(n);
    return buf;
}

// ---------- Forwarder ----------
class HidForwarder {
    FileDescriptor src_, dst_;
    std::thread thr_;
    pthread_t tid_{};
    size_t report_len_;
public:
    HidForwarder(FileDescriptor src, FileDescriptor dst, size_t report_len)
        : src_(std::move(src)), dst_(std::move(dst)), report_len_(report_len) {}

    void start() {
        thr_ = std::thread(&HidForwarder::loop, this);
        tid_ = thr_.native_handle();
        ThreadRegistry::add(tid_);
    }
    void stop() {
        send_null_reports();
        int s = src_.release();
        if (s >= 0) ::close(s);
        if (thr_.joinable()) thr_.join();
        if (tid_) ThreadRegistry::remove(tid_);
    }
    ~HidForwarder() { stop(); }
private:
    void loop() {
        std::vector<uint8_t> buf(report_len_);
        while (g_run.load()) {
            ssize_t r = ::read(src_.get(), buf.data(), buf.size());
            if (!g_run.load()) break;
            if (r == -1) {
                if (errno == EINTR) continue;
                break;
            }
            if (r != static_cast<ssize_t>(buf.size())) {
                std::cerr << "❌ read length mismatch: got " << r
                        << ", expected " << buf.size() << "\n";
                std::abort();
            }
            ssize_t w = ::write(dst_.get(), buf.data(), r);
            if (w != r) {
                std::cerr << "❌ write length mismatch: got " << w
                        << ", expected " << r << "\n";
                std::abort();
            }
        }
    }
    void send_null_reports() {
        if (!dst_) return;
        std::vector<uint8_t> zero(report_len_, 0);
        for (int i = 0; i < 3; ++i) {
            (void)::write(dst_.get(), zero.data(), zero.size());
            ::usleep(1000);
        }
    }
};

// ---------- Mapping hidraw -> hidg ----------
static std::vector<std::pair<std::string,std::string>>
make_hidraw_to_hidg_mapping() {
    std::vector<std::pair<std::string,std::string>> map;
    std::vector<int> nums;
    for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
        const auto name = entry.path().filename().string();
        if (name.rfind("hidraw", 0) == 0) {
            nums.push_back(std::stoi(name.substr(6)));
        }
    }
    std::sort(nums.begin(), nums.end());
    for (size_t idx = 0; idx < nums.size(); ++idx) {
        map.emplace_back("/dev/hidraw" + std::to_string(nums[idx]),
                         "/dev/hidg"   + std::to_string(idx));
    }
    return map;
}

// ---------- Signal handling ----------
static void noop_sigusr1(int) {}
static void* signal_waiter(void* arg) {
    sigset_t* set = (sigset_t*)arg;
    int sig;
    while (sigwait(set, &sig) == 0) {
        if (sig == SIGINT || sig == SIGTERM) {
            g_run.store(false);
            ThreadRegistry::broadcast(SIGUSR1);
            break;
        }
    }
    return nullptr;
}

// ---------- main ----------
int main() {
    if (geteuid() != 0) {
        std::cerr << "Needs root\n";
        return 1;
    }

    struct sigaction sa{};
    sa.sa_handler = noop_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, nullptr) != 0) {
        perror("sigaction(SIGUSR1)");
        return 1;
    }

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
        perror("pthread_sigmask");
        return 1;
    }

    pthread_t sigthr;
    if (pthread_create(&sigthr, nullptr, &signal_waiter, &set) != 0) {
        perror("pthread_create");
        return 1;
    }

    bool gadget_started = false;
    std::vector<std::unique_ptr<HidForwarder>> forwarders;
    std::vector<std::unique_ptr<InputGrab>> input_grabs;

    try {
        std::cout << "🚀 Starting USB gadget...\n";
        run_gadget_script("start");
        gadget_started = true;
        std::cout << "✅ Gadget started.\n";

        input_grabs = grab_all_hid_inputs_sysfs();

        auto mapping = make_hidraw_to_hidg_mapping();
        for (auto& [hidraw_path, hidg_path] : mapping) {
            try {
                auto desc = read_report_descriptor_sysfs(hidraw_path);
                size_t rep_len = parse_report_length(desc.data(), desc.size());
                FileDescriptor src = open_ro_blocking(hidraw_path.c_str());
                FileDescriptor dst = open_wo_blocking(hidg_path.c_str());
                auto fwd = std::make_unique<HidForwarder>(std::move(src), std::move(dst), rep_len);
                fwd->start();
                std::cout << "Forwarding " << hidraw_path << " -> "
                          << hidg_path << " (len=" << rep_len << ")\n";
                forwarders.emplace_back(std::move(fwd));
            } catch (const std::exception& e) {
                std::cerr << "Skip " << hidraw_path << ": " << e.what() << "\n";
            }
        }

        pthread_join(sigthr, nullptr);

        for (auto& f : forwarders) f->stop();

    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << "\n";
    }

    if (gadget_started) {
        std::cout << "🛑 Stopping USB gadget...\n";
        try { run_gadget_script("stop"); std::cout << "✅ Gadget stopped.\n"; }
        catch (const std::exception& e) {
            std::cerr << "⚠️ usb-gadget stop failed: " << e.what() << "\n";
        }
    }

    return 0;
}
