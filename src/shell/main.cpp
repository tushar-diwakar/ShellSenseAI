#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <limits.h>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <cstring>
#include <signal.h>
#include <limits>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <termios.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/stat.h>

using namespace std;

vector<string> command_history;
string monitor_script;
string ai_script;
string python_bin;

// ==========================================
// PHASE 5: SIGNAL HANDLING
// ==========================================

volatile sig_atomic_t sigchld_received = 0;
volatile sig_atomic_t sigint_received = 0;
volatile sig_atomic_t sigterm_received = 0;

void handle_sigchld(int signal_number) {
    (void)signal_number;
    sigchld_received = 1;
}

void handle_sigint(int signal_number) {
    (void)signal_number;
    sigint_received = 1;
    const char newline = '\n';
    (void)write(STDOUT_FILENO, &newline, 1);
}

void handle_sigterm(int signal_number) {
    (void)signal_number;
    sigterm_received = 1;
    const char newline = '\n';
    (void)write(STDOUT_FILENO, &newline, 1);
}

bool setup_signal_handlers() {
    struct sigaction child_action{};
    child_action.sa_handler = handle_sigchld;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &child_action, nullptr) == -1) {
        perror("ShellSense: sigaction SIGCHLD");
        return false;
    }

    struct sigaction interrupt_action{};
    interrupt_action.sa_handler = handle_sigint;
    sigemptyset(&interrupt_action.sa_mask);
    interrupt_action.sa_flags = 0;

    if (sigaction(SIGINT, &interrupt_action, nullptr) == -1) {
        perror("ShellSense: sigaction SIGINT");
        return false;
    }

    struct sigaction terminate_action{};
    terminate_action.sa_handler = handle_sigterm;
    sigemptyset(&terminate_action.sa_mask);
    terminate_action.sa_flags = 0;

    if (sigaction(SIGTERM, &terminate_action, nullptr) == -1) {
        perror("ShellSense: sigaction SIGTERM");
        return false;
    }

    return true;
}

void restore_child_signal_defaults() {
    struct sigaction default_action{};
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    default_action.sa_flags = 0;

    sigaction(SIGINT, &default_action, nullptr);
    sigaction(SIGTERM, &default_action, nullptr);
    sigaction(SIGCHLD, &default_action, nullptr);
}

// ==========================================
// 1. BACKGROUND JOB MANAGEMENT
// ==========================================

struct BackgroundJob {
    vector<pid_t> pids;
    string command;
};

vector<BackgroundJob> background_jobs;

void add_background_job(
    const vector<pid_t>& pids,
    const string& command
) {
    if (pids.empty()) return;

    background_jobs.push_back({pids, command});

    cout << "[Background job started]";

    for (pid_t pid : pids) {
        cout << " PID: " << pid;
    }

    cout << '\n';
}

void update_background_jobs() {

    for (auto job = background_jobs.begin();
         job != background_jobs.end();) {

        for (auto pid = job->pids.begin();
             pid != job->pids.end();) {

            int status;

            pid_t result = waitpid(*pid, &status, WNOHANG);

            if (result == *pid ||
                (result == -1 && errno == ECHILD)) {

                pid = job->pids.erase(pid);
            }
            else {
                ++pid;
            }
        }

        if (job->pids.empty()) {

            cout << "\n[Background job completed] "
                 << job->command << '\n';

            job = background_jobs.erase(job);
        }
        else {
            ++job;
        }
    }
}

void show_jobs() {

    update_background_jobs();

    if (background_jobs.empty()) {
        cout << "No running background jobs.\n";
        return;
    }

    for (size_t i = 0; i < background_jobs.size(); i++) {

        cout << "[" << i + 1 << "] ";

        for (pid_t pid : background_jobs[i].pids) {
            cout << "PID: " << pid << " ";
        }

        cout << "Running - "
             << background_jobs[i].command << '\n';
    }
}

// ==========================================
// PHASE 5: NAMED PIPE (FIFO) IPC
// ==========================================

void create_fifo(const vector<string>& args) {
    if (args.size() != 2) {
        cerr << "Usage: mkfifo <path>\n";
        return;
    }

    if (mkfifo(args[1].c_str(), 0666) == -1) {
        perror("ShellSense: mkfifo");
        return;
    }

    cout << "FIFO created: " << args[1] << '\n';
}

void fifo_send(const vector<string>& args) {
    if (args.size() < 3) {
        cerr << "Usage: fifo-send <path> <message>\n";
        return;
    }

    int fd = open(args[1].c_str(), O_WRONLY);
    if (fd == -1) {
        perror("ShellSense: fifo-send open");
        return;
    }

    string message;
    for (size_t i = 2; i < args.size(); ++i) {
        if (i > 2) message += ' ';
        message += args[i];
    }
    message += '\n';

    size_t written = 0;
    while (written < message.size()) {
        ssize_t count = write(fd, message.data() + written, message.size() - written);
        if (count == -1 && errno == EINTR) {
            if (sigint_received || sigterm_received) break;
            continue;
        }
        if (count <= 0) {
            perror("ShellSense: fifo-send write");
            close(fd);
            return;
        }
        written += static_cast<size_t>(count);
    }

    close(fd);
    if (written == message.size()) cout << "Message sent through FIFO.\n";
}

void fifo_receive(const vector<string>& args) {
    if (args.size() != 2) {
        cerr << "Usage: fifo-recv <path>\n";
        return;
    }

    int fd = open(args[1].c_str(), O_RDONLY);
    if (fd == -1) {
        perror("ShellSense: fifo-recv open");
        return;
    }

    cout << "Waiting for FIFO message...\n";
    cout.flush();

    char buffer[512];
    while (true) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            cout.write(buffer, count);
            cout.flush();
            continue;
        }
        if (count == 0) break;
        if (errno == EINTR) {
            if (sigint_received || sigterm_received) break;
            continue;
        }
        perror("ShellSense: fifo-recv read");
        break;
    }

    close(fd);
}

// ==========================================
// 2. PROCESS TERMINATION
// ==========================================

void terminate_background_process(const vector<string>& args) {

    if (args.size() != 2) {

        cerr << "Usage: kill <PID>\n";

        return;
    }

    char* end = nullptr;

    errno = 0;

    long value = strtol(args[1].c_str(), &end, 10);

    if (errno == ERANGE ||
        end == args[1].c_str() ||
        *end != '\0' ||
        value <= 0 ||
        value > numeric_limits<pid_t>::max()) {

        cerr << "ShellSense: invalid PID\n";

        return;
    }

    pid_t target_pid = static_cast<pid_t>(value);

    update_background_jobs();

    bool found = false;

    for (const auto& job : background_jobs) {

        for (pid_t pid : job.pids) {

            if (pid == target_pid) {

                found = true;

                break;
            }
        }

        if (found) break;
    }

    if (!found) {

        cerr << "ShellSense: PID " << target_pid
             << " is not a tracked background process\n";

        return;
    }

    if (kill(target_pid, SIGTERM) == -1) {

        perror("ShellSense: kill");

        return;
    }

    cout << "[Termination signal sent] PID: "
         << target_pid << '\n';
}
// ==========================================
// PROCESS PRIORITY MANAGEMENT (RENICE)
// ==========================================

void change_process_priority(const vector<string>& args) {

    if (args.size() != 3) {
        cerr << "Usage: renice <priority> <PID>\n";
        return;
    }

    char* priority_end = nullptr;
    char* pid_end = nullptr;

    errno = 0;

    long priority = strtol(
        args[1].c_str(),
        &priority_end,
        10
    );

    if (errno == ERANGE ||
        priority_end == args[1].c_str() ||
        *priority_end != '\0' ||
        priority < -20 ||
        priority > 19) {

        cerr << "ShellSense: priority must be between -20 and 19\n";
        return;
    }

    errno = 0;

    long pid_value = strtol(
        args[2].c_str(),
        &pid_end,
        10
    );

    if (errno == ERANGE ||
        pid_end == args[2].c_str() ||
        *pid_end != '\0' ||
        pid_value <= 0 ||
        pid_value > numeric_limits<pid_t>::max()) {

        cerr << "ShellSense: invalid PID\n";
        return;
    }

    pid_t target_pid = static_cast<pid_t>(pid_value);

    update_background_jobs();

    bool found = false;

    for (const auto& job : background_jobs) {

        for (pid_t pid : job.pids) {

            if (pid == target_pid) {
                found = true;
                break;
            }
        }

        if (found) break;
    }

    if (!found) {
        cerr << "ShellSense: PID " << target_pid
             << " is not a tracked background process\n";
        return;
    }

    if (setpriority(
            PRIO_PROCESS,
            target_pid,
            static_cast<int>(priority)
        ) == -1) {

        perror("ShellSense: renice");
        return;
    }

    errno = 0;

    int actual_priority = getpriority(
        PRIO_PROCESS,
        target_pid
    );

    if (actual_priority == -1 && errno != 0) {
        perror("ShellSense: getpriority");
        return;
    }

    cout << "Process priority updated successfully.\n";
    cout << "PID: " << target_pid << '\n';
    cout << "New priority: " << actual_priority << '\n';
}

// ==========================================
// 3. PROCESS LISTING
// ==========================================

struct ProcessInfo {
    pid_t pid;
    pid_t ppid;
    char state;
    string command;
};

void show_processes() {

    DIR* proc_dir = opendir("/proc");

    if (proc_dir == nullptr) {

        perror("ShellSense: cannot open /proc");

        return;
    }

    vector<ProcessInfo> processes;

    struct dirent* entry;

    while ((entry = readdir(proc_dir)) != nullptr) {

        string name = entry->d_name;

        if (name.empty() ||
            !all_of(name.begin(), name.end(),
                [](unsigned char c) {
                    return isdigit(c);
                })) {

            continue;
        }

        string stat_path = "/proc/" + name + "/stat";

        ifstream stat_file(stat_path);

        if (!stat_file.is_open()) {
            continue;
        }

        string line;

        if (!getline(stat_file, line)) {
            continue;
        }

        size_t open_paren = line.find('(');
        size_t close_paren = line.rfind(')');

        if (open_paren == string::npos ||
            close_paren == string::npos ||
            close_paren <= open_paren) {

            continue;
        }

        try {

            pid_t pid = stoi(
                line.substr(0, open_paren)
            );

            string command = line.substr(
                open_paren + 1,
                close_paren - open_paren - 1
            );

            istringstream remaining(
                line.substr(close_paren + 1)
            );

            char state;
            pid_t ppid;

            if (!(remaining >> state >> ppid)) {
                continue;
            }

            processes.push_back({
                pid,
                ppid,
                state,
                command
            });

        }
        catch (...) {
            continue;
        }
    }

    closedir(proc_dir);

    sort(
        processes.begin(),
        processes.end(),
        [](const ProcessInfo& a, const ProcessInfo& b) {
            return a.pid < b.pid;
        }
    );

    cout << '\n';

    cout << left
         << setw(10) << "PID"
         << setw(10) << "PPID"
         << setw(10) << "STATE"
         << "COMMAND\n";

    cout << string(55, '-') << '\n';

    for (const auto& process : processes) {

        cout << left
             << setw(10) << process.pid
             << setw(10) << process.ppid
             << setw(10) << process.state
             << process.command << '\n';
    }

    cout << string(55, '-') << '\n';

    cout << "Total processes: "
         << processes.size() << '\n';
}

// ==========================================
// 4. REAL-TIME PROCESS MONITOR (TOP)
// ==========================================

struct TopProcess {
    pid_t pid;
    char state;
    string command;
    unsigned long long cpu_ticks;
    long resident_kb;
};

struct TerminalMode {
    termios original{};
    bool active = false;

    bool enable() {

        if (!isatty(STDIN_FILENO))
            return false;

        if (tcgetattr(STDIN_FILENO, &original) == -1)
            return false;

        termios raw = original;

        raw.c_lflag &= static_cast<tcflag_t>(
            ~(ICANON | ECHO)
        );

        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
            return false;

        active = true;

        return true;
    }

    void restore() {

        if (active) {

            tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &original
            );

            active = false;
        }
    }

    ~TerminalMode() {
        restore();
    }
};

unsigned long long read_total_cpu_ticks() {

    ifstream file("/proc/stat");

    string label;

    if (!(file >> label) || label != "cpu")
        return 0;

    unsigned long long value = 0;
    unsigned long long total = 0;

    while (file >> value)
        total += value;

    return total;
}

long read_total_memory_kb() {

    ifstream file("/proc/meminfo");

    string key, unit;
    long value;

    while (file >> key >> value >> unit) {

        if (key == "MemTotal:")
            return value;
    }

    return 0;
}

vector<TopProcess> read_top_processes() {

    vector<TopProcess> processes;

    DIR* proc_dir = opendir("/proc");

    if (proc_dir == nullptr)
        return processes;

    struct dirent* entry;

    while ((entry = readdir(proc_dir)) != nullptr) {

        string name = entry->d_name;

        if (name.empty() ||
            !all_of(name.begin(), name.end(),
                [](unsigned char c) {
                    return isdigit(c);
                })) {

            continue;
        }

        string stat_path = "/proc/" + name + "/stat";

        ifstream stat_file(stat_path);

        if (!stat_file.is_open())
            continue;

        string line;

        if (!getline(stat_file, line))
            continue;

        size_t open_paren = line.find('(');
        size_t close_paren = line.rfind(')');

        if (open_paren == string::npos ||
            close_paren == string::npos ||
            close_paren <= open_paren) {

            continue;
        }

        try {

            pid_t pid = stoi(
                line.substr(0, open_paren)
            );

            string command = line.substr(
                open_paren + 1,
                close_paren - open_paren - 1
            );

            istringstream fields_stream(
                line.substr(close_paren + 1)
            );

            vector<string> fields;

            string field;

            while (fields_stream >> field)
                fields.push_back(field);

            if (fields.size() <= 12)
                continue;

            char state = fields[0][0];

            unsigned long long user_ticks =
                stoull(fields[11]);

            unsigned long long system_ticks =
                stoull(fields[12]);

            long resident_kb = 0;

            ifstream status_file(
                "/proc/" + name + "/status"
            );

            string status_line;

            while (getline(status_file, status_line)) {

                if (status_line.rfind("VmRSS:", 0) == 0) {

                    istringstream memory_stream(
                        status_line.substr(6)
                    );

                    memory_stream >> resident_kb;

                    break;
                }
            }

            processes.push_back({
                pid,
                state,
                command,
                user_ticks + system_ticks,
                resident_kb
            });

        }
        catch (...) {
            continue;
        }
    }

    closedir(proc_dir);

    return processes;
}

void show_top() {

    if (!isatty(STDIN_FILENO) ||
        !isatty(STDOUT_FILENO)) {

        cerr << "ShellSense: top requires an interactive terminal and cannot be redirected\n";

        return;
    }

    TerminalMode terminal;

    if (!terminal.enable()) {

        cerr << "ShellSense: unable to configure terminal for top\n";

        return;
    }

    const long total_memory_kb =
        read_total_memory_kb();

    const long cpu_count = max(
        1L,
        sysconf(_SC_NPROCESSORS_ONLN)
    );

    map<pid_t, unsigned long long> previous_cpu_ticks;

    unsigned long long previous_total_ticks =
        read_total_cpu_ticks();

    bool first_refresh = true;
    bool quit = false;

    while (!quit && !sigint_received) {

        vector<TopProcess> processes =
            read_top_processes();

        unsigned long long current_total_ticks =
            read_total_cpu_ticks();

        unsigned long long total_delta =
            current_total_ticks >= previous_total_ticks
            ? current_total_ticks - previous_total_ticks
            : 0;

        struct DisplayProcess {
            TopProcess process;
            double cpu_percent;
            double memory_percent;
        };

        vector<DisplayProcess> display;

        for (const auto& process : processes) {

            double cpu_percent = 0.0;

            auto previous =
                previous_cpu_ticks.find(process.pid);

            if (!first_refresh &&
                previous != previous_cpu_ticks.end() &&
                total_delta > 0 &&
                process.cpu_ticks >= previous->second) {

                unsigned long long process_delta =
                    process.cpu_ticks - previous->second;

                cpu_percent =
                    (static_cast<double>(process_delta) /
                     static_cast<double>(total_delta)) *
                    static_cast<double>(cpu_count) * 100.0;
            }

            double memory_percent = 0.0;

            if (total_memory_kb > 0) {

                memory_percent =
                    (static_cast<double>(process.resident_kb) /
                     static_cast<double>(total_memory_kb)) * 100.0;
            }

            display.push_back({
                process,
                cpu_percent,
                memory_percent
            });
        }

        sort(
            display.begin(),
            display.end(),
            [](const DisplayProcess& a,
               const DisplayProcess& b) {

                return a.cpu_percent > b.cpu_percent;
            }
        );

        cout << "\033[2J\033[H";

        cout << "ShellSense AI - Process Monitor (top)\n";

        cout << "Refresh: 2 seconds | Press 'q' to quit\n";

        cout << "CPU cores: " << cpu_count
             << " | Total RAM: "
             << fixed << setprecision(1)
             << total_memory_kb / 1024.0
             << " MB\n\n";

        cout << left
             << setw(8) << "PID"
             << setw(8) << "STATE"
             << right
             << setw(9) << "CPU%"
             << setw(10) << "MEM%"
             << setw(12) << "RAM(MB)"
             << "  COMMAND\n";

        cout << string(78, '-') << '\n';

        size_t limit = min<size_t>(
            display.size(),
            20
        );

        for (size_t i = 0; i < limit; i++) {

            const auto& item = display[i];

            cout << left
                 << setw(8) << item.process.pid
                 << setw(8) << item.process.state
                 << right
                 << setw(8) << setprecision(1)
                 << item.cpu_percent << "%"
                 << setw(9) << item.memory_percent << "%"
                 << setw(11)
                 << item.process.resident_kb / 1024.0
                 << "  " << item.process.command
                 << '\n';
        }

        cout << "\nShowing " << limit
             << " of " << display.size()
             << " processes.\n";

        cout.flush();

        previous_cpu_ticks.clear();

        for (const auto& process : processes) {

            previous_cpu_ticks[process.pid] =
                process.cpu_ticks;
        }

        previous_total_ticks = current_total_ticks;

        first_refresh = false;

        fd_set read_fds;

        FD_ZERO(&read_fds);

        FD_SET(STDIN_FILENO, &read_fds);

        timeval timeout{};

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        int ready = select(
            STDIN_FILENO + 1,
            &read_fds,
            nullptr,
            nullptr,
            &timeout
        );

        if (ready > 0 &&
            FD_ISSET(STDIN_FILENO, &read_fds)) {

            char key = '\0';

            if (read(STDIN_FILENO, &key, 1) == 1 &&
                (key == 'q' || key == 'Q')) {

                quit = true;
            }
        }

        else if (ready < 0 && errno != EINTR) {

            cerr << "\nShellSense: terminal input error\n";

            quit = true;
        }
    }

    terminal.restore();
    sigint_received = 0;

    cout << "\033[2J\033[H";
}

// ==========================================
// 5. TOKENIZER
// ==========================================

struct Token {
    string value;
    char type;
};

vector<Token> tokenize(
    const string& input,
    bool& valid
) {

    vector<Token> tokens;

    string current;

    char quote = '\0';

    bool escaping = false;
    bool token_started = false;

    valid = true;

    auto flush_word = [&]() {

        if (token_started) {

            tokens.push_back({current, 'W'});

            current.clear();

            token_started = false;
        }
    };

    for (char ch : input) {

        if (escaping) {

            current += ch;

            escaping = false;

            token_started = true;

            continue;
        }

        if (ch == '\\' && quote != '\'') {

            escaping = true;

            token_started = true;

            continue;
        }

        if (quote != '\0') {

            if (ch == quote) {
                quote = '\0';
            }
            else {
                current += ch;
            }

            token_started = true;

            continue;
        }

        if (ch == '"' || ch == '\'') {

            quote = ch;

            token_started = true;

            continue;
        }

        if (isspace(static_cast<unsigned char>(ch))) {

            flush_word();

            continue;
        }

        if (ch == '|' || ch == '<' ||
            ch == '>' || ch == '&') {

            flush_word();

            tokens.push_back({
                string(1, ch),
                ch
            });

            continue;
        }

        current += ch;

        token_started = true;
    }

    if (escaping) {

        cerr << "ShellSense: incomplete escape sequence\n";

        valid = false;

        return {};
    }

    if (quote != '\0') {

        cerr << "ShellSense: unmatched quote\n";

        valid = false;

        return {};
    }

    flush_word();

    return tokens;
}

// ==========================================
// 6. COMMAND PARSER
// ==========================================

vector<string> parse_command(const string& input) {

    bool valid = false;

    vector<Token> tokens = tokenize(input, valid);

    if (!valid) return {};

    vector<string> args;

    for (const Token& token : tokens) {

        if (token.type != 'W') {

            cerr << "ShellSense: unexpected operator\n";

            return {};
        }

        args.push_back(token.value);
    }

    return args;
}

// ==========================================
// 7. PIPELINE AND REDIRECTION PARSER
// ==========================================

struct ParsedLine {

    vector<vector<string>> commands;

    string input_file;
    string output_file;

    bool background = false;
    bool valid = true;
};

ParsedLine parse_input(const string& input) {

    ParsedLine result;

    bool token_valid = false;

    vector<Token> tokens = tokenize(
        input,
        token_valid
    );

    if (!token_valid) {

        result.valid = false;

        return result;
    }

    vector<string> current_command;

    bool input_found = false;
    bool output_found = false;

    for (size_t i = 0; i < tokens.size(); i++) {

        char type = tokens[i].type;

        if (type == 'W') {

            current_command.push_back(
                tokens[i].value
            );
        }

        else if (type == '|') {

            if (current_command.empty()) {

                cerr << "ShellSense: empty pipeline command\n";

                result.valid = false;

                return result;
            }

            result.commands.push_back(current_command);

            current_command.clear();
        }

        else if (type == '<' || type == '>') {

            if (i + 1 >= tokens.size() ||
                tokens[i + 1].type != 'W') {

                cerr << "ShellSense: missing filename for redirection\n";

                result.valid = false;

                return result;
            }

            string filename = tokens[++i].value;

            if (type == '<') {

                if (input_found) {

                    cerr << "ShellSense: multiple input redirections not supported\n";

                    result.valid = false;

                    return result;
                }

                result.input_file = filename;

                input_found = true;
            }

            else {

                if (output_found) {

                    cerr << "ShellSense: multiple output redirections not supported\n";

                    result.valid = false;

                    return result;
                }

                result.output_file = filename;

                output_found = true;
            }
        }

        else if (type == '&') {

            if (i != tokens.size() - 1 ||
                result.background) {

                cerr << "ShellSense: '&' must appear only once at the end\n";

                result.valid = false;

                return result;
            }

            result.background = true;
        }
    }

    if (current_command.empty()) {

        cerr << "ShellSense: empty command\n";

        result.valid = false;

        return result;
    }

    result.commands.push_back(current_command);

    return result;
}

// ==========================================
// 8. HELP AND HISTORY
// ==========================================

void show_help() {

    cout << "\n========== ShellSense AI ==========\n";

    cout << "Built-in commands:\n";
    cout << "  help       Show help\n";
    cout << "  pwd        Current directory\n";
    cout << "  cd         Change directory\n";
    cout << "  history    Show command history\n";
    cout << "  jobs       Show background jobs\n";
    cout << "  kill       Terminate a background process\n";
    cout << "  renice     Change background process priority\n";
    cout << "  mkfifo     Create a named pipe (FIFO)\n";
    cout << "  fifo-send  Send a message through a FIFO\n";
    cout << "  fifo-recv  Receive a message from a FIFO\n";
    cout << "  ps         Show running processes\n";
    cout << "  top        Real-time process monitor (press q to quit)\n";
    cout << "  sysinfo    System monitoring\n";
    cout << "  exit       Exit shell\n";

    cout << "\nExternal commands:\n";
    cout << "  ls, cat, echo, date, whoami, wc, sort\n";

    cout << "\nSupported features:\n";
    cout << "  command1 | command2\n";
    cout << "  command > file\n";
    cout << "  command < file\n";
    cout << "  command < input.txt > output.txt\n";
    cout << "  command1 | command2 > file\n";
    cout << "  command &\n";
    cout << "  jobs\n";
    cout << "  kill PID\n";
    cout << "  renice <priority> <PID>\n";
    cout << "  mkfifo /tmp/myfifo\n";
    cout << "  fifo-recv /tmp/myfifo\n";
    cout << "  fifo-send /tmp/myfifo Hello from ShellSense\n";
    cout << "  ps\n";
    cout << "  top\n";
    cout << "  ai ask <question>\n";
    cout << "  ai explain <command>\n";
    cout << "  ai suggest <task>\n";
    cout << "  ai diagnose <error message>\n";
    cout << "  sysinfo    System monitoring\n";
    cout << "  ai         Local AI assistant\n";

    cout << "===================================\n\n";
}

void show_history() {

    for (size_t i = 0; i < command_history.size(); i++) {

        cout << i + 1 << ". "
             << command_history[i] << '\n';
    }
}

// ==========================================
// 9. DIRECTORY MANAGEMENT
// ==========================================

void change_directory(const vector<string>& args) {

    string path;

    if (args.size() > 2) {

        cerr << "Usage: cd [directory]\n";

        return;
    }

    if (args.size() < 2 || args[1] == "~") {

        const char* home = getenv("HOME");

        if (home == nullptr) {

            cerr << "ShellSense: HOME is not set\n";

            return;
        }

        path = home;
    }

    else {

        path = args[1];

        if (path.rfind("~/", 0) == 0) {

            const char* home = getenv("HOME");

            if (home != nullptr) {

                path = string(home) + path.substr(1);
            }
        }
    }

    if (chdir(path.c_str()) != 0) {

        perror("ShellSense: cd");
    }
}

// ==========================================
// 10. REDIRECTION HELPERS
// ==========================================

bool redirect_input(const string& filename) {

    if (filename.empty()) return true;

    int fd = open(filename.c_str(), O_RDONLY);

    if (fd < 0) {

        perror("ShellSense: input redirection");

        return false;
    }

    if (dup2(fd, STDIN_FILENO) == -1) {

        perror("ShellSense: dup2 stdin");

        close(fd);

        return false;
    }

    close(fd);

    return true;
}

bool redirect_output(const string& filename) {

    if (filename.empty()) return true;

    int fd = open(
        filename.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC,
        0644
    );

    if (fd < 0) {

        perror("ShellSense: output redirection");

        return false;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {

        perror("ShellSense: dup2 stdout");

        close(fd);

        return false;
    }

    close(fd);

    return true;
}

// ==========================================
// 11. EXTERNAL COMMAND EXECUTION
// ==========================================

// Forward declaration (defaults live here only, not in the definition)
void execute_ai(
    const vector<string>& args,
    const string& input_file = "",
    const string& output_file = "",
    bool background = false,
    const string& command_line = ""
);

void execute_external_command(
    const vector<string>& args,
    const string& input_file = "",
    const string& output_file = "",
    bool background = false,
    const string& command_line = ""
) {

    if (args.empty()) return;

    vector<char*> argv;

    for (const string& arg : args) {
        argv.push_back(
            const_cast<char*>(arg.c_str())
        );
    }

    argv.push_back(nullptr);

    int error_pipe[2];

    // O_CLOEXEC: the write end closes automatically if exec succeeds,
    // so the parent never hangs waiting on it.
    if (pipe2(error_pipe, O_CLOEXEC) < 0) {
        perror("ShellSense: pipe");
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("ShellSense: fork");
        close(error_pipe[0]);
        close(error_pipe[1]);
        return;
    }

    if (pid == 0) {

        close(error_pipe[0]);

        restore_child_signal_defaults();

        if (!redirect_input(input_file)) {
            _exit(1);
        }

        if (!redirect_output(output_file)) {
            _exit(1);
        }

        execvp(argv[0], argv.data());

        int saved_errno = errno;

        (void)write(
            error_pipe[1],
            &saved_errno,
            sizeof(saved_errno)
        );

        errno = saved_errno;
        perror("ShellSense: execvp");

        _exit(127);
    }

    close(error_pipe[1]);

    if (background) {

        close(error_pipe[0]);

        add_background_job({pid}, command_line);

        return;
    }

    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        perror("ShellSense: waitpid");
        close(error_pipe[0]);
        return;
    }

    int exec_error = 0;

    ssize_t bytes_read;

    do {
        bytes_read = read(
            error_pipe[0],
            &exec_error,
            sizeof(exec_error)
        );
    } while (bytes_read < 0 && errno == EINTR);

    close(error_pipe[0]);

    if (bytes_read != static_cast<ssize_t>(sizeof(exec_error))) {
        exec_error = 0;
    }

    if (WIFEXITED(status)) {

        int exit_code = WEXITSTATUS(status);

        cout << "[Exit code: "
             << exit_code << "]\n";

        // Never auto-diagnose ShellSense's own helper scripts (ai / sysinfo).
        // Otherwise a failing ai script would call ai diagnose again,
        // forever (infinite recursion).
        bool is_internal_helper = (args[0] == python_bin);

        if (exit_code != 0 && !is_internal_helper) {

            cout << "\n[ShellSense AI] Command failed.\n";

            string error_text = (exec_error != 0)
                ? string(strerror(exec_error))
                : string("command exited with a non-zero status");

            vector<string> ai_args = {
                "ai",
                "diagnose",
                "Command: " + command_line,
                "Exit code: " + to_string(exit_code),
                "Error: " + error_text
            };

            execute_ai(ai_args);
        }
    }

    else if (WIFSIGNALED(status)) {

        cout << "[Terminated by signal: "
             << WTERMSIG(status) << "]\n";
    }
}

// ==========================================
// 12. PIPELINE EXECUTION
// ==========================================

void execute_pipeline(
    const vector<vector<string>>& commands,
    const string& input_file = "",
    const string& output_file = "",
    bool background = false,
    const string& command_line = ""
) {

    if (commands.size() < 2) return;

    int previous_read = -1;

    vector<pid_t> children;

    for (size_t i = 0; i < commands.size(); i++) {

        int pipe_fd[2] = {-1, -1};

        bool has_next = (i + 1 < commands.size());

        if (has_next && pipe(pipe_fd) == -1) {

            perror("ShellSense: pipe");

            if (previous_read != -1) {
                close(previous_read);
            }

            return;
        }

        pid_t pid = fork();

        if (pid < 0) {

            perror("ShellSense: fork");

            if (previous_read != -1) {
                close(previous_read);
            }

            if (has_next) {
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }

            for (pid_t child : children) {
                waitpid(child, nullptr, 0);
            }

            return;
        }

        if (pid == 0) {

            restore_child_signal_defaults();

            if (i == 0 && !input_file.empty()) {

                if (!redirect_input(input_file)) {
                    _exit(1);
                }
            }

            else if (previous_read != -1) {

                if (dup2(previous_read, STDIN_FILENO) == -1) {

                    perror("ShellSense: dup2 stdin");

                    _exit(1);
                }
            }

            if (has_next) {

                if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {

                    perror("ShellSense: dup2 stdout");

                    _exit(1);
                }
            }

            else if (!output_file.empty()) {

                if (!redirect_output(output_file)) {
                    _exit(1);
                }
            }

            if (previous_read != -1) {
                close(previous_read);
            }

            if (has_next) {
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }

            vector<char*> argv;

            for (const string& arg : commands[i]) {

                argv.push_back(
                    const_cast<char*>(arg.c_str())
                );
            }

            argv.push_back(nullptr);

            execvp(argv[0], argv.data());

            perror("ShellSense: pipeline command");

            _exit(127);
        }

        children.push_back(pid);

        if (previous_read != -1) {
            close(previous_read);
        }

        if (has_next) {

            close(pipe_fd[1]);

            previous_read = pipe_fd[0];
        }
    }

    if (background) {

        add_background_job(children, command_line);

        return;
    }

    int last_status = 0;

    bool last_status_valid = false;

    for (size_t i = 0; i < children.size(); i++) {

        int status;

        pid_t result;

        do {

            result = waitpid(children[i], &status, 0);

        } while (result < 0 && errno == EINTR);

        if (result < 0) {

            perror("ShellSense: waitpid");

            continue;
        }

        if (i == children.size() - 1) {

            last_status = status;

            last_status_valid = true;
        }
    }

    if (last_status_valid && WIFEXITED(last_status)) {

        cout << "[Exit code: "
             << WEXITSTATUS(last_status) << "]\n";
    }

    else if (last_status_valid && WIFSIGNALED(last_status)) {

        cout << "[Terminated by signal: "
             << WTERMSIG(last_status) << "]\n";
    }
}

// ==========================================
// 13. BUILT-IN COMMAND EXECUTION
// ==========================================

void run_builtin(const vector<string>& args) {

    if (args.empty()) return;

    const string& command = args[0];

    if (command == "help") {

        show_help();
    }

    else if (command == "pwd") {

        char current_directory[PATH_MAX];

        if (getcwd(current_directory, sizeof(current_directory))) {

            cout << current_directory << '\n';
        }

        else {

            perror("pwd");
        }
    }

    else if (command == "cd") {

        change_directory(args);
    }

    else if (command == "history") {

        show_history();
    }

    else if (command == "jobs") {

        show_jobs();
    }

    else if (command == "ps") {

        show_processes();
    }

    else if (command == "top") {

        show_top();
    }

    else if (command == "kill") {

        terminate_background_process(args);
    }

    else if (command == "renice") {

        change_process_priority(args);
    }

    else if (command == "mkfifo") {
        create_fifo(args);
    }

    else if (command == "fifo-send") {
        fifo_send(args);
    }

    else if (command == "fifo-recv") {
        fifo_receive(args);
    }
}

// ==========================================
// 14. BUILT-IN REDIRECTION
// ==========================================

void execute_builtin_with_redirection(
    const vector<string>& args,
    const string& input_file,
    const string& output_file
) {

    int saved_stdin = dup(STDIN_FILENO);

    int saved_stdout = dup(STDOUT_FILENO);

    if (saved_stdin < 0 || saved_stdout < 0) {

        perror("ShellSense: dup");

        if (saved_stdin >= 0) close(saved_stdin);

        if (saved_stdout >= 0) close(saved_stdout);

        return;
    }

    bool success = true;

    if (!input_file.empty()) {

        success = redirect_input(input_file);
    }

    if (success && !output_file.empty()) {

        success = redirect_output(output_file);
    }

    if (success) {

        run_builtin(args);
    }

    cout.flush();

    cerr.flush();

    if (dup2(saved_stdin, STDIN_FILENO) == -1) {

        perror("ShellSense: restore stdin");
    }

    if (dup2(saved_stdout, STDOUT_FILENO) == -1) {

        perror("ShellSense: restore stdout");
    }

    close(saved_stdin);

    close(saved_stdout);
}

// ==========================================
// 15. SYSTEM INFORMATION AND AI ASSISTANT
// ==========================================

void execute_sysinfo(
    const string& input_file = "",
    const string& output_file = "",
    bool background = false,
    const string& command_line = ""
) {

    vector<string> args = {
        python_bin,
        monitor_script
    };

    execute_external_command(
        args,
        input_file,
        output_file,
        background,
        command_line
    );
}

void execute_ai(
    const vector<string>& args,
    const string& input_file,
    const string& output_file,
    bool background,
    const string& command_line
) {
    if (args.size() < 3) {
        cerr << "Usage:\n";
        cerr << "  ai ask <question>\n";
        cerr << "  ai explain <command>\n";
        cerr << "  ai suggest <task>\n";
        cerr << "  ai diagnose <error message>\n";
        return;
    }

    string mode = args[1];

    if (mode != "ask" &&
        mode != "explain" &&
        mode != "suggest" &&
        mode != "diagnose") {

        cerr << "ShellSense: invalid AI mode\n";
        cerr << "Available modes: ask, explain, suggest, diagnose\n";
        return;
    }

    vector<string> ai_args = {
        python_bin,
        ai_script,
        mode
    };

    for (size_t i = 2; i < args.size(); i++) {
        ai_args.push_back(args[i]);
    }

    execute_external_command(
        ai_args,
        input_file,
        output_file,
        background,
        command_line
    );
}

// ==========================================
// 15b. NATURAL-LANGUAGE COMMANDS (AI -> safe command)
// ==========================================

string generate_ai_command(const string& request) {
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
        perror("ShellSense: pipe");
        return "";
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("ShellSense: fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return "";
    }

    if (pid == 0) {
        close(pipe_fd[0]);

        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execl(
            python_bin.c_str(),
            python_bin.c_str(),
            ai_script.c_str(),
            "command",
            request.c_str(),
            (char*)nullptr
        );

        _exit(127);
    }

    close(pipe_fd[1]);

    string output;
    char buffer[512];

    while (true) {
        ssize_t n = read(pipe_fd[0], buffer, sizeof(buffer));

        if (n > 0) {
            output.append(buffer, n);
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }

    close(pipe_fd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return "";
    }

    while (!output.empty() &&
           isspace(static_cast<unsigned char>(output.back()))) {
        output.pop_back();
    }

    size_t start = output.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";

    return output.substr(start);
}

bool validate_ai_command(
    const string& generated,
    vector<string>& args
) {
    if (generated.empty() || generated.size() > 500) {
        return false;
    }

    if (generated == "UNSAFE") {
        return false;
    }

    if (generated.find_first_of("|&;<>`$\n\r") != string::npos) {
        return false;
    }

    ParsedLine parsed = parse_input(generated);

    if (!parsed.valid ||
        parsed.commands.size() != 1 ||
        parsed.background ||
        !parsed.input_file.empty() ||
        !parsed.output_file.empty()) {
        return false;
    }

    args = parsed.commands[0];
    if (!args.empty()) {
    transform(
        args[0].begin(),
        args[0].end(),
        args[0].begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        }
    );
}

    if (!args.empty()) {
        transform(
            args[0].begin(),
            args[0].end(),
            args[0].begin(),
            [](unsigned char c) {
                return static_cast<char>(tolower(c));
            }
        );
    }

    if (args.empty()) return false;

    const vector<string> allowed = {
        "ls", "pwd", "date", "uname",
        "df", "free", "ps", "whoami",
        "find", "cat", "head", "tail",
        "wc", "grep", "mkdir", "touch"
    };

    if (find(allowed.begin(), allowed.end(), args[0])
        == allowed.end()) {
        return false;
    }

    // Prevent dangerous find operations (execute or delete or write files).
    if (args[0] == "find") {
        for (const string& arg : args) {
            if (arg == "-exec" || arg == "-execdir" ||
                arg == "-delete" || arg == "-ok" ||
                arg == "-okdir" || arg == "-fprint" ||
                arg == "-fprint0" || arg == "-fprintf" ||
                arg == "-fls") {
                return false;
            }
        }
    }

    return true;
}

void execute_natural_language(const string& request) {
    cout << "ShellSense AI: Understanding your request...\n";

    string generated = generate_ai_command(request);
    if (generated == "NO_COMMAND") {
    cout << "ShellSense AI: I'm here to help with Linux commands!\n";
    return;
}
    cout << "[DEBUG] Raw AI output: [" << generated << "]\n";

    if (generated == "NO_COMMAND") {
        cout << "ShellSense AI: I'm here to help with Linux commands!\n";
        return;
    }

    if (generated.empty()) {
        cerr << "ShellSense AI: Could not generate a command.\n";
        return;
    }

    vector<string> args;

    if (!validate_ai_command(generated, args)) {
        cerr << "ShellSense AI: Generated command was rejected for safety.\n";
        return;
    }

    cout << "AI generated: " << generated << "\n";

    // Commands that modify files require confirmation.
    if (args[0] == "mkdir" || args[0] == "touch") {
        cout << "This command will modify your filesystem.\n";
        cout << "Execute? (y/n): ";

        string answer;
        if (!getline(cin, answer)) {
            cin.clear();
            cout << "Cancelled.\n";
            return;
        }

        if (answer != "y" && answer != "Y" &&
            answer != "yes" && answer != "YES") {
            cout << "Command cancelled.\n";
            return;
        }
    }

    cout << "Executing command...\n";

    execute_external_command(
        args,
        "",
        "",
        false,
        request
    );
}

bool executable_exists(const string& command) {
    if (command.find('/') != string::npos) {
        return access(command.c_str(), X_OK) == 0;
    }

    const char* path_env = getenv("PATH");

    if (!path_env) return false;

    string path(path_env);
    stringstream ss(path);
    string directory;

    while (getline(ss, directory, ':')) {
        if (directory.empty()) directory = ".";

        string full_path = directory + "/" + command;

        if (access(full_path.c_str(), X_OK) == 0) {
            return true;
        }
    }

    return false;
}

bool is_shell_builtin_name(const string& command) {
    const vector<string> builtins = {
        "help", "pwd", "cd", "history",
        "jobs", "ps", "top", "kill",
        "renice", "mkfifo", "fifo-send",
        "fifo-recv", "sysinfo", "ai", "exit"
    };

    return find(
        builtins.begin(),
        builtins.end(),
        command
    ) != builtins.end();
}

// ==========================================
// 16. MAIN SHELL
// ==========================================

int main() {

    if (!setup_signal_handlers()) {
        return 1;
    }

    char startup_directory[PATH_MAX];

    if (getcwd(startup_directory, sizeof(startup_directory)) == nullptr) {

        perror("ShellSense: getcwd");

        return 1;
    }

    monitor_script = string(startup_directory) +
                     "/src/monitor/sysinfo.py";

    ai_script = string(startup_directory) +
                "/src/ai/ai_assistant.py";

    python_bin = string(startup_directory) +
                 "/.venv/bin/python";

    string input;

    cout << "\n====================================\n";

    cout << "          ShellSense AI             \n";

    cout << "   Linux Shell & System Intelligence\n";

    cout << "====================================\n";

    cout << "Type 'help' to get started.\n\n";

    while (true) {

        if (sigterm_received) {
            cout << "ShellSense received SIGTERM. Shutting down...\n";
            break;
        }

        update_background_jobs();
        sigchld_received = 0;

        // A Ctrl+C at the prompt cancels the current input and redraws it.
        if (sigint_received) {
            sigint_received = 0;
            cin.clear();
        }

        char cwd[PATH_MAX];

        if (getcwd(cwd, sizeof(cwd)) != nullptr) {

            cout << "ShellSense:" << cwd << "$ ";
        }

        else {

            cout << "ShellSense$ ";
        }

        cout.flush();

        errno = 0;
        if (!getline(cin, input)) {

            if (errno == EINTR || sigint_received) {
                cin.clear();
                sigint_received = 0;
                input.clear();
                continue;
            }

            cout << "\nExiting ShellSense AI...\n";
            break;
        }

        if (sigterm_received) {
            cout << "ShellSense received SIGTERM. Shutting down...\n";
            break;
        }

        sigint_received = 0;

        if (input.empty()) continue;

        istringstream input_stream(input);
        string first_word;
        input_stream >> first_word;

        if (!first_word.empty() &&
            first_word != "ai" &&
            !is_shell_builtin_name(first_word) &&
            !executable_exists(first_word)) {

            command_history.push_back(input);

            execute_natural_language(input);

            continue;
        }

        ParsedLine parsed = parse_input(input);

        if (!parsed.valid) {
            continue;
        }

        if (parsed.commands.empty()) {
            continue;
        }

        command_history.push_back(input);

        vector<string>& first_command = parsed.commands[0];

        if (first_command.empty()) {
            continue;
        }

        string command = first_command[0];

        if (parsed.commands.size() == 1 && command == "exit") {

            if (parsed.background) {

                cerr << "ShellSense: exit cannot run in background\n";

                continue;
            }

            cout << "Goodbye!\n";

            break;
        }

        if (parsed.commands.size() > 1) {

            execute_pipeline(
                parsed.commands,
                parsed.input_file,
                parsed.output_file,
                parsed.background,
                input
            );

            continue;
        }

        vector<string>& args = parsed.commands[0];

        bool is_builtin =
            command == "help" ||
            command == "pwd" ||
            command == "cd" ||
            command == "history" ||
            command == "jobs" ||
            command == "ps" ||
            command == "top" ||
            command == "kill" ||
            command == "renice" ||
            command == "mkfifo" ||
            command == "fifo-send" ||
            command == "fifo-recv";

        if (parsed.background && is_builtin) {

            cerr << "ShellSense: background execution of built-in commands is not supported\n";

            continue;
        }

        if (command == "ai") {

            execute_ai(
                args,
                parsed.input_file,
                parsed.output_file,
                parsed.background,
                input
            );
        }

        else if (command == "sysinfo") {

            execute_sysinfo(
                parsed.input_file,
                parsed.output_file,
                parsed.background,
                input
            );
        }

        else if (command == "top" &&
                 (!parsed.input_file.empty() ||
                  !parsed.output_file.empty())) {

            cerr << "ShellSense: top is interactive and does not support redirection\n";
        }

        else if (is_builtin) {

            execute_builtin_with_redirection(
                args,
                parsed.input_file,
                parsed.output_file
            );
        }

        else {

            execute_external_command(
                args,
                parsed.input_file,
                parsed.output_file,
                parsed.background,
                input
            );
        }
    }

    return 0;
}